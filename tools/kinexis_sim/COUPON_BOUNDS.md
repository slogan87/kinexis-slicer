# Bounds for the quadratic coupon

These bounds apply to this simulator's ideal sliding-support bed and linear
machine-axis interpolation. They are not collision certificates for a real
printer. Floating-point comparisons in tests allow small numerical residuals.

## Geometry and orientation

The bed-local surface is `f(x,y)=a*x²+b*y²`, with `a,b>=0`. Layers use vertical
offset `k*h`, plus the optional constant substrate thickness. The surface normal is proportional to `(-2ax,-2by,1)`.
The bed slopes are `s=(-2ax,-2by)`, so the simulator's shortest rotation sends
that surface normal to world +Z. Pitch/roll are plane-slope angles, not Euler
angles. No orientation is clamped.

For one unit-parameter segment, XY and bed lift are affine. Bed slope is also
affine, so the three actuator commands are affine. A controller interpolating
the actuator endpoints therefore reconstructs exactly the intended intermediate
bed plane. The world nozzle-tip path `W(t)=R(s(t))*p(t)+lift(t)*Z` is generally
curved; the controller instead follows its straight chord.

Let `D=|s'|` and require `|s|<=1`. Put `c=1/sqrt(1+|s|²)`,
`v=(s_y,-s_x,0)*c`, `V=cross_matrix(v)`, and `q=1/(1+c)`.
The rotation is `R=I+V+q*V²`. Conservative operator-norm derivative bounds are:

```text
|c'| <= D;       |c''| <= 4D²
|v| <= 1;       |v'| <= 2D;       |v''| <= 6D²
|q| <= 1;       |q'| <= D;        |q''| <= 6D²
||R'|| <= 7D;   ||R''|| <= 40D²
```

For bed-local position (relative to the pivot) bounded by `P`, with first and
second derivative norms bounded by `P1,P2`, this gives
`max|W''| <= 40D²*P + 14D*P1 + P2`. The vector chord error over the unit interval
is at most one eighth of this value. A convex quadratic layer has its maximum
Z at a segment endpoint; componentwise endpoint extents therefore bound `P`.
For deposition, `P1 <= XY_length*sqrt(1+max|gradient|²)` and
`P2=2a*dx²+2b*dy²`. For other moves the local point is affine and `P2=0`.
Affine bed lift adds no second derivative.

The generator subdivides until this bound meets the requested positional
tolerance. It also bounds slope change per segment and the induced surface
normal alignment error at the *actual* interpolated point. The height-map
gradient has Lipschitz constant `H=2*max(a,b)`; a conservative angular bound is
`2H*position_error` radians. Point/depth exhaustion rejects generation.

## Extrusion

The stated deposition model uses a rectangular bead of surface width `w` and
local normal layer spacing `h/sqrt(1+|gradient|²)`, derived from the vertical
spacing of adjacent tangent planes. For planar XY displacement `d` of length
`L`, volume density on the unit interval is

```text
w*h * sqrt(L² + (gradient dot d)²) / sqrt(1+|gradient|²).
```

This is a **local tangent approximation** to finite curved-layer spacing.
Real bead shape, pressure, shrinkage and physical deposition remain unvalidated.
It neither uses a warp Jacobian alone nor blindly scales E by world path length
(which would count bed motion). Filament length is volume divided by circular
filament area. Retraction/recovery are separate E moves; final parking retains
one retraction and is excluded from deposited volume.

With `|gradient|<=1` and gradient change `D`, the density's Lipschitz constant
is at most `w*h*L*D*(1+sqrt(2))`. Midpoint integration error is at most one
quarter of that. Accepted deposition segments meet
`error_bound <= volume_error_per_mm*L`; their bounds are summed in the report.
The flat-surface limit gives exact planar volume. An independent high-resolution
Simpson integral tests the nonzero-curvature case against the reported bound.

## Travel and limits

Between tracks: retract, lift vertically to a fixed bed-local clearance plane,
travel with the required bed pose, descend, recover E. Initial/final bed-pose
changes also occur at that plane. Vertical approach/lift paths have no lateral
motion relative to the part and do not claim clearance above its full envelope.

For cleared travel and pose changes, the inverse bed rotation preserves the
position-error norm. Local Z is bounded below by the smaller ideal endpoint Z
minus the chord error; absolute XY is bounded above by endpoint extents plus
that error. The convex height map and highest coupon layer then give a
conservative **vertical point-clearance** lower bound, compared with the
configured margin. This omits the nozzle/tool body, fixtures and gantry.

Machine XYZ and actuator commands are linear between accepted endpoints, so
their box limits and component speeds are bounded by those endpoints and the
chosen duration. The scheduler lengthens each segment for XYZ/actuator/E speed
limits, with a 1% timing margin. Acceleration, jerk, compliance and firmware
synchronization remain unmodeled. The final full simulator replay is mandatory.
