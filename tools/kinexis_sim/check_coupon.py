"""Independent geometry, deposition and failure tests for the height-map coupon."""
import csv
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXE = str(Path(sys.argv.pop(1)).resolve())
FIXTURES = Path(__file__).resolve().parent / "fixtures"


def rotation(sx, sy):
    n = math.sqrt(1 + sx * sx + sy * sy)
    k = n * (n + 1)
    return ((1 - sx * sx / k, -sx * sy / k, -sx / n),
            (-sx * sy / k, 1 - sy * sy / k, -sy / n),
            (sx / n, sy / n, 1 / n))


def apply(matrix, vector):
    return [sum(a * b for a, b in zip(row, vector)) for row in matrix]


def blend(a, b, t):
    return [x + t * (y - x) for x, y in zip(a, b)]


class CouponTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = (FIXTURES / "heightmap-coupon.cfg").read_text()
        cls.machine = (FIXTURES / "virtual-bed.machine").read_text()

    def run_coupon(self, config=None, machine=None, existing=False):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            (folder / "config").write_text(config or self.config)
            (folder / "machine").write_text(machine or self.machine)
            output = folder / "path.csv"
            if existing:
                output.write_text("keep this")
            result = subprocess.run([EXE, str(folder / "machine"), str(folder / "config"),
                                     "--csv", str(output)], capture_output=True, text=True, timeout=30)
            data = output.read_text() if output.exists() else None
            return result, data

    def default(self):
        result, text = self.run_coupon()
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        rows = list(csv.DictReader(text.splitlines()))
        for row in rows:
            for key in row.keys() - {"kind"}:
                row[key] = float(row[key])
                self.assertTrue(math.isfinite(row[key]))
        return report, rows

    def test_continuous_axis_interpolation_bounds(self):
        report, rows = self.default()
        self.assertEqual(report["samples"], len(rows))
        self.assertFalse(report["collision_checked"])
        self.assertFalse(report["printer_safe"])
        self.assertLessEqual(report["position_error_bound_mm"], 0.01)
        self.assertGreaterEqual(report["travel_clearance_lower_bound_mm"], 2)
        for a, b in zip(rows, rows[1:]):
            dt = b["time"] - a["time"]
            self.assertGreater(dt, 0)
            for key, speed in (("world_x", 100), ("world_y", 100), ("world_z", 30),
                               ("axis_U", 10), ("axis_V", 10), ("axis_W", 10), ("e", 25)):
                self.assertLessEqual(abs(b[key] - a[key]) / dt, speed + 1e-8)
            depositing = b["kind"] == "extrusion"
            clear = a["z"] == 20 and b["z"] == 20
            for t in (0.25, 0.5, 0.75):
                local = blend([a[k] for k in ("x", "y", "z")],
                              [b[k] for k in ("x", "y", "z")], t)
                if depositing:
                    layer = a["z"] - 0.005 * a["x"] ** 2 - 0.0025 * a["y"] ** 2
                    local[2] = layer + 0.005 * local[0] ** 2 + 0.0025 * local[1] ** 2
                axes = blend([a[k] for k in ("axis_U", "axis_V", "axis_W")],
                             [b[k] for k in ("axis_U", "axis_V", "axis_W")], t)
                h0, h1, h2 = [value - 100 for value in axes]
                sx = (h1 - h0) / 200
                sy = (h2 - (h0 + h1) / 2) / 200
                lift = (h0 + h1) / 2 + 100 * sy
                r = rotation(sx, sy)
                desired = apply(r, local)
                desired[2] += lift
                actual = blend([a[k] for k in ("world_x", "world_y", "world_z")],
                               [b[k] for k in ("world_x", "world_y", "world_z")], t)
                error = math.dist(actual, desired)
                self.assertLessEqual(error, b["position_bound"] + 1e-9)
                # Invert the independently reconstructed machine rotation.
                shifted = actual[:]
                shifted[2] -= lift
                actual_local = apply(list(zip(*r)), shifted)
                if depositing:
                    normal = [-0.01 * actual_local[0], -0.005 * actual_local[1], 1]
                    world_normal = apply(r, normal)
                    angle = math.degrees(math.atan2(math.hypot(*world_normal[:2]), world_normal[2]))
                    self.assertLessEqual(angle, report["alignment_error_bound_deg"] + 1e-8)
                if clear:
                    gap = actual_local[2] - 0.005 * actual_local[0] ** 2 - 0.0025 * actual_local[1] ** 2 - 0.4
                    self.assertGreaterEqual(gap, report["travel_clearance_lower_bound_mm"] - 1e-8)

    def test_volume_against_independent_simpson_integral(self):
        report, rows = self.default()
        total = 0
        steps = 4096
        dx = 20 / steps
        for y in (-4, -2, 0, 2, 4):
            def density(x):
                return 0.08 * math.sqrt(1 + (0.01 * x) ** 2) / math.sqrt(1 + (0.01 * x) ** 2 + (0.005 * y) ** 2)
            integral = density(-10) + density(10)
            integral += sum((4 if i % 2 else 2) * density(-10 + dx * i) for i in range(1, steps))
            total += 2 * integral * dx / 3  # two layers
        self.assertLessEqual(abs(report["volume_mm3"] - total), report["volume_error_bound_mm3"] + 1e-10)
        self.assertLessEqual(report["volume_error_bound_mm3"], 200 * 0.00005 + 1e-10)
        area = math.pi * 1.75 ** 2 / 4
        self.assertAlmostEqual(report["deposited_filament_mm"], report["volume_mm3"] / area)
        self.assertAlmostEqual(rows[-1]["e"] + 0.8, report["deposited_filament_mm"])
        self.assertEqual((rows[0]["lift"], rows[0]["pitch"], rows[0]["roll"]), (0, 0, 0))
        self.assertEqual((rows[-1]["lift"], rows[-1]["pitch"], rows[-1]["roll"]), (0, 0, 0))
        for a, b in zip(rows, rows[1:]):
            if abs(a["pitch"] - b["pitch"]) + abs(a["roll"] - b["roll"]) > 1e-8 and b["kind"] != "extrusion":
                self.assertEqual((a["z"], b["z"]), (20, 20))

    def test_flat_surface_conserves_planar_volume(self):
        result, _ = self.run_coupon(self.config.replace("surface 0.005 0.0025", "surface 0 0"))
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertAlmostEqual(report["volume_mm3"], 16)
        self.assertEqual(report["volume_error_bound_mm3"], 0)
        self.assertEqual(report["alignment_error_bound_deg"], 0)

    def test_invalid_config_fails_without_output(self):
        for original, replacement in (("surface 0.005 0.0025", "surface -0.1 0"),
                                      ("surface 0.005 0.0025", "surface 1 1"),
                                      ("clearance_z 20", "clearance_z 1"),
                                      ("point_budget 100000", "point_budget 10"),
                                      ("position_tolerance 0.01", "position_tolerance 0"),
                                      ("rows 5", "rows 1"),
                                      ("filament_diameter 1.75", "filament_diameter 0")):
            with self.subTest(replacement=replacement):
                result, data = self.run_coupon(self.config.replace(original, replacement))
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "")
                self.assertIsNone(data)

    def test_machine_limit_failure_is_not_clamped(self):
        machine = self.machine.replace("tilt_limit 15 15", "tilt_limit 1 1")
        result, data = self.run_coupon(machine=machine)
        self.assertNotEqual(result.returncode, 0)
        self.assertIsNone(data)
        self.assertEqual(result.stdout, "")

    def test_existing_output_is_preserved(self):
        result, data = self.run_coupon(existing=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(data, "keep this")

    def test_report_uses_declared_actuator_names(self):
        result, data = self.run_coupon(machine=self.machine.replace("actuator U ", "actuator left_1 "))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("axis_left_1", data.splitlines()[0])


if __name__ == "__main__":
    unittest.main()
