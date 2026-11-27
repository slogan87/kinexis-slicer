"""Fresh PrusaSlicer CLI export -> simulator -> exact identity output."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

slicer, checker = map(lambda p: str(Path(p).resolve()), sys.argv[1:])
fixtures = Path(__file__).resolve().parent / "fixtures"
with tempfile.TemporaryDirectory(prefix="kinexis-planar-") as folder:
    folder = Path(folder)
    original = folder / "fresh.gcode"
    subprocess.run([slicer, "--datadir", str(folder / "profiles"), "--export-gcode",
                    "--output", str(original), str(fixtures / "overhang_lbracket.stl")],
                   check=True, timeout=90)
    baseline = original.read_bytes()
    for mode in ("zero-tilt", "disabled"):
        output = folder / (mode + ".gcode")
        args = [checker, str(fixtures / "planar-replay.machine"), str(original),
                "--home", "0", "0", "0", "--identity-output", str(output)]
        if mode == "disabled":
            args.append("--disabled")
        subprocess.run(args, check=True, timeout=30)
        if output.read_bytes() != baseline:
            raise RuntimeError(mode + " changed the planar export")
    print("Fresh planar export is byte-identical in both modes; SHA256=" +
          hashlib.sha256(baseline).hexdigest())
