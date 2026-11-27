"""Fresh native substrate generation -> slicing -> replay -> fit assessment."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

slicer, scene, checker, fit = [str(Path(p).resolve()) for p in sys.argv[1:]]
fixtures = Path(__file__).resolve().parent / "fixtures"
with tempfile.TemporaryDirectory(prefix="kinexis-substrate-") as folder:
    folder = Path(folder)
    for name, coupon, expected, extra in (
        ("curved", "supported-coupon.cfg", False, []),
        ("flat", "flat-interface-coupon.cfg", True,
         ["--top-fill-pattern", "rectilinear", "--fill-angle", "0", "--seam-position", "rear",
          "--top-infill-extrusion-width", "0.45"]),
    ):
        mesh = folder / (name + ".stl")
        source = folder / (name + ".gcode")
        subprocess.run([scene, str(fixtures / "virtual-bed.machine"), str(fixtures / coupon),
                        str(fixtures / "coupon.scene"), "--substrate-stl", str(mesh)], check=True, timeout=30)
        subprocess.run([slicer, "--datadir", str(folder / "profile"), "--center", "100,100", "--export-gcode",
                        "--fill-density", "100%", "--fill-pattern", "rectilinear", "--perimeter-generator", "classic",
                        "--layer-height", "0.05", "--first-layer-height", "0.1", "--extrusion-width", "0.45",
                        "--first-layer-extrusion-width", "0.45", "--output", str(source), *extra, str(mesh)],
                       check=True, timeout=90)
        for mode in ("zero", "disabled"):
            identity = folder / (name + "-" + mode + ".gcode")
            args = [checker, str(fixtures / "planar-replay.machine"), str(source), "--home", "0", "0", "0",
                    "--identity-output", str(identity)]
            if mode == "disabled":
                args.append("--disabled")
            subprocess.run(args, check=True, timeout=30)
            if identity.read_bytes() != source.read_bytes():
                raise RuntimeError("substrate byte identity failed")
        result = subprocess.run([fit, str(fixtures / "virtual-bed.machine"), str(fixtures / coupon),
                                 str(fixtures / "substrate-fit.cfg"), str(source)],
                                capture_output=True, text=True, timeout=60)
        if result.returncode != (0 if expected else 1):
            raise RuntimeError(result.stderr or result.stdout)
        report = json.loads(result.stdout)
        if report["fit_passed"] != expected or report["printer_safe"] or report["registration_calibrated"]:
            raise RuntimeError("unexpected fit assessment")
        print(name, "model_fit_passed=" + str(expected), "SHA256=" + hashlib.sha256(source.read_bytes()).hexdigest())
