"""End-to-end validation, including absence of partial reports on rejection."""
import csv
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXE = str(Path(sys.argv.pop(1)).resolve())
FIXTURES = Path(__file__).resolve().parent / "fixtures"


class CliTests(unittest.TestCase):
    def run_sim(self, rows=None, config=None, *options):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            machine = folder / "machine.txt"
            machine.write_text(config if config is not None else
                               (FIXTURES / "virtual-bed.machine").read_text())
            trajectory = folder / "motion.csv"
            if rows is None:
                trajectory.write_text((FIXTURES / "motion.csv").read_text())
            else:
                with trajectory.open("w", newline="") as stream:
                    csv.writer(stream).writerows(rows)
            return subprocess.run([EXE, str(machine), str(trajectory), *options],
                                  capture_output=True, text=True, timeout=10)

    def rows(self):
        return list(csv.reader((FIXTURES / "motion.csv").read_text().splitlines()))

    def reject(self, result):
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")
        self.assertIn("Simulation rejected:", result.stderr)

    def test_valid_trajectory_report(self):
        result = self.run_sim()
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = list(csv.DictReader(io.StringIO(result.stdout)))
        self.assertEqual(len(rows), 7)
        self.assertEqual({row["kind"] for row in rows},
                         {"travel", "extrusion", "wipe", "retract", "lift", "transition"})
        self.assertAlmostEqual(float(rows[1]["U"]), 91.2511336474076)
        self.assertAlmostEqual(float(rows[1]["V"]), 108.7488663525924)
        self.assertAlmostEqual(float(rows[1]["reconstructed_pitch"]), 5)

    def test_disabled_and_zero_pose_match_original_motion(self):
        rows = self.rows()
        disabled = self.run_sim(rows, None, "--disabled")
        for row in rows[1:]:
            row[6:] = ["0", "0", "0"]
        zero = self.run_sim(rows)
        self.assertEqual(disabled.returncode, 0, disabled.stderr)
        self.assertEqual(zero.returncode, 0, zero.stderr)
        self.assertEqual(disabled.stdout, zero.stdout)
        report = list(csv.reader(io.StringIO(zero.stdout)))
        for source, output in zip(rows[1:], report[1:]):
            self.assertEqual(output[1], source[1])
            self.assertEqual(list(map(float, output[2:6])), list(map(float, source[2:6])))

    def test_late_invalid_moves_produce_no_partial_report(self):
        for kind in ("travel", "extrusion", "wipe", "retract", "lift", "transition"):
            with self.subTest(kind=kind):
                rows = self.rows()
                rows[-1][1:3] = [kind, "999"]
                self.reject(self.run_sim(rows))

    def test_invalid_numeric_fields(self):
        for column in (0, 2, 3, 4, 5, 6, 7, 8):
            for value in ("nan", "inf", "1e999", "12junk", ""):
                with self.subTest(column=column, value=value):
                    rows = self.rows()
                    rows[-1][column] = value
                    self.reject(self.run_sim(rows))

    def test_bad_csv_shape(self):
        for count in (8, 10):
            rows = self.rows()
            rows[-1] = (["1"] * count)
            self.reject(self.run_sim(rows))

    def test_invalid_machine_and_unknown_option(self):
        config = (FIXTURES / "virtual-bed.machine").read_text()
        self.reject(self.run_sim(config=config.replace("tilt_limit 15 15", "tilt_limit 90 15")))
        self.reject(self.run_sim(config=config.replace("actuator W 0 100", "actuator W 0 -100")))
        self.reject(self.run_sim(config=config + "unexpected data\n"))
        self.reject(self.run_sim(None, None, "--unknown"))


if __name__ == "__main__":
    unittest.main()
