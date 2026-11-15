import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import testrunner

FAKE_CLI = """
import sys, time
lines = ["TESTRUN BEGIN t 1", "TESTRUN CASE t.a PASS 5 ok"]
if sys.argv[1] == "restart":
    lines.append("TESTRUN BEGIN t 1")
if sys.argv[1] == "mono":
    lines.append("Meadow successfully started MONO")
lines.append("TESTRUN END failures=0 cases=1")
for line in lines:
    print(line, flush=True)
time.sleep(60)
"""


class Events:
    def emit(self, kind, message, **fields):
        pass


class RunTestVerdicts(unittest.TestCase):
    def run_fake(self, scenario):
        with tempfile.TemporaryDirectory() as directory:
            settings = {"timeout_s": 30, "runtime_dir": directory, "core_dir": directory,
                        "cli": [sys.executable, "-c", FAKE_CLI, scenario]}
            test = {"name": "fake", "project": os.path.join(directory, "App.csproj")}
            return testrunner.run_test(test, "jit", "", settings, directory, Events())

    def test_normal_run_passes(self):
        result = self.run_fake("normal")
        self.assertEqual((result["verdict"], result["kind"]), ("pass", None))

    def test_second_begin_is_a_restart(self):
        result = self.run_fake("restart")
        self.assertEqual((result["verdict"], result["kind"]), ("fail", "restart"))

    def test_mono_start_after_begin_is_a_restart(self):
        result = self.run_fake("mono")
        self.assertEqual((result["verdict"], result["kind"]), ("fail", "restart"))


if __name__ == "__main__":
    unittest.main()
