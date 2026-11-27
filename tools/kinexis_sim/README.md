# Kinexis offline bed simulation

This is the first software-only kinematics harness, written in C++17 with no
third-party dependencies. It is independent of the slicer GUI and does not
change PrusaSlicer output. The bed simulator emits CSV reports. The planar
replay adapter can copy an original G-code file verbatim after its checks;
neither tool generates nonzero-tilt printer G-code.
The machine fixture is invented test geometry, not a commissioned Duet profile.

## Run

From the fork root:

```powershell
cmake -S tools/kinexis_sim -B build/kinexis-sim
cmake --build build/kinexis-sim --config Release
ctest --test-dir build/kinexis-sim -C Release --output-on-failure
./build/kinexis-sim/Release/kinexis-sim.exe tools/kinexis_sim/fixtures/virtual-bed.machine tools/kinexis_sim/fixtures/motion.csv
```

For Linux, the executable is `build/kinexis-sim/kinexis-sim` (no Release folder).
Append `--disabled` to retain the original XYZ and E while holding all actuators
at their neutral values. Zero pose also preserves XYZ bit-for-bit. These checks
cover the simulator's motion representation; integration with real planar
G-code integration is exercised separately below; the production slicer still
has no non-planar export feature.

Python 3 enables the additional CLI validation suite in CTest. CI provides it
on Windows and Linux. To run the same numerical and CLI tests using Docker:

```sh
docker build --network=none -t kinexis-sim-test tools/kinexis_sim
docker run --rm --network=none --read-only --tmpfs /tmp kinexis-sim-test
```

The image uses the compiler already supplied by `python:3.12-bookworm`. No
packages are installed. The first build may need to pull that base image;
subsequent builds can run offline. Only the simulation directory enters the
build context; no repository credentials or printer connections are required.

## Model and conventions

Units are millimetres, seconds, and degrees. Input XYZ is the nozzle **tip** in
bed-local coordinates; carriage/tool-length compensation is not implemented.
The simulation uses a fixed-length zero-offset tip tool, not a collision solid.
The bed pivot is `(pivot_x, pivot_y, 0)` in a right-handed XYZ frame.
`lift` raises it in world +Z. Positive pitch rises towards +X; positive roll
rises towards +Y. These are **plane slope angles**, not ordered Euler rotations:

```text
z(x,y) = lift + tan(pitch)*(x-pivot_x) + tan(roll)*(y-pivot_y)
command_i = zero_i + sign_i*z(contact_x_i, contact_y_i)
```

Supports are ideal vertical actuators contacting the plane at fixed world XY,
with freely sliding contact joints. A real bed with fixed material attachment
points and linkages needs a different mechanical model. The nozzle tip is
transformed with the shortest rotation aligning +Z to the plane normal
`(-tan(pitch), -tan(roll), 1)` and translated by lift. This fixes the otherwise
ambiguous in-plane yaw and preserves lengths; it does not warp the mesh.

The fixture declares pivot, pitch/roll bounds, XYZ bounds and component speeds,
extruder speed, then exactly three actuator rows:
`actuator name contact_x contact_y sign neutral min max speed`.
U/V/W are report labels only, not an assertion about the eventual RRF dialect.
All signs are +1 in the sample fixture, with a neutral command of 100 mm and
50–150 mm actuator travel. Contacts are (-100,-100), (100,-100), (0,100).
XYZ ranges are (-150,-150,0) to (150,150,250), tilt bounds ±15 degrees,
XYZ speed limits (100,100,30) mm/s, actuator limits 10 mm/s, E limit 25 mm/s.

The input CSV uses absolute E. Each move kind (extrusion, travel, retract,
lift, transition, wipe) undergoes the same checks. The first pose must be
neutral; homing/initial placement is assumed, not simulated. Times strictly
increase. Positions, poses, limits, and speeds must be finite. Invalid input
exits nonzero on stderr with **no report on stdout**. The entire trajectory is
validated before output. No tilt clamping is performed.
Angular comparisons allow a 1e-10 degree numerical residual for exact-boundary
reconstruction; speed comparisons allow 1e-9 mm/s. Coordinates are not clamped.

Between samples the modeled machine interpolates **axis commands linearly**.
Thus endpoint box limits and component rates bound that segment; nonlinear
angle-space interpolation and acceleration/jerk are not validated. Reconstructed
poses at samples are included in the report. Toolpath subdivision, collision
clearance, material deposition compensation, 3D preview, RRF synchronization,
and hardware commissioning remain unimplemented. A passed report does not mean
the trajectory is printer-safe.

## Verification

Tests include independently calculated pure positive/negative pitch and roll,
combined plane heights, shifted pivot and reversed sign, a known 30-degree
rigid rotation, reconstruction, length preservation, exact disabled/zero-pose
identity, every supported move kind, and rejection of degenerate geometry,
non-finite values, invalid timing, travel, tilt, and speed violations.

## Planar G-code replay and identity

```powershell
./build/kinexis-sim/Release/kinexis-gcode-check.exe tools/kinexis_sim/fixtures/planar-replay.machine tools/kinexis_sim/fixtures/prusa-lbracket.gcode --home 0 0 0 --identity-output checked-planar.gcode
```

The output path must not exist. Append `--disabled` for the disabled-transform
path; the default is enabled with zero pose. All parsed motion is replayed
through the bed simulator, with exact XYZ/E identity and neutral-actuator
checks. Only after the entire replay passes can the original input bytes be
copied, including comments, numeric spelling and line endings. This is the
neutral-output adapter contract, not a test of a new nonzero-tilt G-code writer.
The JSON report always declares `printer_safe: false`.

`--home X Y Z` supplies an explicit **test fixture**, not homing instructions.
Initial state is millimetres and absolute XYZ, with logical and physical XYZ
at that home. E mode must be explicitly selected with M82/M83 before E moves,
and selected again after G90/G91; this avoids guessing firmware-specific
extruder-mode interactions. G92 changes logical coordinates without creating
motion; E is unwrapped across resets and relative retractions. A homing command
resets only the requested XYZ axes to the fixture and starts a new segment.
Its physical homing path is excluded from validation and counted in the report.

The intentionally restricted, uppercase G-code subset is:

- G0/G1 with XYZ/E/F, G4 with P or S, G21, G28 (all axes or X0/Y0/Z0),
  G90/G91, G92 with explicit XYZ/E, and M82/M83.
- M104/M109/M140/M190 (S/R and optional T0), M106 (S/P), M107 (P),
  and terminal M84 (optional S). Temperature, fan, motor-disable behaviour and
  heater-wait durations are not simulated.

Arcs, inch units, macros, additional axes/tools, coordinate systems, firmware
retraction, line numbers/checksums, binary input and unknown commands are
rejected, including in disabled mode. Numbers use ordinary decimal syntax;
parenthesis comments and exponent notation are outside this subset. Each line
needs an explicit command. No unsupported move is silently dropped.

Timing uses constant requested F in mm/min, based on XYZ distance or pure-E
distance. Acceleration, firmware speed caps, heater waits and homing duration
are not modeled. Dwells insert stationary samples so they cannot mask the
speed of the subsequent move. The separate `planar-replay.machine` is an
invented, permissive envelope for the real planar fixture, not Duet hardware
limits. It uses 0–250 mm XYZ, component speed bounds 500/500/200 mm/s and
100 mm/s E; the actuator geometry and signs remain explicit in the fixture.

The checked-in L-bracket G-code was generated from the adjacent STL using the
built fork. Its provenance, SHA256 and 10,009-move expectation are recorded in
`fixtures/prusa-lbracket.json`. Git preserves its original bytes. Tests verify
intermediate modal states against analytic positions/E values, reject unsafe
or unsupported inputs without creating an identity output, and preserve
existing output files.

To test a **fresh export** from a built slicer, reconfigure with:

```powershell
cmake -S tools/kinexis_sim -B build/kinexis-sim -DKINEXIS_SLICER_EXECUTABLE=C:/path/to/prusa-slicer-console.exe
ctest --test-dir build/kinexis-sim -C Release --output-on-failure
```

This adds `fresh-planar-export`, which slices the STL with an isolated profile
and checks the new export in both modes byte-for-byte. The native Windows CI
does this after building PrusaSlicer; standalone Windows/Linux CI replays the
checked-in real export without building the full GUI dependency stack.

## Software-only height-map coupon

```powershell
./build/kinexis-sim/Release/kinexis-coupon.exe tools/kinexis_sim/fixtures/virtual-bed.machine tools/kinexis_sim/fixtures/heightmap-coupon.cfg --csv coupon.csv
```

This generates **simulation reports only**, never firmware commands. The CSV
path must be new. It contains bed-local XYZ, E, pose, transformed world XYZ,
declared actuator coordinates (`axis_U` etc.), and the incoming segment's
position-error bound. JSON on stdout summarizes volume/error/clearance bounds.
Configuration, subdivision or limit failures produce no report or CSV.

The default fixture is two sparse raster layers (five tracks per layer) over
`z=0.005*x²+0.0025*y²`, for X ±10 and Y ±4 mm, with 0.2 mm vertical layer
spacing. It is a diagnostic trajectory, not a filled or supported solid. No
substrate, support material, substrate-conforming first layer, or printable
base is generated. It must not be sent to a printer.

Bed tilt follows the **negative** surface gradient to align the local surface
normal with the fixed vertical nozzle. A virtual 5 mm bed lift keeps world Z
in range. Between tracks the extruder retracts, the tip lifts to bed-local
Z=20 mm, travels at clearance while updating the bed pose, descends, and
recovers the retraction. Initial/final bed orientation changes occur at the
same clearance plane; the final position is neutral and retracted.

The generator adaptively subdivides with conservative chord-error bounds for
linear world-XYZ/actuator interpolation, not just endpoint checks. It separately
bounds surface-normal alignment, vertical point-clearance on cleared travel,
and midpoint volume-integration error. Durations are lengthened as needed for
configured XYZ, actuator and E speed limits. There is no acceleration/jerk or
tool-body collision model.

Extrusion uses a declared local-tangent rectangular-bead model: surface width
times vertical layer spacing projected onto the surface normal, integrated
along the bed-local curved path. This is an approximation to finite physical
layer spacing, not validated bead physics. It avoids counting bed motion as
deposition length. See [COUPON_BOUNDS.md](COUPON_BOUNDS.md) for assumptions and
the geometry, clearance and volume-bound derivations.

The default run emits 2,730 samples, approximately 15.9984 mm³ of deposition,
with a maximum conservative position-error bound below 0.0047 mm (target
0.01 mm). Tests independently reconstruct interpolated axis motion, check
interior path errors/normal alignment/clearance, compare volume with a separate
high-resolution integral, verify the flat-surface limit and retraction balance,
and reject impossible limits or exhausted subdivision budgets.

`collision_checked`, `support_generated` and `printer_safe` remain false.
General mesh warping, arbitrary height maps, collision solids, physical prints,
and nonzero-tilt RRF/other firmware output remain separate gates.
