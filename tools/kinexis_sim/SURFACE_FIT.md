# Manufactured-substrate surface-fit assessment

`kinexis-substrate-fit` compares a registered planar G-code deposition model
with the first coupon layer over its conservative XY footprint. It does not
equate the manufactured top with the source STL. This is a software model;
actual bead shape, registration and the completed curved job are not validated.

```powershell
./build/kinexis-sim/Release/kinexis-substrate-fit.exe tools/kinexis_sim/fixtures/virtual-bed.machine tools/kinexis_sim/fixtures/supported-coupon.cfg tools/kinexis_sim/fixtures/substrate-fit.cfg tools/kinexis_sim/fixtures/substrate-curved-005.gcode --map NEW_MAP.csv
```

Exit 0 means every assessed cell passes the declared model limits; exit 1
means a modeled failure or unresolved cell; exit 2 means invalid/unsupported
input or exhausted resources. A diagnostic CSV may be written for an exit-1
assessment; it is not an approved motion artifact. Existing paths are preserved.
Invalid input/resource exhaustion emits no map or success report.

## G-code and bead model

Replay handles modal XYZ/E and G92 resets. Deposition records retain the
PrusaSlicer `TYPE`, `WIDTH` and `HEIGHT` comments; ordinary replay/byte identity
continues to ignore their semantics. Surface assessment requires valid geometry
hints for every moving extrusion. Bridges, unsupported roles, nonplanar
deposition, unmodeled priming and moving recovery reject. Retraction/recovery
are tracked separately from deposited volume.

[Flow.cpp](../../src/libslic3r/Flow.cpp) uses the nonbridge cross-section area
`h*(w-h*(1-pi/4))`. We use this upstream *volume formula* to check annotated
width against emitted E and declared filament diameter. This does not prove
that a physical bead has the assumed manufactured shape.

The declared virtual surface is a rounded strip: a flat top of width `w-h`,
with semicircular shoulders of radius h/2. At perpendicular distance d from
the centerline its top is z for `d <= (w-h)/2`, otherwise
`z-h/2+sqrt((h/2)^2-(d-(w-h)/2)^2)` up to d=w/2. Width bounds include the
configured width uncertainty and the E-rounding allowance; a separate Z
uncertainty expands surface-height bounds. A dose inconsistent with the width
annotation rejects instead of inventing a much wider bead.
If the lower width bound crosses below h, the rounded-strip model supplies no
guaranteed lower support. It is not clamped up to a larger circular bead.

For a guaranteed lower surface bound, the entire cell must project inside a
finite extrusion segment, and its maximum perpendicular distance must fit
the narrower width. Endcaps are not assumed to provide guaranteed support.
For a possible upper bound, minimum segment-to-cell distance and the wider
rounded capsule footprint conservatively include possible endpoint material.
Unsupported endpoint/seam regions therefore remain uncertain, not silently full.

## Continuous footprint check

Each first-layer track is enclosed by an XY rectangle, including half bead
width, normal-depth XY displacement and controller chord error. Desired first
layer Z is bounded over each cell using the convex quadratic, with controller
error and a tangent/curvature residual. Frame translation is explicit; no
registration is guessed from G-code. The fixture translation is (100,100,0).

For a cell, let L be the best guaranteed lower bound from a bead covering the
whole cell, and U the greatest possible upper bound among intersecting beads.
Desired nozzle Z is bounded by `[Dmin,Dmax]`. The vertical interface gap lies
inside `[Dmin-U,Dmax-L]`. It passes only if that whole interval is inside the
configured limits. Unresolved cells subdivide; depth exhaustion remains
uncertain, and cell/work exhaustion rejects the whole assessment. This checks
regions, not just sample points, and does not fill missing support optimistically.

The default **virtual acceptance** interval is 0.15–0.25 mm around the 0.2 mm
coupon spacing. Width uncertainty is 0.005 mm and Z uncertainty 0.005 mm.
These values are test assumptions, not measured capabilities or recommended
production settings. They were held fixed during the profile comparisons.

## Results and next strategy

Real sliced cases and hashes are recorded in `fixtures/substrate-fit-cases.json`:

- A fully filled curved base with 0.05 mm layers does not pass. Some model cells
  definitely violate the selected limits; seam/coverage regions also remain
  unproven. The earlier coarse CLI-default base additionally contains unsupported
  bridge deposition and cannot be approved by this strip model.
- A 0.025 mm curved-base experiment with a rear seam removes the definite
  violations, but endpoint/coverage bounds remain uncertain, even with deeper
  subdivision. It is not approved.
- A fully filled **flat 2 mm base and flat first-layer control** passes, with
  modeled gap bounds about 0.195–0.221 mm. This is a different interface, not
  approval of the original curved coupon on that base.

The supported next strategy is to develop a planar first layer followed by a
gradual transition to the desired curvature, then revalidate layer spacing,
deposition, motion and collisions for the whole transition. That transition is
not implemented by this assessment. Alternatively, a prepared curved substrate
would need independently established surface bounds and registration; an STL
or successful slice is insufficient evidence.

The test suite includes analytic rounded-bead controls, high/low/missing
support, a small endpoint interruption, wrong registration, invalid metadata,
bridge and dose rejection, resource exhaustion, existing-output preservation,
and real passing/rejected slicer files. With a built slicer configured in CMake,
`fresh-substrate-assessment` regenerates both model cases and verifies planar
replay/identity before fit analysis.

A green test run means the assessor behaves as expected, including rejection
of the curved case. It does not mean every manufacturing candidate passes.
`printer_safe`, `registration_calibrated`, `complete_curved_job_validated`,
`planar_build_support_checked` and `planar_motion_limits_checked` remain false
in the standalone fit report. Motion-limit replay is a separate pipeline check.
