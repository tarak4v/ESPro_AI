#!/usr/bin/env python3
"""
perf_analyzer.py — Parse ESPro AI performance logs from serial or CSV.

Usage:
    python perf_analyzer.py --serial COM10          # Live serial monitor
    python perf_analyzer.py --csv perf_log.csv      # Analyze saved CSV
"""

import argparse
import csv
import sys
from collections import defaultdict
from dataclasses import dataclass, field


@dataclass
class MetricStats:
    min_ms: float = float("inf")
    max_ms: float = 0.0
    total_ms: float = 0.0
    count: int = 0
    samples: list = field(default_factory=list)

    @property
    def avg_ms(self):
        return self.total_ms / self.count if self.count else 0

    def add(self, val):
        self.min_ms = min(self.min_ms, val)
        self.max_ms = max(self.max_ms, val)
        self.total_ms += val
        self.count += 1
        self.samples.append(val)


THRESHOLDS = {
    "frame_time":    (10, 20),
    "touch_read":    (2, 5),
    "voice_stt":     (3000, 5000),
    "voice_llm":     (2000, 4000),
    "evt_dispatch":  (1, 5),
    "imu_read":      (1, 3),
    "lvgl_flush":    (15, 30),
}


def analyze_csv(filepath):
    metrics = defaultdict(MetricStats)

    with open(filepath, "r") as f:
        reader = csv.reader(f)
        for row in reader:
            if len(row) < 6:
                continue
            _, name, min_v, max_v, avg_v, count = row
            stats = metrics[name.strip()]
            stats.add(float(avg_v))

    print(f"\n{'Metric':<20} {'Min':>8} {'Avg':>8} {'Max':>8} {'Count':>8} {'Status':<10}")
    print("=" * 70)

    for name, stats in sorted(metrics.items()):
        warn, crit = THRESHOLDS.get(name, (999999, 999999))
        status = "OK"
        if stats.avg_ms > crit:
            status = "CRITICAL"
        elif stats.avg_ms > warn:
            status = "WARNING"

        print(f"{name:<20} {stats.min_ms:>8.1f} {stats.avg_ms:>8.1f} "
              f"{stats.max_ms:>8.1f} {stats.count:>8} {status:<10}")

    print(f"\nTotal metrics: {len(metrics)}, "
          f"Total samples: {sum(s.count for s in metrics.values())}")


def analyze_serial(port):
    try:
        import serial
    except ImportError:
        print("Install pyserial: pip install pyserial")
        sys.exit(1)

    ser = serial.Serial(port, 115200, timeout=1)
    metrics = defaultdict(MetricStats)
    print(f"Monitoring {port}... (Ctrl+C to stop and show report)")

    try:
        while True:
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if not line:
                continue

            # Parse perf report lines like:
            # I (xxx) perf:   frame_time    min=  5  max= 12  avg=  7  n=200
            if "perf:" in line and "min=" in line:
                parts = line.split()
                try:
                    name_idx = parts.index("perf:") + 1
                    name = parts[name_idx]
                    vals = {}
                    for p in parts:
                        if "=" in p:
                            k, v = p.split("=")
                            vals[k] = float(v)
                    if "avg" in vals:
                        metrics[name].add(vals["avg"])
                except (ValueError, IndexError):
                    pass

            print(line)

    except KeyboardInterrupt:
        ser.close()
        print("\n\n--- Performance Summary ---")
        for name, stats in sorted(metrics.items()):
            print(f"  {name:<20} avg={stats.avg_ms:.1f}ms  "
                  f"min={stats.min_ms:.1f}  max={stats.max_ms:.1f}  n={stats.count}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="ESPro AI Performance Analyzer")
    parser.add_argument("--csv", help="CSV log file to analyze")
    parser.add_argument("--serial", help="Serial port (e.g., COM10)")
    args = parser.parse_args()

    if args.csv:
        analyze_csv(args.csv)
    elif args.serial:
        analyze_serial(args.serial)
    else:
        parser.print_help()
