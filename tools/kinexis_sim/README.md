# Kinexis offline bed simulation

This is the first software-only kinematics harness, written in C++17 with no
third-party dependencies. It is independent of the slicer GUI and does not
change PrusaSlicer output. It emits CSV reports, **not printer G-code**.
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
G-code and zero-tilt slicer equivalence are still acceptance gates.

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
