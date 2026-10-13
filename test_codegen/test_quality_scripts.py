import importlib.util
from pathlib import Path
import tempfile
import unittest
import subprocess
import tarfile
import shutil

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("diagnostics", ROOT / "scripts/check-diagnostics.py")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class DiagnosticsTest(unittest.TestCase):
    def test_locations_move_but_new_warning_and_count_increase_remain_visible(self):
        with tempfile.TemporaryDirectory() as root:
            log = Path(root) / "build.log"
            log.write_text("/tmp/base/comm/x.c:1:2: warning: old [-Wunused]\n")
            before = checker.diagnostics(log)
            log.write_text("/tmp/current/comm/x.c:9:4: warning: old [-Wunused]\n")
            self.assertEqual(before, checker.diagnostics(log))
            log.write_text(log.read_text() * 2 + "/tmp/current/x.c:3:1: warning: new [-Wextra]\n")
            self.assertEqual(sum((checker.diagnostics(log) - before).values()), 2)

    def test_msvc_diagnostics_ignore_build_locations(self):
        with tempfile.TemporaryDirectory() as root:
            log = Path(root) / "build.log"
            log.write_text("C:\\a\\old\\x.c(12,3): warning C4101: unused variable [C:\\a\\old\\ent.vcxproj]\n")
            before = checker.diagnostics(log)
            log.write_text("C:\\a\\new\\x.c(22,3): warning C4101: unused variable [C:\\a\\new\\ent.vcxproj]\n")
            self.assertEqual(sum(before.values()), 1)
            self.assertEqual(before, checker.diagnostics(log))

    @unittest.skipUnless(shutil.which("bash"), "Linux SDK validator requires bash")
    def test_sdk_missing_metadata_fails_without_trying_a_consumer(self):
        with tempfile.TemporaryDirectory() as root:
            archive = Path(root) / "empty.tar.gz"
            with tarfile.open(archive, "w:gz"):
                pass
            outcome = subprocess.run(["bash", str(ROOT / "scripts/validate-release-sdk.sh"),
                                      str(archive), "missing-consumer"], capture_output=True, text=True)
            self.assertNotEqual(outcome.returncode, 0)
            self.assertIn("no exported CMake metadata", outcome.stderr)


if __name__ == "__main__":
    unittest.main()
