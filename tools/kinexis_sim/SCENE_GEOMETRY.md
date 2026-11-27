# Virtual collision scene and matched substrate

This preflight accepts only the generated quadratic coupon and its linear
machine-axis interpolation model. It is not a general mesh collision engine,
and scene dimensions are invented software fixtures, not commissioned hardware.
Passing this tool does not make a job printer-safe.

## Substrate

`supported-coupon.cfg` adds a 2 mm substrate offset to the previous coupon.
The substrate occupies bed-local `z=0` through
`z=thickness+a*x²+b*y²`, with an XY margin around the tracks. The first layer's
nozzle-tip target is 0.2 mm above this analytic top, rather than floating over
the flat bed. This is a **pre-existing substrate assumption**, not a generated
base-manufacturing or support-print toolpath.

The exported STL contains a gridded top, flat bottom and closed side walls.
Triangle winding is outward. For grid spacing dx/dy, the convex quadratic's
piecewise-linear top lies above its analytic surface by at most
`(a*dx²+b*dy²)/4`. The collision check includes this bound. The default
40x24 grid gives 4,096 triangles and a maximum error of 0.00060625 mm.
The substrate must fit the configured bed bounds, and its margin must cover
half the bead width plus the trajectory's position-error bound.

Independent tests require two oppositely directed uses of each mesh edge,
nondegenerate faces, positive signed volume, upward top faces and agreement
with the analytic surface/volume bounds. No actual manufacturing is validated.

## Tool and obstacle primitives

The tool comprises a horizontal tip disk and vertical world-Z capsules above
the tip. Capsule rows contain `name radius lower_center_Z upper_center_Z`,
all offsets relative to the nozzle tip. They represent swept sphere centers
on a vertical segment. The tip disk has finite radius and zero thickness;
a configured positive separation margin is applied to all shapes.

`world_box` obstacles are stationary world-axis-aligned boxes. `bed_box`
obstacles are axis-aligned in the bed-local frame and rotate with the bed.
Their rows contain `name xmin ymin zmin xmax ymax zmax`. Default boxes stand
for an illustrative gantry keep-out and clamp; they are not a complete printer.

For a world obstacle, the box is expanded by the capsule radius/margin in XY
and by the capsule's lower/upper offsets in Z. A slab intersection test checks
the entire linear tip segment against the expanded box. Sphere-to-box corner
geometry is conservatively overestimated. This detects mid-segment obstacles
even when both tip endpoints are clear.

## Bed-frame bounds across pose changes

The coupon provides a bound e between the ideal curved world-tip path and the
actual chord. At each intermediate pose, inverse rotation preserves that norm,
so the actual bed-local tip lies within e of the ideal bed-local point.
The ideal XY components are affine. Local Z is affine for nondeposition moves;
for deposition its quadratic minimum is conservatively bounded using the
nearest-to-zero X/Y values over the interval.

For bed slopes `(sx,sy)`, inverse rotation maps world +Z to
`(sx,sy,1)/sqrt(1+sx²+sy²)`. A capsule with center offsets h0..h1 therefore lies
inside an interval box obtained by expanding ideal XY by e, `h1*max|sx|`,
`h1*max|sy|` and radius/margin. Its local Z lower bound is the ideal minimum
minus e, plus `h0/sqrt(1+max|slope|²)`, minus radius/margin. A horizontal tip
disk's local Z variation is at most `radius*|slope|/sqrt(1+|slope|²)`.

The interval box is checked against the bed plane and bed-fixed boxes. Its
intersection with the substrate footprint gives a conservative upper surface
height for the convex quadratic. No tool sphere can reach that surface when
the box's lower Z exceeds it. Capsule bodies are also checked against an
overestimate of the complete rectangular-bead coupon envelope, including future
layers, bead-width/slope allowance and trajectory error. The tip disk is checked
against the substrate and fixtures, then against chronological bounded bead
cells. [MATERIAL_MODEL.md](MATERIAL_MODEL.md) defines its narrow fresh-track and
lift-off contact exceptions. Arbitrary infill joins and actual bead physics
remain outside that model.

Overlapping bounds are rejected as unresolved possible collisions; geometry is
never silently clamped and no endpoint-only fallback is used. These conservative
boxes can reject clear paths. They are engineering bounds with floating-point
margins, not a formally verified interval-arithmetic implementation.

## Scope and output

All scene checks complete before any STL is created. Existing output files
are preserved. A failed or indeterminate check exits nonzero and emits no
success report or new geometry file. The result distinguishes
`scene_collision_checked` and `substrate_geometry_generated` from
`substrate_manufacturing_planned` and `printer_safe`, which remain false.
`tip_vs_prior_tracks_checked` is true only after the chronological coupon
material check also passes.

Arbitrary printer meshes, real tool geometry, linkage/gantry dynamics,
general deposited-material contacts, homing, substrate manufacture,
acceleration, firmware synchronization and physical commissioning remain open.
