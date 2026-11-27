# Planar-to-curved sparse coupon

Validated in software on 2026-11-28. This is a bounded diagnostic fixture,
not a general slicer export path or a commissioned printer job.

`fixtures/transition-coupon.cfg` starts on a flat 2 mm substrate and prints
24 layers, five straight tracks per layer. For transition length T=24,
beta(k)=min(1,(k-1)/(T-1)) for printed layers k>=1; beta(0)=0.
The surface is base+k*h+beta(k)*(a*x*x+b*y*y). Thus the first layer is flat,
and the final layer reaches the requested curvature. Legacy fixtures without
`transition_layers` retain beta=1, including their curved substrate.

Vertical spacing is g=h+(beta(k)-beta(k-1))*f(x,y). Extrusion uses this
variable spacing together with the current surface gradient, rather than
assuming a constant layer dose. Explicit sample layer metadata is mandatory
for transitions. Invalid schedules, exhausted budgets and unresolved bounds
reject the job.

The spacing check expands the footprint by w/2+g_max*S+position_tolerance.
For the expanded gradient bound S_ext and curvature A=max(a,b), it adds
2*((1+S_ext)*position_tolerance+A*footprint^2) for two-layer interpolation
and tangent residuals. The manufactured first interface is checked separately
against actual planar sliced G-code. Configured spacing limits are 0.15–0.25 mm.

The fixture produces 40,778 samples and 120 tracks. Planned interlayer bounds
are 0.177368–0.247342 mm; modeled extrusion is 199.459725 mm³ with a summed
integration error bound of 0.082490 mm³. The flat substrate fit passes with
modeled first-interface gaps 0.195–0.220728 mm. Scene preflight checks all
intervals and chronological deposited material; raw coupon generation alone
does not check collisions.

`check_transition.py` independently checks the schedule, normals, intermediate
actuator interpolation, speed limits and Simpson-integrated dose. It also
requires scene/history and actual planar G-code fit checks. Optional native
integration regenerates and slices the flat substrate with PrusaSlicer.

Run the existing coupon/scene/fit commands using `transition-coupon.cfg` in
place of `supported-coupon.cfg`, with the flat substrate fit configuration.
The scene material budget is explicitly 10 million candidate operations.

The sparse-track rectangular-bead model and virtual sliding-support bed remain
assumptions. General infill joins, thermal effects, real linkage calibration,
physical registration and nonzero-tilt firmware output are not validated.
`printer_safe` remains false. Software comes first, then the built Duet/RRF
machine; Klipper remains last.
