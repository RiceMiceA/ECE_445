#!/usr/bin/env python3
"""
NuChef — Connection Soak Test  (R&V requirement 2)
===================================================
Polls GET /state (or GET /rv_state) every second for 30 minutes and logs
connection health, demo state, weight timestamps, and reconnect durations.

Usage:
    python tools/connection_soak_test.py --url http://192.168.4.28:8000
    python tools/connection_soak_test.py --url http://localhost:8000 --duration-min 30

Tip: during the test you can manually disconnect Quest or power-cycle the ESP32.
The script will detect the gap in backend connectivity and measure reconnect time.

Acceptance criteria (R&V requirement 2):
    No gap in backend reachability > 5 seconds.
    (If Quest / ESP32 disconnect, reconnect within 5 s.)

Outputs (in results/ by default):
    connection_soak.csv          — one row per poll
    connection_soak_summary.txt  — human-readable summary with max reconnect time
"""

from __future__ import annotations

import argparse
import csv
import sys
import time
from pathlib import Path

try:
    import requests
except ImportError:
    sys.exit("requests not installed — run:  pip install requests")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="NuChef connection soak test (R&V 2)")
    p.add_argument("--url",          default="http://localhost:8000",
                   help="Base URL of the FastAPI backend")
    p.add_argument("--duration-min", type=float, default=30.0,
                   help="Test duration in minutes (default: 30)")
    p.add_argument("--poll-s",       type=float, default=1.0,
                   help="Poll interval in seconds (default: 1)")
    p.add_argument("--out",          default="results/connection_soak.csv",
                   help="Output CSV path (default: results/connection_soak.csv)")
    p.add_argument("--use-rv-state", action="store_true",
                   help="Poll /rv_state instead of /state (richer connection info)")
    return p.parse_args()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    args = parse_args()

    out_path     = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path = out_path.with_name(out_path.stem + "_summary.txt")

    endpoint    = "/rv_state" if args.use_rv_state else "/state"
    duration_s  = args.duration_min * 60.0
    poll_s      = args.poll_s
    total_polls = int(duration_s / poll_s)

    print(f"NuChef Connection Soak Test")
    print(f"  Backend  : {args.url}")
    print(f"  Endpoint : {endpoint}")
    print(f"  Duration : {args.duration_min} min  ({duration_s:.0f} s)")
    print(f"  Poll     : every {poll_s} s  ({total_polls} polls expected)")
    print(f"  Output   : {out_path}")
    print(f"\nPress Ctrl+C to stop early.\n")

    fieldnames = [
        "timestamp_s",
        "wall_clock",
        "backend_reachable",
        "demo_state",
        "weight_g",
        "vision_frame_count",
        "rv_event_count",
        "rv_latency_p95_ms",
        "dispense_status",
        "http_status",
        "error",
    ]

    rows: list[dict] = []
    session = requests.Session()

    disconnected_since: float | None = None
    reconnect_durations: list[float] = []
    consecutive_failures = 0

    test_start = time.monotonic()

    try:
        for poll_idx in range(total_polls):
            frame_target = test_start + poll_idx * poll_s
            now_mono     = time.monotonic()
            wall         = time.strftime("%H:%M:%S")

            reachable    = False
            demo_state   = ""
            weight_g     = ""
            frame_count  = ""
            rv_evt_count = ""
            rv_p95       = ""
            disp_status  = ""
            http_status  = ""
            error_msg    = ""

            try:
                resp = session.get(f"{args.url}{endpoint}", timeout=3.0)
                http_status = str(resp.status_code)
                if resp.ok:
                    data         = resp.json()
                    reachable    = True
                    demo_state   = data.get("demo_state", "")
                    weight_g     = data.get("weight", "")
                    frame_count  = data.get("vision_frame_count", "")
                    rv_evt_count = data.get("rv_event_count", "")
                    rv_p95       = data.get("rv_latency_p95_ms", "")
                    disp_status  = data.get("dispense_status", "")
                else:
                    error_msg = f"HTTP {resp.status_code}"
            except Exception as exc:
                error_msg   = str(exc)
                http_status = "timeout/error"

            if reachable:
                if disconnected_since is not None:
                    gap = now_mono - disconnected_since
                    reconnect_durations.append(gap)
                    print(f"  [{wall}] RECONNECTED after {gap:.1f} s")
                    disconnected_since = None
                consecutive_failures = 0
            else:
                consecutive_failures += 1
                if disconnected_since is None:
                    disconnected_since = now_mono
                    print(f"  [{wall}] DISCONNECT detected (consecutive fail: {consecutive_failures})")

            rows.append({
                "timestamp_s":        round(now_mono - test_start, 1),
                "wall_clock":         wall,
                "backend_reachable":  reachable,
                "demo_state":         demo_state,
                "weight_g":           weight_g,
                "vision_frame_count": frame_count,
                "rv_event_count":     rv_evt_count,
                "rv_latency_p95_ms":  rv_p95,
                "dispense_status":    disp_status,
                "http_status":        http_status,
                "error":              error_msg,
            })

            if poll_idx % 60 == 0:
                elapsed_min = (now_mono - test_start) / 60.0
                print(f"  [{wall}] {elapsed_min:.1f} min elapsed  "
                      f"state={demo_state}  weight={weight_g}  "
                      f"reachable={reachable}")

            # Sleep to maintain target rate
            sleep_s = frame_target - time.monotonic()
            if sleep_s > 0:
                time.sleep(sleep_s)

    except KeyboardInterrupt:
        print("\nStopped early by user.")

    total_elapsed = time.monotonic() - test_start
    total_polls_done = len(rows)

    # Write CSV
    with open(out_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    # Stats
    reachable_polls    = sum(1 for r in rows if r["backend_reachable"])
    unreachable_polls  = total_polls_done - reachable_polls
    max_reconnect_s    = max(reconnect_durations) if reconnect_durations else 0.0
    num_disconnects    = len(reconnect_durations)
    passed             = max_reconnect_s <= 5.0
    verdict            = "PASS" if passed else "FAIL"

    summary_lines = [
        "=" * 56,
        "NuChef Connection Soak Test — SUMMARY",
        "=" * 56,
        f"Backend          : {args.url}",
        f"Duration planned : {args.duration_min} min",
        f"Actual elapsed   : {total_elapsed / 60:.1f} min",
        f"Total polls done : {total_polls_done}",
        "",
        f"Reachable polls  : {reachable_polls} ({100*reachable_polls/max(1,total_polls_done):.1f}%)",
        f"Unreachable polls: {unreachable_polls}",
        f"Disconnect events: {num_disconnects}",
        f"Max reconnect    : {max_reconnect_s:.1f} s   (threshold <= 5 s)",
    ]
    if reconnect_durations:
        summary_lines.append(
            "Reconnect times  : " + "  ".join(f"{d:.1f}s" for d in reconnect_durations)
        )
    summary_lines += [
        "",
        f"PASS criteria    : max reconnect <= 5 s",
        f"RESULT           : {verdict}",
        "=" * 56,
    ]
    summary_text = "\n".join(summary_lines)
    print("\n" + summary_text)
    summary_path.write_text(summary_text)
    print(f"\nCSV saved to     : {out_path}")
    print(f"Summary saved to : {summary_path}")

    if not passed:
        sys.exit(1)


if __name__ == "__main__":
    main()
