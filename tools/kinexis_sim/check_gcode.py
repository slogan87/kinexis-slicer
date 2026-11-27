"""G-code modal semantics, real slicer fixture, and byte identity integration."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXE = str(Path(sys.argv.pop(1)).resolve())
FIXTURES = Path(__file__).resolve().parent / "fixtures"


class ReplayTests(unittest.TestCase):
    def invoke(self, text, *options, machine=None, existing=None):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            source = folder / "input.gcode"
            source.write_bytes(text)
            output = folder / "identity.gcode"
            if existing is not None:
                output.write_bytes(existing)
            config = folder / "machine.txt"
            config.write_text(machine or (FIXTURES / "planar-replay.machine").read_text())
            result = subprocess.run([EXE, str(config), str(source), "--home", "0", "0", "0",
                                     "--identity-output", str(output), *options],
                                    capture_output=True, text=True, timeout=20)
            return result, output.read_bytes() if output.exists() else None

    def reject(self, text):
        result, output = self.invoke(text)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")
        self.assertIsNone(output)
        self.assertIn("G-code replay rejected:", result.stderr)

    def test_real_prusaslicer_fixture(self):
        source = (FIXTURES / "prusa-lbracket.gcode").read_bytes()
        metadata = json.loads((FIXTURES / "prusa-lbracket.json").read_text())
        self.assertEqual(hashlib.sha256(source).hexdigest(), metadata["gcode_sha256"])
        for options in ((), ("--disabled",)):
            with self.subTest(options=options):
                result, output = self.invoke(source, *options)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(output, source)
                self.assertEqual(hashlib.sha256(output).digest(), hashlib.sha256(source).digest())
                report = json.loads(result.stdout)
                self.assertEqual(report["moves"], metadata["replayed_moves"])
                self.assertEqual(report["source_bytes"], metadata["source_bytes"])
                self.assertEqual(report["homing_commands"], metadata["homing_commands_excluded_from_path_validation"])
                self.assertGreater(report["net_extrusion"], 0)
                self.assertFalse(report["homing_paths_validated"])
                self.assertFalse(report["printer_safe"])

    def test_modal_offsets_retraction_relative_and_dwell(self):
        source = (b"G21\r\nG90\r\nM82\r\nG28\r\nG1 X10 Z2 E2 F600\r\n"
                  b"G92 X0 E0\r\nG1 X5 E3\r\nG91\r\nM83\r\n"
                  b"G1 X-2 E-1 F60\r\nG4 P500\r\nG1 Z1 E0.5\r\n"
                  b"G90\r\nM82\r\nG1 X8 E4\r\n; preserve CRLF and comments\r\n")
        result, output = self.invoke(source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output, source)
        report = json.loads(result.stdout)
        self.assertEqual(report["moves"], 5)
        self.assertEqual(report["final_tip"], [18, 0, 3])
        self.assertEqual(report["net_extrusion"], 6)

    def test_homing_is_a_boundary_not_a_fabricated_move(self):
        source = b"M82\nG1 X50 E1 F60\nG28 X0\nG1 X1 E2 F60\n"
        result, _ = self.invoke(source)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report["moves"], 2)
        self.assertEqual(report["homing_commands"], 1)
        self.assertEqual(report["segments"], 2)
        self.assertEqual(report["final_tip"], [1, 0, 0])

    def test_unsupported_geometry_and_state_never_passthrough(self):
        prefix = b"M82\nG1 X1 F60\n"
        for command in (b"G2 X2 I1", b"G3 X2 I1", b"G20", b"G10", b"G54",
                        b"T1", b"M98 P1", b"G1 U2", b"G1 A10", b"G1 S2",
                        b"G90.1", b"G1 X2*10", b"N12 G1 X2", b"M204 P1000"):
            with self.subTest(command=command):
                self.reject(prefix + command + b"\n")

    def test_bad_numbers_parameters_and_missing_feed(self):
        for source in (b"G1 X1\n", b"G1 X1 F0\n", b"G1 X1 F-1\n", b"G1 Xnan F60\n",
                       b"G1 X1 X2 F60\n", b"G1 X1 F60\nG1 X999\n", b"G1 X-1 F60\n",
                       b"M82\nG1 E1000 F12000\n", b"G4 P-1\nG1 X1 F60\n",
                       b"G4 P1 S1\nG1 X1 F60\n", b"G1 X1 F60\nM84\nG1 X2\n"):
            with self.subTest(source=source):
                self.reject(source)
        self.reject(b"G1 X1 F60\n; binary comment\0\n")

    def test_explicit_e_mode_required_after_coordinate_mode(self):
        self.reject(b"G1 E1 F60\n")
        self.reject(b"M82\nG90\nG1 E1 F60\n")
        self.reject(b"M83\nG91\nG1 E1 F60\n")

    def test_dwell_cannot_mask_a_speed_violation(self):
        self.reject(b"G1 X1 F60\nG4 S1000\nG1 X100 F60000\n")

    def test_existing_output_is_preserved(self):
        result, output = self.invoke(b"G1 X1 F60\n", existing=b"keep this")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(output, b"keep this")

    def test_empty_and_comments_only_rejected(self):
        self.reject(b"")
        self.reject(b"; no motion\r\n")

    def test_geometry_hints_do_not_change_basic_replay(self):
        source = b";WIDTH:not-a-number\n;HEIGHT:unknown\n;TYPE:Unrecognized\nM82\nG1 X1 E0.1 F60\n"
        result, output = self.invoke(source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output, source)


if __name__ == "__main__":
    unittest.main()
