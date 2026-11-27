"""Independent schedule, spacing, interpolation, dose and preflight checks."""
import csv
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

COUPON, SCENE, FIT = [str(Path(p).resolve()) for p in sys.argv[1:4]]
del sys.argv[1:4]
FIXTURES = Path(__file__).resolve().parent / "fixtures"


def rotation(sx, sy):
    n = math.sqrt(1 + sx*sx + sy*sy)
    k = n * (n + 1)
    return ((1-sx*sx/k, -sx*sy/k, -sx/n), (-sx*sy/k, 1-sy*sy/k, -sy/n), (sx/n, sy/n, 1/n))


def apply(r, p):
    return [sum(a*b for a, b in zip(row, p)) for row in r]


class TransitionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = (FIXTURES / "transition-coupon.cfg").read_text()
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "path.csv"
            result = subprocess.run([COUPON, str(FIXTURES / "virtual-bed.machine"),
                                     str(FIXTURES / "transition-coupon.cfg"), "--csv", str(output)],
                                    capture_output=True, text=True, timeout=40)
            if result.returncode:
                raise RuntimeError(result.stderr)
            cls.report = json.loads(result.stdout)
            cls.rows = list(csv.DictReader(output.open()))
            for row in cls.rows:
                for key in row.keys() - {"kind"}:
                    row[key] = float(row[key])

    def test_flat_start_full_curvature_end_and_layer_geometry(self):
        layers = set()
        for row in self.rows:
            for key, value in row.items():
                if key != "kind":
                    self.assertTrue(math.isfinite(value))
            if row["kind"] != "extrusion":
                continue
            k = int(row["layer"])
            layers.add(k)
            fraction = (k-1)/23
            self.assertAlmostEqual(row["surface_fraction"], fraction)
            x, y = row["x"], row["y"]
            self.assertAlmostEqual(row["z"], 2 + k*0.2 + fraction*(0.005*x*x + 0.0025*y*y))
            self.assertAlmostEqual(row["pitch"], -math.degrees(math.atan(fraction*0.01*x)))
            self.assertAlmostEqual(row["roll"], -math.degrees(math.atan(fraction*0.005*y)))
        self.assertEqual(layers, set(range(1, 25)))
        self.assertGreaterEqual(self.report["planned_interlayer_gap_lower_mm"], 0.15)
        self.assertLessEqual(self.report["planned_interlayer_gap_upper_mm"], 0.25)
        # All ideal interlayer spacings lie within the reported conservative range.
        for x in range(-10, 11):
            for y in range(-4, 5):
                gap = 0.2 + (0.005*x*x + 0.0025*y*y)/23
                self.assertGreaterEqual(gap, self.report["planned_interlayer_gap_lower_mm"])
                self.assertLessEqual(gap, self.report["planned_interlayer_gap_upper_mm"])

    def test_interpolated_machine_geometry_and_speed(self):
        for a, b in zip(self.rows, self.rows[1:]):
            dt = b["time"]-a["time"]
            self.assertGreater(dt, 0)
            for key, limit in (("world_x",100),("world_y",100),("world_z",30),
                               ("axis_U",10),("axis_V",10),("axis_W",10),("e",25)):
                self.assertLessEqual(abs(b[key]-a[key])/dt, limit+1e-8)
            if b["kind"] != "extrusion":
                continue
            k = int(b["layer"])
            fraction = (k-1)/23
            x, y = (a["x"]+b["x"])/2, (a["y"]+b["y"])/2
            u,v,w = [(a[key]+b[key])/2-100 for key in ("axis_U","axis_V","axis_W")]
            sx = (v-u)/200
            sy = (w-(u+v)/2)/200
            lift = (u+v)/2+100*sy
            self.assertAlmostEqual(sx, -fraction*0.01*x)
            self.assertAlmostEqual(sy, -fraction*0.005*y)
            r = rotation(sx,sy)
            ideal = apply(r,[x,y,2+k*0.2+fraction*(0.005*x*x+0.0025*y*y)])
            ideal[2] += lift
            actual = [(a[key]+b[key])/2 for key in ("world_x","world_y","world_z")]
            self.assertLessEqual(math.dist(actual,ideal), b["position_bound"]+1e-9)
            actual[2] -= lift
            local = apply(list(zip(*r)),actual)
            normal = apply(r,[-fraction*0.01*local[0],-fraction*0.005*local[1],1])
            angle = math.degrees(math.atan2(math.hypot(*normal[:2]),normal[2]))
            self.assertLessEqual(angle,self.report["alignment_error_bound_deg"]+1e-8)

    def test_variable_spacing_extrusion_against_independent_integral(self):
        volume = 0
        steps = 2048
        dx = 20/steps
        for k in range(1,25):
            beta = (k-1)/23
            delta = 0 if k==1 else 1/23
            for y in (-4,-2,0,2,4):
                def density(x):
                    f = 0.005*x*x+0.0025*y*y
                    return 0.4*(0.2+delta*f)*math.sqrt(1+(beta*0.01*x)**2)/math.sqrt(1+(beta*0.01*x)**2+(beta*0.005*y)**2)
                total = density(-10)+density(10)
                total += sum((4 if i%2 else 2)*density(-10+i*dx) for i in range(1,steps))
                volume += total*dx/3
        self.assertLessEqual(abs(volume-self.report["volume_mm3"]),self.report["volume_error_bound_mm3"]+1e-9)
        self.assertGreater(volume,199)  # fixed 0.2 mm dose would be below 193 mm3
        self.assertAlmostEqual(self.rows[-1]["e"]+0.8,self.report["deposited_filament_mm"])

    def test_scene_history_and_actual_flat_interface(self):
        result = subprocess.run([SCENE,str(FIXTURES/"virtual-bed.machine"),str(FIXTURES/"transition-coupon.cfg"),
                                 str(FIXTURES/"coupon.scene")],capture_output=True,text=True,timeout=40)
        self.assertEqual(result.returncode,0,result.stderr)
        report=json.loads(result.stdout)
        self.assertTrue(report["tip_vs_prior_tracks_checked"])
        self.assertEqual(report["material_tracks"],120)
        self.assertFalse(report["printer_safe"])
        result = subprocess.run([FIT,str(FIXTURES/"virtual-bed.machine"),str(FIXTURES/"transition-coupon.cfg"),
                                 str(FIXTURES/"substrate-fit.cfg"),str(FIXTURES/"substrate-flat-005.gcode")],
                                capture_output=True,text=True,timeout=40)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertTrue(json.loads(result.stdout)["fit_passed"])

    def test_unsafe_short_transition_or_missing_limits_rejects(self):
        changes = (("transition_layers 24","transition_layers 2"),
                   ("transition_layers 24","transition_layers 1"),
                   ("layer_gap_limits 0.15 0.25","layer_gap_limits 0 0"),
                   ("material_work_budget 10000000","material_work_budget 1"))
        for old,new in changes:
            with self.subTest(new=new),tempfile.TemporaryDirectory() as folder:
                config=Path(folder)/"config"
                config.write_text(self.config.replace(old,new))
                result=subprocess.run([SCENE,str(FIXTURES/"virtual-bed.machine"),str(config),str(FIXTURES/"coupon.scene"),
                                       "--substrate-stl",str(Path(folder)/"output.stl")],
                                      capture_output=True,text=True,timeout=40)
                self.assertNotEqual(result.returncode,0)
                self.assertEqual(result.stdout,"")
                self.assertFalse((Path(folder)/"output.stl").exists())


if __name__=="__main__":
    unittest.main()
