#!/usr/bin/env python3
"""Regression checks for paired timer measurements using deterministic fake binaries.

The tests exercise collector semantics, not real timer accuracy or latency.
"""
import csv
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from timer_baseline import OPTIONAL, REQUIRED
from compare_timer_backends import check_cpu_quota_capacity


SCRIPT = Path(__file__).with_name("compare_timer_backends.py")


class PairedTimerCollectorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def fake_binary(self, name, p99, lag, omit_deadline=False):
        path = self.root / name
        fields = {field: "1" for field in REQUIRED if field not in (
            "mode", "timers", "period", "duration_ms"
        )}
        fields.update({
            "hits": "10", "intervals": "9", "p99_us": str(p99),
        })
        deadline = {field: str(lag) for field in OPTIONAL}
        code = (
            "#!/usr/bin/env python3\n"
            "import sys\n"
            "mode, timers, period, duration = sys.argv[1:]\n"
            f"metrics = {fields!r}\n"
            f"lag_metrics = {deadline!r}\n"
            "metrics.update(mode=mode, timers=timers, period=period, duration_ms=duration)\n"
            + ("" if omit_deadline else "if mode == 'ms': metrics.update(lag_metrics)\n")
            + "print('timer_scale ' + ' '.join(f'{key}={value}' for key, value in metrics.items()))\n"
        )
        path.write_text(code, encoding="utf-8")
        path.chmod(0o755)
        return path

    def collect(self, legacy, timerfd, *extra):
        return subprocess.run(
            [sys.executable, str(SCRIPT), "--legacy-bin", str(legacy),
             "--prototype-bin", str(timerfd), "--output-dir", str(self.root / "out"),
             "--trials", "2", "--duration-ms", "250", *extra],
            text=True, capture_output=True, timeout=30, check=False,
        )

    def test_deadline_p99_pairs_and_uninstrumented_us_mode(self):
        legacy = self.fake_binary("legacy", 110, 130)
        timerfd = self.fake_binary("timerfd", 70, 80)
        result = self.collect(legacy, timerfd, "--scenario", "ms:1:20",
                              "--scenario", "us:1:1000")
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads((self.root / "out" / "summary.json").read_text())
        ms = report["summaries"]["ms:1:20"]
        self.assertEqual(ms["paired_p99"]["timerfd_better_runs"], 2)
        self.assertEqual(ms["paired_deadline_lag_p99"]["timerfd_better_runs"], 2)
        self.assertEqual(ms["paired_deadline_lag_p99"]["total_pairs"], 2)
        self.assertEqual(ms["legacy"]["deadline_lag_p99_us"]["median"], 130)
        self.assertNotIn("paired_deadline_lag_p99", report["summaries"]["us:1:1000"])
        self.assertEqual(report["metadata"]["benchmark_affinity_mode"],
                         "inherited_full_allowed")
        with (self.root / "out" / "paired_samples.csv").open(newline="") as source:
            rows = list(csv.DictReader(source))
        self.assertEqual(len(rows), 8)
        self.assertEqual({r["position"] for r in rows}, {"1", "2"})

    def test_missing_legacy_deadline_is_rejected(self):
        legacy = self.fake_binary("legacy", 110, 130, omit_deadline=True)
        timerfd = self.fake_binary("timerfd", 70, 80)
        result = self.collect(legacy, timerfd, "--scenario", "ms:1:20")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing deadline_lag_", result.stderr)
        self.assertTrue((self.root / "out" / "error.txt").exists())
        self.assertFalse((self.root / "out" / "summary.json").exists())

    def test_cgroup_quota_capacity_is_fail_closed(self):
        # V2 cgroup: max permits the full requested CPU count. A finite
        # quota must cover all four cores for this acceptance experiment.
        self.assertIsNone(check_cpu_quota_capacity("max 100000", 4))
        self.assertEqual(check_cpu_quota_capacity("400000 100000", 4), 4)
        self.assertEqual(check_cpu_quota_capacity("80000 20000", 4), 4)
        with self.assertRaisesRegex(ValueError, "only 3.000 cores"):
            check_cpu_quota_capacity("300000 100000", 4)
        for value in (None, "bad", "max", "nan 100000", "400000 0", "0 100000"):
            with self.subTest(cpu_max=value):
                with self.assertRaises(ValueError):
                    check_cpu_quota_capacity(value, 4)

    def test_cgroup_quota_capacity_needs_explicit_affinity_gate(self):
        legacy = self.fake_binary("legacy", 110, 130)
        timerfd = self.fake_binary("timerfd", 70, 80)
        result = self.collect(
            legacy, timerfd, "--scenario", "ms:1:20",
            "--require-cpu-quota-capacity",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires --require-unrestricted-cpus N", result.stderr)

    @unittest.skipUnless(hasattr(os, "sched_getaffinity"),
                         "Linux CPU affinity API required")
    def test_unrestricted_affinity_contract(self):
        allowed = len(os.sched_getaffinity(0))
        if not 1 <= allowed <= 4:
            self.skipTest("affinity contract accepts 1..4 allowed CPUs")
        legacy = self.fake_binary("legacy", 110, 130)
        timerfd = self.fake_binary("timerfd", 70, 80)
        result = self.collect(legacy, timerfd, "--scenario", "ms:1:20",
                              "--require-unrestricted-cpus", str(allowed))
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads((self.root / "out" / "summary.json").read_text())
        self.assertEqual(report["metadata"]["unrestricted_cpu_count_required"], allowed)
        self.assertEqual(len(report["metadata"]["benchmark_affinity_cpus"]), allowed)


if __name__ == "__main__":
    unittest.main()
