# Chronological tip versus deposited material

This model is restricted to the generated quadratic coupon's straight, monotone
raster tracks and bounded linear machine-axis interpolation. It is a virtual
local-tangent bead model, not measured bead physics or a general mesh/infill
collision engine. `printer_safe` remains false.

## Chronology and bead bounds

Each deposition interval is checked against the **existing** material history.
Its bead cell is added only after the interval passes. Future tracks are absent
from this history; retraction does not remove previously deposited cells.
An XY spatial index finds candidate cells, followed by full XYZ interval-box
overlap checks. The tool-tip disk is bounded across the entire interpolated
interval using the coupon's position-error bound and the bed rotation, as in
the static scene checker. It does not rely on endpoints alone.

Let S bound the surface-gradient norm over a deposition interval and e bound
tip chord error. In the stated rectangular local-tangent model, a bead spans
surface width w and normal depth h/sqrt(1+|gradient|²) (h is vertical layer spacing).
Conservative coordinate expansions around its nominal curved centerline are:

```text
XY padding: w/2 + h*S + e
Z below the ideal minimum: h + w*S/2 + e
Z above the ideal maximum: w*S/2 + e
```

The quadratic minimum uses nearest-to-zero coordinate bounds; the convex
maximum lies at an endpoint. The boxes therefore contain the stated bead
model throughout each interval, including path approximation. They can
overestimate occupied material. Ambiguous overlap with unrelated material
rejects the trajectory, even if a more exact future solver could clear it.

The extrusion dose must agree with the declared bead-volume model within its
integration bound and floating-point tolerance. A retraction ledger allows
stationary pressure recovery only up to the outstanding retract amount.
Unmodeled priming/blob deposition and moving recovery reject. Every E delta is
accounted for; deposition requires recovered E and nonzero XY displacement.

## Narrow intended-contact policy

Continuous extrusion intervals form a new track only when they lie on a
configured layer, follow its normal and move along a fixed, positive XY
direction. Reversal, turning or layer changes within that run reject.
Contact with earlier cells of **that same active track** is permitted: the
nozzle disk overlaps the freshly deposited trailing bead during forward motion.
Other tracks retain normal collision checks, including prior layers.

After extrusion ends, the same-track exception survives only at the last
deposition endpoint with unchanged bed pose: stationary retraction/hold, then
monotone vertical lift-off. Lateral motion, descent or pose change ends it.
It cannot be reacquired by returning to a completed track. A new extrusion run
does not inherit the old run's exemption. Stationary recovery still needs
retraction debt; it cannot invent an unmodeled blob at a contact point.

This deliberately does not authorize adjacent overlapping infill, perimeter
corners, resumed seams, wiping on material or arbitrary track welding. Dense
same-layer joins can reject and need a future, explicit contact model.

## Integration and limits

`kinexis-scene-check` runs this check before writing any substrate artifact.
A pass reports `tip_vs_prior_tracks_checked: true` plus track/cell/candidate
and permitted-contact counts. These counts describe the bounded coupon model,
not a printer safety certificate. Static tool bodies still use the conservative
complete material envelope in addition to these chronological tip checks.

The interval count includes the initial stationary state. The default fixture
has 10 tracks and 2,560 deposited cells. Regression tests
include the same crossing before/after deposition, a mid-segment travel crossing,
an unrelated extrusion crossing, valid fresh contact/lift-off, low lateral drag,
low pose changes, next-layer clearance, revisiting a completed layer, reversal,
overdose, unrecovered E, and resource exhaustion. CLI tests ensure rejected
history checks create no new STL and preserve existing outputs.

Work is capped at two million index/candidate operations, 100,000 cells and
bounded index queries. Exhaustion rejects instead of skipping candidates.
Numerical contact/continuity tolerances are 1e-9 in the model's mm/degree/E
units, used alongside the explicit scene separation margin. This is engineering
floating-point bounding, not a formal interval-arithmetic implementation.

Real bead shape, swelling, cooling, shrinkage, nozzle calibration, arbitrary
meshes/contacts, substrate manufacture, gantry dynamics and firmware execution
remain unvalidated. This implementation exports no nonzero-tilt printer code.
