"""Analytic support controls, unresolved gaps, and real sliced substrates."""
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXE = str(Path(sys.argv.pop(1)).resolve())
FIXTURES = Path(__file__).resolve().parent / "fixtures"


def support_strips(z=2, width=1, missing=False, hole=False):
    height = 0.1
    area = height * (width - height * (1 - math.pi / 4))
    filament = math.pi * 1.75**2 / 4
    lines = ["; Analytic geometry control, not a manufacturing job", "G21", "G90", "M83", "G28",
             ";TYPE:Solid infill", f";WIDTH:{width}", f";HEIGHT:{height}"]
    for y in (-4, -2, 0, 2, 4):
        if missing and y == 0:
            continue
        spans = [(88, 99.98), (100.02, 112)] if hole and y == 0 else [(88, 112)]
        for a, b in spans:
            lines += [f"G1 X{a} Y{y+100} Z{z} F600", f"G1 X{b} E{area*(b-a)/filament:.12f}"]
    return ("\n".join(lines) + "\n").encode()


class FitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fit = (FIXTURES / "substrate-fit.cfg").read_text()
        cls.coupon = (FIXTURES / "supported-coupon.cfg").read_text().replace("surface 0.005 0.0025", "surface 0 0")

    def invoke(self, data=None, coupon=None, fit=None, existing=False, map_output=True):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            (folder / "part.gcode").write_bytes(support_strips() if data is None else data)
            (folder / "coupon").write_text(self.coupon if coupon is None else coupon)
            (folder / "fit").write_text(self.fit if fit is None else fit)
            output = folder / "map.csv"
            if existing:
                output.write_text("keep this")
            args = [EXE, str(FIXTURES / "virtual-bed.machine"), str(folder / "coupon"),
                    str(folder / "fit"), str(folder / "part.gcode")]
            if map_output:
                args += ["--map", str(output)]
            result = subprocess.run(args, capture_output=True, text=True, timeout=30)
            return result, output.read_text() if output.exists() else None

    def test_supported_flat_interface(self):
        result, data = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertTrue(report["fit_passed"])
        self.assertFalse(report["printer_safe"])
        self.assertFalse(report["registration_calibrated"])
        self.assertEqual(report["first_layer_regions"], 5)
        self.assertAlmostEqual(report["minimum_gap_bound_mm"], 0.195)
        self.assertAlmostEqual(report["maximum_gap_bound_mm"], 0.205)
        self.assertIn("status", data.splitlines()[0])

    def test_rounded_bead_top_is_not_a_flat_full_width_slab(self):
        fit = self.fit.replace("width_uncertainty 0.005", "width_uncertainty 0").replace(
            "z_uncertainty 0.005", "z_uncertainty 0").replace("e_rounding 0.00001", "e_rounding 0.000000000001")
        result, _ = self.invoke(support_strips(width=0.45), fit=fit)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        expected = 0.2 + 0.05 - math.sqrt(0.05**2 - 0.025**2)
        self.assertAlmostEqual(report["maximum_gap_bound_mm"], expected, places=7)

    def test_high_and_low_support_reject(self):
        for z in (1.8, 2.1):
            with self.subTest(z=z):
                result, _ = self.invoke(support_strips(z=z))
                self.assertEqual(result.returncode, 1, result.stderr)
                report = json.loads(result.stdout)
                self.assertFalse(report["fit_passed"])
                self.assertGreater(report["failed_cells"], 0)

    def test_missing_support_is_not_hidden_by_other_tracks(self):
        result, _ = self.invoke(support_strips(missing=True))
        self.assertEqual(result.returncode, 1, result.stderr)
        report = json.loads(result.stdout)
        self.assertGreater(report["failed_cells"], 0)
        self.assertIsNone(report["maximum_gap_bound_mm"])

    def test_small_endpoint_gap_remains_uncertain(self):
        # Possible endcaps can bound the top from above but do not establish
        # guaranteed support across a deliberate 0.04 mm interruption.
        result, _ = self.invoke(support_strips(hole=True))
        self.assertEqual(result.returncode, 1, result.stderr)
        report = json.loads(result.stdout)
        self.assertGreater(report["uncertain_cells"], 0)
        self.assertFalse(report["fit_passed"])

    def test_bad_metadata_bridge_and_dose_reject_without_map(self):
        source = support_strips()
        for data in (source.replace(b";WIDTH:1\n", b""), source.replace(b";HEIGHT:0.1", b";HEIGHT:nan"),
                     source.replace(b";TYPE:Solid infill", b";TYPE:Bridge infill"),
                     source.replace(b";WIDTH:1\n", b";WIDTH:0.3\n")):
            with self.subTest(data=data[:100]):
                result, output = self.invoke(data)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, "")
                self.assertIsNone(output)

    def test_wrong_registration_rejects(self):
        result, _ = self.invoke(fit=self.fit.replace("translation 100 100 0", "translation 0 0 0"))
        self.assertEqual(result.returncode, 1)
        self.assertFalse(json.loads(result.stdout)["fit_passed"])

    def test_work_budget_and_existing_output(self):
        result, output = self.invoke(fit=self.fit.replace("work_budget 20000000", "work_budget 1"))
        self.assertEqual(result.returncode, 2)
        self.assertIsNone(output)
        result, output = self.invoke(existing=True)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(output, "keep this")

    def test_real_slicer_cases(self):
        cases = json.loads((FIXTURES / "substrate-fit-cases.json").read_text())
        for case in cases:
            with self.subTest(case=case["name"]):
                source = (FIXTURES / case["gcode"]).read_bytes()
                self.assertEqual(hashlib.sha256(source).hexdigest(), case["sha256"])
                result, _ = self.invoke(source, coupon=(FIXTURES / case["coupon"]).read_text(), map_output=False)
                self.assertEqual(result.returncode, 0 if case["fit_passed"] else 1, result.stderr)
                report = json.loads(result.stdout)
                self.assertEqual(report["fit_passed"], case["fit_passed"])
                if case["fit_passed"]:
                    self.assertGreaterEqual(report["minimum_gap_bound_mm"], 0.15)
                    self.assertLessEqual(report["maximum_gap_bound_mm"], 0.25)
                else:
                    self.assertGreater(report["failed_cells"], 0)


if __name__ == "__main__":
    unittest.main()
