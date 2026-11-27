"""Substrate topology and conservative swept-collision integration tests."""
from collections import Counter
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXE = str(Path(sys.argv.pop(1)).resolve())
FIXTURES = Path(__file__).resolve().parent / "fixtures"


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


class SceneTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scene = (FIXTURES / "coupon.scene").read_text()
        cls.coupon = (FIXTURES / "supported-coupon.cfg").read_text()

    def invoke(self, scene=None, coupon=None, existing=False):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            (folder / "scene").write_text(self.scene if scene is None else scene)
            (folder / "coupon").write_text(self.coupon if coupon is None else coupon)
            output = folder / "substrate.stl"
            if existing:
                output.write_text("keep this")
            result = subprocess.run([EXE, str(FIXTURES / "virtual-bed.machine"), str(folder / "coupon"),
                                     str(folder / "scene"), "--substrate-stl", str(output)],
                                    capture_output=True, text=True, timeout=30)
            return result, output.read_text() if output.exists() else None

    def reject(self, scene=None, coupon=None, message=None):
        result, mesh = self.invoke(scene=scene, coupon=coupon)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")
        self.assertIsNone(mesh)
        if message:
            self.assertIn(message, result.stderr)

    def test_closed_oriented_substrate_and_analytic_error(self):
        result, mesh = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertTrue(report["scene_collision_checked"])
        self.assertTrue(report["substrate_geometry_generated"])
        self.assertFalse(report["substrate_manufacturing_planned"])
        self.assertTrue(report["tip_vs_prior_tracks_checked"])
        self.assertEqual(report["material_tracks"], 10)
        self.assertGreater(report["material_cells"], 1000)
        self.assertGreater(report["permitted_contact_pairs"], 0)
        self.assertFalse(report["printer_safe"])
        points = [tuple(map(float, line.split()[1:])) for line in mesh.splitlines() if line.strip().startswith("vertex ")]
        self.assertEqual(len(points) // 3, report["substrate_triangles"])
        self.assertEqual(report["substrate_triangles"], 4096)
        self.assertEqual(min(p[0] for p in points), -12)
        self.assertEqual(max(p[0] for p in points), 12)
        self.assertEqual(min(p[1] for p in points), -6)
        self.assertEqual(max(p[1] for p in points), 6)
        edges = Counter()
        directed = Counter()
        volume = 0
        for i in range(0, len(points), 3):
            a, b, c = points[i:i+3]
            for u, v in ((a, b), (b, c), (c, a)):
                edges[tuple(sorted((u, v)))] += 1
                directed[u, v] += 1
            normal = cross([b[j] - a[j] for j in range(3)], [c[j] - a[j] for j in range(3)])
            self.assertGreater(math.hypot(*normal), 0)
            volume += sum(a[j] * cross(b, c)[j] for j in range(3)) / 6
            if all(p[2] > 0 for p in (a, b, c)):
                self.assertGreater(normal[2], 0)
                # Independent barycentric check: top triangles bound the exact quadratic from above.
                x = (a[0] + b[0] + c[0]) / 3
                y = (a[1] + b[1] + c[1]) / 3
                z = (a[2] + b[2] + c[2]) / 3
                error = z - (2 + 0.005 * x*x + 0.0025 * y*y)
                self.assertGreaterEqual(error, -1e-12)
                self.assertLessEqual(error, report["substrate_error_bound_mm"] + 1e-12)
        self.assertTrue(all(n == 2 for n in edges.values()))
        self.assertTrue(all(directed[v, u] == count for (u, v), count in directed.items()))
        # Integral of 2 + ax^2 + by^2 over the 24x12 footprint.
        analytic_volume = 24 * 12 * (2 + 0.005 * 12**2 / 3 + 0.0025 * 6**2 / 3)
        self.assertGreaterEqual(volume, analytic_volume - 1e-8)
        self.assertLessEqual(volume, analytic_volume + 24 * 12 * report["substrate_error_bound_mm"] + 1e-8)

    def test_mid_segment_world_obstacle_is_not_missed(self):
        # First travel goes from (0,0,20) to (-10,-4,20). Both ends
        # clear this box; the swept nozzle body crosses it at t=0.5.
        scene = self.scene + "world_box mid_sweep -5.1 -2.1 20.5 -4.9 -1.9 21.5\n"
        self.reject(scene=scene, message="mid_sweep")

    def test_bed_attached_obstacle_is_rejected(self):
        self.reject(scene=self.scene + "bed_box strike -10.2 -4.2 2.3 -9.8 -3.8 3.5\n", message="strike")

    def test_nozzle_radius_and_tip_radius_affect_clearance(self):
        self.reject(scene=self.scene.replace("capsule nozzle 0.2 0.6 2", "capsule nozzle 1 0.6 2"), message="nozzle")
        self.reject(scene=self.scene.replace("tip_radius 0.2", "tip_radius 5"), message="tip_disk")

    def test_bed_plane_contact_and_unresolved_margin_are_rejected(self):
        self.reject(scene=self.scene.replace("capsule hotend 2 4 12", "capsule hotend 30 0 0"), message="bed-plane")
        self.reject(scene=self.scene + "world_box margin_contact 0.22 -0.1 19.99 0.3 0.1 20.01\n", message="margin_contact")

    def test_missing_base_or_insufficient_footprint_rejected(self):
        self.reject(coupon=(FIXTURES / "heightmap-coupon.cfg").read_text(), message="substrate thickness")
        self.reject(scene=self.scene.replace("bed_bounds -150 150 -150 150", "bed_bounds -5 5 -5 5"), message="outside bed")
        self.reject(scene=self.scene.replace("substrate_margin 2", "substrate_margin 0.1"), message="footprint")

    def test_invalid_scene_fails_without_geometry_output(self):
        for old, new in (("substrate_grid 40 24", "substrate_grid 0 24"),
                         ("clearance_margin 0.02", "clearance_margin 0"),
                         ("tip_radius 0.2", "tip_radius nan"),
                         ("capsule nozzle 0.2 0.6 2", "capsule nozzle -1 0.6 2")):
            with self.subTest(new=new):
                self.reject(scene=self.scene.replace(old, new))
        self.reject(scene=self.scene + "unknown 1\n")
        self.reject(scene=self.scene + "capsule broken 1\n")

    def test_existing_mesh_is_preserved(self):
        result, mesh = self.invoke(existing=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(mesh, "keep this")

    def test_dense_same_layer_contacts_are_not_blanket_exempted(self):
        # The sparse diagnostic contact policy does not authorize joining
        # adjacent overlapping tracks. This needs a future infill/join model.
        self.reject(coupon=self.coupon.replace("rows 5", "rows 50"), message="prior material")


if __name__ == "__main__":
    unittest.main()
