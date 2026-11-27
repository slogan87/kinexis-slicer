# Virtual planar substrate preview

After chronological material preflight passed, the local PrusaSlicer Release
build exported a normal 3-axis plan for the matched substrate STL. This is a
manufacturing **preview**, not a commissioned printer job or a passed physical
substrate gate.

The 2026-11-27 local preview used an isolated CLI-default profile and explicit
`--center 100,100`. Replay decoded 1,243 moves, with homing paths excluded,
and preserved the entire file byte-for-byte in both disabled and zero-tilt
modes. Its virtual settings used 0.35 mm first-layer height and 0.3 mm nominal
layer height, while the curved coupon uses 0.2 mm spacing. These are recorded
inputs for the fit check, not selected production settings.
Generation and replay succeeded without changing printer configuration
or communicating with hardware.

Reproduction from a built fork (use new output paths):

```powershell
./build/kinexis-sim/Release/kinexis-scene-check.exe tools/kinexis_sim/fixtures/virtual-bed.machine tools/kinexis_sim/fixtures/supported-coupon.cfg tools/kinexis_sim/fixtures/coupon.scene --substrate-stl NEW_SUBSTRATE.stl
./build/kinexis-baseline/src/Release/prusa-slicer-console.exe --datadir NEW_PROFILE_DIRECTORY --center 100,100 --export-gcode --output NEW_PLAN.gcode NEW_SUBSTRATE.stl
./build/kinexis-sim/Release/kinexis-gcode-check.exe tools/kinexis_sim/fixtures/planar-replay.machine NEW_PLAN.gcode --home 0 0 0 --identity-output NEW_ZERO_COPY.gcode
./build/kinexis-sim/Release/kinexis-gcode-check.exe tools/kinexis_sim/fixtures/planar-replay.machine NEW_PLAN.gcode --home 0 0 0 --disabled --identity-output NEW_DISABLED_COPY.gcode
```

The slicer centers the substrate in a virtual planar coordinate frame. This
must be registered to the tilting-bed coupon frame before any combined job is
considered; the coupon's bed-local origin is not that virtual printer center.

The next required check is **surface fit**. A planar-sliced base has a stepped
top, while the coupon's substrate model is a smooth quadratic with a tightly
bounded STL approximation. Replay/byte identity does not prove that the
manufactured stepped top supports the curved first layer at the intended
spacing. Reconstruct or bound the actual planar deposition surface, compare
the first curved layer against it, then select layer settings, surface
preparation or a conforming first-layer strategy explicitly. Do not silently
treat the ideal STL as the manufactured surface.

Real printer profiles, thermal/material parameters, coordinate registration,
base surface fit, hardware commissioning and physical prints remain open.
`kinexis-scene-check` still reports `substrate_manufacturing_planned: false`:
its job is geometric preflight, not this separate unvalidated planning preview.

## Surface assessment update (2026-11-28)

The new `kinexis-substrate-fit` checks the reconstructed bounded bead surface,
not just slice success. The direct curved-base plan remains rejected/unproven
under the fixed virtual 0.15–0.25 mm gap interval. A fully filled flat-base and
flat-first-layer control passes, at roughly 0.195–0.221 mm. That does not approve
the curved coupon on the flat base; the next strategy requires implementing
and checking a gradual transition from a planar first layer to the target
curvature. Detailed assumptions and cases are in
[`SURFACE_FIT.md`](../tools/kinexis_sim/SURFACE_FIT.md).

## 2026-11-28: gradual transition software fixture

Implemented a flat first layer progressing to full curvature over 24 sparse
layers in `tools/kinexis_sim/fixtures/transition-coupon.cfg`. Variable-spacing
extrusion and explicit layer metadata accompany the geometry. Conservative
interlayer bounds are 0.177368–0.247342 mm within configured 0.15–0.25 mm.
All eleven local test groups pass, including independent interpolation/dose
checks, chronological material/scene preflight and a newly sliced flat-base
first-interface assessment. See `tools/kinexis_sim/TRANSITION_MODEL.md` in the
fork. This is a software fixture; general production slicing, firmware output
and physical Duet/RRF commissioning remain open. Klipper stays last.
