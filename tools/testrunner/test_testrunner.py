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
cases = 1
if sys.argv[1] in ("split", "exact"):
    case = "TESTRUN CASE t.long FAIL 7 " + "x" * 300 + " tail"
    if sys.argv[1] == "split":
        lines += [case[:256], case[256:]]
    else:
        lines.append(case[:256])
    cases = 2
lines.append(f"TESTRUN END failures=0 cases={cases}")
for line in lines:
    print("stdout> " + line, flush=True)
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

    def test_split_case_line_is_joined(self):
        result = self.run_fake("split")
        long_case = result["cases"][1]
        self.assertEqual(long_case["name"], "t.long")
        self.assertEqual(long_case["detail"], "x" * 300 + " tail")
        self.assertEqual(len(result["cases"]), 2)
        self.assertNotIn("malformed", " ".join(result["info"]))

    def test_case_line_at_the_limit_keeps_the_next_testrun_line(self):
        result = self.run_fake("exact")
        self.assertEqual(len(result["cases"][1]["detail"]), 256 - len("TESTRUN CASE t.long FAIL 7 "))
        self.assertEqual(result["end"], "failures=0 cases=2")


if __name__ == "__main__":
    unittest.main()
