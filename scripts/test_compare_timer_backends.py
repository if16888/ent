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
from review_timerfd_fourcpu import audit_pair


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


class FourCpuEvidenceAuditTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.sha = "0123456789abcdef0123456789abcdef01234567"
        self.idle = self.make_condition("idle", duty=0)
        self.loaded = self.make_condition("loaded25", duty=25)

    def make_condition(self, name, duty):
        from timer_baseline import DEFAULT_SCENARIOS
        from review_timerfd_fourcpu import SCENARIOS
        root = self.root / name
        root.mkdir()
        metadata = {
            "git_sha": self.sha,
            "benchmark_affinity_mode": "inherited_full_allowed",
            "unrestricted_cpu_count_required": 4,
            "cpu_quota_capacity_required": True,
            "cpu_cgroup_max": "400000 100000",
            "cpu_quota_capacity_cores": 4,
            "cpu_affinity_allowed": [0, 1, 2, 3],
            "benchmark_affinity_cpus": [0, 1, 2, 3],
            "duration_ms": 1500,
            "trials_per_case_per_backend": 20,
            "scenarios": SCENARIOS,
            "background_load": {
                "duty_percent_of_one_core": duty,
                "cpu": 0 if duty else None,
            },
        }
        self.assertEqual(len(DEFAULT_SCENARIOS), 5)
        rows = []
        summaries = {}
        for scenario in SCENARIOS:
            mode, count, period = scenario.split(":")
            summary = {}
            for backend in ("legacy", "timerfd"):
                p99 = 120 if backend == "legacy" else 80
                lag = 140 if backend == "legacy" else 90
                for trial in range(1, 21):
                    position = (1 if backend == "legacy" else 2) if trial % 2 else (
                        2 if backend == "legacy" else 1)
                    rows.append({
                        "scenario": scenario, "trial": trial, "backend": backend,
                        "position": position, "mode": mode, "timers": count,
                        "period": period, "duration_ms": 1500, "p99_us": p99,
                        "deadline_lag_p99_us": lag if mode == "ms" else "",
                    })
                summary[backend] = {"p99_us": {"median": p99, "max": p99}}
                if mode == "ms":
                    summary[backend]["deadline_lag_p99_us"] = {
                        "median": lag, "max": lag}
            summary["paired_p99"] = {
                "timerfd_better_runs": 20, "total_pairs": 20}
            if mode == "ms":
                summary["paired_deadline_lag_p99"] = {
                    "timerfd_better_runs": 20, "total_pairs": 20}
            summaries[scenario] = summary
        (root / "summary.json").write_text(
            json.dumps({"metadata": metadata, "summaries": summaries}))
        with (root / "paired_samples.csv").open("w", newline="") as fh:
            writer = csv.DictWriter(fh, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        return root

    def rewrite_report(self, path, mutate):
        document = json.loads(path.read_text())
        mutate(document)
        path.write_text(json.dumps(document))

    def rewrite_rows(self, path, mutate):
        with path.open(newline="") as fh:
            rows = list(csv.DictReader(fh))
        mutate(rows)
        with path.open("w", newline="") as fh:
            writer = csv.DictWriter(fh, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)

    def test_valid_evidence_is_not_production_approval(self):
        result = audit_pair(self.idle, self.loaded, self.sha)
        self.assertEqual(result["evidence_integrity"], "PASS")
        self.assertEqual(result["total_independent_runs"], 400)
        self.assertEqual(result["production_migration"], "NO-GO")
        self.assertEqual(result["idle"]["scenarios"]["ms:64:20"]
                         ["paired_deadline_lag_p99"]["timerfd_better_runs"], 20)

    def test_wrong_sha_is_rejected(self):
        self.rewrite_report(self.loaded / "summary.json",
                            lambda d: d["metadata"].update(git_sha="wrong"))
        with self.assertRaisesRegex(ValueError, "git_sha"):
            audit_pair(self.idle, self.loaded, self.sha)

    def test_incomplete_raw_trials_are_rejected(self):
        self.rewrite_rows(self.idle / "paired_samples.csv",
                          lambda rows: rows.pop())
        with self.assertRaisesRegex(ValueError, "wrong raw row count"):
            audit_pair(self.idle, self.loaded, self.sha)

    def test_duplicate_pair_even_with_right_count_is_rejected(self):
        def replace_pair(rows):
            rows[1] = dict(rows[0])
        self.rewrite_rows(self.idle / "paired_samples.csv", replace_pair)
        with self.assertRaisesRegex(ValueError, "duplicate"):
            audit_pair(self.idle, self.loaded, self.sha)

    def test_order_reversal_is_rejected(self):
        self.rewrite_rows(self.loaded / "paired_samples.csv",
                          lambda rows: rows[0].update(position="2"))
        with self.assertRaisesRegex(ValueError, "alternation"):
            audit_pair(self.idle, self.loaded, self.sha)

    def test_missing_deadline_sample_is_rejected(self):
        self.rewrite_rows(self.loaded / "paired_samples.csv",
                          lambda rows: rows[0].update(deadline_lag_p99_us=""))
        with self.assertRaisesRegex(ValueError, "missing/non-numeric"):
            audit_pair(self.idle, self.loaded, self.sha)

    def test_inadequate_quota_is_rejected(self):
        self.rewrite_report(self.idle / "summary.json",
                            lambda d: d["metadata"].update(cpu_cgroup_max="300000 100000"))
        with self.assertRaisesRegex(ValueError, "only 3.000 cores"):
            audit_pair(self.idle, self.loaded, self.sha)

    def test_summary_does_not_override_raw_samples(self):
        self.rewrite_report(
            self.idle / "summary.json",
            lambda d: d["summaries"]["ms:64:20"]["timerfd"]["p99_us"].update(max=1))
        with self.assertRaisesRegex(ValueError, "summary=1 raw=80"):
            audit_pair(self.idle, self.loaded, self.sha)

    def test_mismatched_idle_loaded_cpu_metadata_is_rejected(self):
        self.rewrite_report(self.loaded / "summary.json", lambda d: d["metadata"].update(
            cpu_affinity_allowed=[4, 5, 6, 7],
            benchmark_affinity_cpus=[4, 5, 6, 7],
            background_load={"duty_percent_of_one_core": 25, "cpu": 4}))
        with self.assertRaisesRegex(ValueError, "affinity mismatch"):
            audit_pair(self.idle, self.loaded, self.sha)


if __name__ == "__main__":
    unittest.main()
