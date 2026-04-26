#!/usr/bin/env python3
"""
NuChef — Latency Burst Test  (R&V requirement 1)
=================================================
Sends events to POST /rv_event at a configured rate for a configured duration
and measures end-to-end latency from sender to backend.

Usage:
    python tools/latency_burst_test.py --url http://192.168.4.28:8000
    python tools/latency_burst_test.py --url http://localhost:8000 --rate-hz 10 --duration-s 60

Acceptance criteria (R&V requirement):
    p95 latency <= 150 ms  AND  dropped (missed responses) == 0

Outputs (in results/ by default):
    latency_burst.csv          — one row per event
    latency_burst_summary.txt  — human-readable PASS/FAIL summary
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import statistics
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
    p = argparse.ArgumentParser(description="NuChef latency burst test (R&V 1)")
    p.add_argument("--url",         default="http://localhost:8000",
                   help="Base URL of the FastAPI backend")
    p.add_argument("--rate-hz",     type=float, default=10.0,
                   help="Events per second to send (default: 10)")
    p.add_argument("--duration-s",  type=float, default=60.0,
                   help="Test duration in seconds (default: 60)")
    p.add_argument("--out",         default="results/latency_burst.csv",
                   help="Output CSV path (default: results/latency_burst.csv)")
    p.add_argument("--event-type",  default="scripted_burst",
                   help="event_type field value (default: scripted_burst)")
    return p.parse_args()


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def percentile(sorted_values: list[float], pct: float) -> float:
    """Return the p-th percentile from a sorted list."""
    if not sorted_values:
        return float("nan")
    idx = min(int(len(sorted_values) * pct / 100), len(sorted_values) - 1)
    return sorted_values[idx]


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    args = parse_args()

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path = out_path.with_name(out_path.stem + "_summary.txt")

    interval_s    = 1.0 / args.rate_hz
    expected_count = int(args.rate_hz * args.duration_s)

    print(f"NuChef Latency Burst Test")
    print(f"  Backend : {args.url}")
    print(f"  Rate    : {args.rate_hz} Hz")
    print(f"  Duration: {args.duration_s} s")
    print(f"  Expected: {expected_count} events")
    print(f"  Output  : {out_path}")
    print()

    rows: list[dict] = []
    missed = 0
    session = requests.Session()

    test_start = time.monotonic()
    for seq in range(expected_count):
        event_id      = f"quest_evt_{seq:06d}"
        quest_send_ms = time.time() * 1000.0
        frame_target  = test_start + seq * interval_s

        payload = {
            "event_id":      event_id,
            "event_type":    args.event_type,
            "quest_send_ms": quest_send_ms,
        }

        response_latency_ms: float | None = None
        reported_latency_ms: float | None = None
        status = "error"

        try:
            resp = session.post(f"{args.url}/rv_event", json=payload, timeout=2.0)
            response_latency_ms = (time.time() * 1000.0) - quest_send_ms
            if resp.ok:
                data = resp.json()
                reported_latency_ms = data.get("latency_ms")
                status = "ok"
            else:
                status = f"http_{resp.status_code}"
                missed += 1
        except Exception as exc:
            response_latency_ms = (time.time() * 1000.0) - quest_send_ms
            status = f"exception: {type(exc).__name__}"
            missed += 1

        rows.append({
            "seq":                   seq,
            "event_id":              event_id,
            "quest_send_ms":         round(quest_send_ms, 3),
            "response_latency_ms":   round(response_latency_ms, 2) if response_latency_ms else "",
            "reported_latency_ms":   round(reported_latency_ms, 2) if reported_latency_ms else "",
            "status":                status,
        })

        if seq % 50 == 0:
            print(f"  [{seq:4d}/{expected_count}] last latency: "
                  f"{response_latency_ms:.1f} ms  status: {status}")

        # Sleep to maintain target rate
        sleep_s = frame_target - time.monotonic()
        if sleep_s > 0:
            time.sleep(sleep_s)

    total_elapsed = time.monotonic() - test_start

    # Write CSV
    with open(out_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    # Compute stats on successful rows
    ok_rows = [r for r in rows if r["status"] == "ok"]
    received = len(ok_rows)
    dropped  = expected_count - received

    reported_lats = sorted(
        float(r["reported_latency_ms"]) for r in ok_rows if r["reported_latency_ms"] != ""
    )

    p50  = percentile(reported_lats, 50)
    p95  = percentile(reported_lats, 95)
    pmax = reported_lats[-1] if reported_lats else float("nan")

    passed = (p95 <= 150.0) and (dropped == 0)
    verdict = "PASS" if passed else "FAIL"

    summary_lines = [
        "=" * 56,
        "NuChef Latency Burst Test — SUMMARY",
        "=" * 56,
        f"Backend         : {args.url}",
        f"Rate            : {args.rate_hz} Hz  for  {args.duration_s} s",
        f"Actual elapsed  : {total_elapsed:.1f} s",
        "",
        f"Expected events : {expected_count}",
        f"Received OK     : {received}",
        f"Dropped / error : {dropped}",
        "",
        f"p50 latency     : {p50:.1f} ms",
        f"p95 latency     : {p95:.1f} ms   (threshold <= 150 ms)",
        f"Max latency     : {pmax:.1f} ms",
        "",
        f"PASS criteria   : p95 <= 150 ms  AND  dropped == 0",
        f"RESULT          : {verdict}",
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
