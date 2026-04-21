"""
BLE Bridge — connects to the ESP32 BLE peripheral and bridges
dispense commands / results / weight between it and the FastAPI backend.

The backend HTTP endpoints (/pending_command, /command_result, /status_update)
stay unchanged; this script replaces the ESP32's old WiFi polling loop.

Requirements:
    pip install bleak httpx

Run (after starting the backend):
    python ble_bridge.py

Environment variables (optional):
    BACKEND_URL   — default http://localhost:8000
    POLL_INTERVAL — seconds between /pending_command polls (default 1.0)
"""

from __future__ import annotations

import asyncio
import json
import os
import sys
from typing import Optional

import httpx
from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice

# ── UUIDs (must match the ESP32 firmware) ──────────────────────────────────────

SERVICE_UUID   = "4e7a9b1c-d203-4e2a-b8f1-67c1d9e3f5a0"
CMD_CHAR_UUID  = "4e7a9b1c-d203-4e2a-b8f1-67c1d9e3f5a1"  # Write
RSLT_CHAR_UUID = "4e7a9b1c-d203-4e2a-b8f1-67c1d9e3f5a2"  # Notify
WT_CHAR_UUID   = "4e7a9b1c-d203-4e2a-b8f1-67c1d9e3f5a3"  # Notify

# ── Configuration ──────────────────────────────────────────────────────────────

BACKEND_URL   = os.getenv("BACKEND_URL", "http://localhost:8000")
POLL_INTERVAL = float(os.getenv("POLL_INTERVAL", "1.0"))


async def find_esp32(timeout: float = 15.0) -> Optional[BLEDevice]:
    """Scan for the NuChef-Dispenser peripheral by service UUID or name."""
    print(f"Scanning for NuChef-Dispenser (timeout {timeout}s) …")

    # First try: match by advertised service UUID
    device = await BleakScanner.find_device_by_filter(
        lambda d, adv: SERVICE_UUID.lower()
        in [s.lower() for s in (adv.service_uuids or [])],
        timeout=timeout,
    )

    # Fallback: BlueZ sometimes caches devices without re-reading service
    # UUIDs after a reconnect — try matching by name instead.
    if device is None:
        print("  UUID scan missed, retrying by name …")
        device = await BleakScanner.find_device_by_name(
            "NuChef-Dispenser", timeout=timeout,
        )

    if device:
        print(f"  Found: {device.name}  ({device.address})")
    else:
        print("  ESP32 not found. Is it powered on and advertising?")
    return device


async def run_bridge(device: BLEDevice) -> None:
    """Connect to the ESP32 and bridge commands / notifications."""

    last_sent_cmd_id: Optional[str] = None

    async with BleakClient(device, timeout=20.0, mtu_size=512) as client:
        # Request larger MTU so JSON commands aren't truncated
        try:
            await client._acquire_mtu()
        except Exception:
            pass
        print(f"Connected to {device.address}  (MTU {client.mtu_size})")

        async with httpx.AsyncClient(
            base_url=BACKEND_URL, timeout=5.0
        ) as http:

            # ── Notification handlers ──────────────────────────────────────

            async def _forward_result(payload: dict) -> None:
                nonlocal last_sent_cmd_id
                try:
                    r = await http.post("/command_result", json=payload)
                    print(f"  → /command_result  HTTP {r.status_code}")
                    # Allow the same command_id to be re-sent after result
                    if payload.get("command_id") == last_sent_cmd_id:
                        last_sent_cmd_id = None
                except httpx.HTTPError as exc:
                    print(f"  → /command_result  FAILED: {exc}")

            async def _forward_weight(payload: dict) -> None:
                try:
                    await http.post("/status_update", json=payload)
                except httpx.HTTPError:
                    pass  # weight heartbeats are best-effort

            def on_result_notify(_sender: int, data: bytearray) -> None:
                payload = json.loads(data.decode())
                print(f"BLE RSLT ← {payload}")
                asyncio.ensure_future(_forward_result(payload))

            def on_weight_notify(_sender: int, data: bytearray) -> None:
                payload = json.loads(data.decode())
                w = payload.get("weight", "?")
                print(f"BLE WT   ← {w} g", end="\r")  # overwrite line in-place
                asyncio.ensure_future(_forward_weight(payload))

            await client.start_notify(RSLT_CHAR_UUID, on_result_notify)
            await client.start_notify(WT_CHAR_UUID, on_weight_notify)
            print("Subscribed to RSLT + WT notifications.")

            # ── Polling loop ───────────────────────────────────────────────

            while client.is_connected:
                try:
                    r = await http.get("/pending_command")
                    cmd = r.json()

                    action = cmd.get("action")
                    if action in ("dispense", "tare"):
                        cmd_id = cmd.get("command_id")

                        # Don't re-send the same command
                        if cmd_id and cmd_id != last_sent_cmd_id:
                            payload_bytes = json.dumps(cmd, separators=(',', ':')).encode()
                            print(f"BLE CMD  → ({len(payload_bytes)} bytes) {cmd}")
                            try:
                                await client.write_gatt_char(
                                    CMD_CHAR_UUID, payload_bytes, response=True
                                )
                                last_sent_cmd_id = cmd_id
                                print(f"  write OK")
                            except Exception as write_exc:
                                print(f"  write FAILED: {write_exc}")
                    else:
                        pass  # action=none, nothing pending

                except httpx.HTTPError as exc:
                    print(f"Poll error: {exc}")
                except Exception as exc:
                    print(f"Unexpected error: {exc}")
                    break

                await asyncio.sleep(POLL_INTERVAL)

    print("Disconnected from ESP32.")


async def main() -> None:
    device = await find_esp32()
    if device is None:
        sys.exit(1)

    while True:
        try:
            await run_bridge(device)
        except Exception as exc:
            print(f"Bridge error: {exc}")

        print("Reconnecting in 3 s …")
        await asyncio.sleep(3.0)

        device = await find_esp32()
        if device is None:
            print("Could not find ESP32 again. Exiting.")
            sys.exit(1)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except (KeyboardInterrupt, SystemExit):
        pass
    finally:
        print("\nBLE bridge stopped.")
