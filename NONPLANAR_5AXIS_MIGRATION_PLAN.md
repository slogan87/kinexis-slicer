# Kinexis → PrusaSlicer fork: continuous 5-axis non-planar migration plan

**Goal:** A best-in-class, fully open-source slicer that does **true continuous
non-planar 5-axis** FDM — curved deposition layers with the nozzle tilting
continuously along each path — by forking PrusaSlicer and inheriting its mature
toolpath quality, rather than reimplementing a slicer from scratch.

**License:** PrusaSlicer is AGPL-3.0. Kinexis is open source forever, so this is
fully compatible and in the spirit of the project. Keep the fork AGPL, publish
all changes.

**Inspiration:** Open5x (open 5-axis non-planar FDM) for the toolpath/tilt and
Klipper-based motion ideas — but bring it *inside a real slicer* so it's a
general tool, not a CAD-scripted pipeline. Also study RotBot (ZHAW, 4-axis
non-planar) and the S³-Slicer paper (support-reducing non-planar).

---

## 0. The core idea (read this first)

### Execution status (2026-11-24)

This is the authoritative migration plan; `ROADMAP.md` is the historical Rust
prototype record. The existing fork is
[slogan87/kinexis-slicer](https://github.com/slogan87/kinexis-slicer), with full
PrusaSlicer history. Do not create another fork. Its inspected default-branch
revision is `d9f1e73a32809fc21e481b8cfd8118ceedbe401e` (PrusaSlicer 2.9.6
plus migration documents and branding). No Actions runs were returned when
inspected. A workflow file alone is not a verified CI build.

**Confirmed validation order:** software simulation first (the physical machine
has not been built), then the Duet/RRF three-actuator tilting bed. Klipper is
last priority; it is not the primary firmware target. Hardware acceptance stays
open until the Duet machine exists and has been commissioned.

The Rust prototype supplies algorithms and fixtures, not validated continuous
5-axis motion. Its nozzle collision warning is approximate; the radius argument
is currently unused. Reachability checks and negative-Z/non-finite detection
do not prove collision freedom. Rust export currently writes detected dangerous
moves with a warning, so that behavior must not become the fork's acceptance rule.

Acceptance gates, in dependency order:

- [x] Locate the existing fork and identify its baseline revision.
- [ ] Phase 0: reproduce a Windows dependency build, app build, tests, and launch;
      record compiler/CMake versions and the exact commit.
- [x] Phase 0: run fork-owned CI on a pushed revision and record a green run.
- [x] Phase 1: select validation order: simulation, Duet/RRF, then Klipper.
- [ ] Phase 1: document simulated and Duet axis names, signs, pivot, tool
      length, firmware configuration, and travel/tilt limits. Simulator values
      must be explicit fixtures, not assumed final hardware measurements.
- [ ] Phase 1: prove disabled/zero-tilt output equivalence on fixed fixtures;
      validate nonzero poses against independently calculated expected axes.
- [ ] Phase 2: print one height-map test coupon with per-point Z/orientation,
      bounded geometric error, deposition compensation, and safe travel moves.
- [ ] Phase 3: validate collision/reachability across extrusion, travel, and
      pose transitions. Reject invalid export until the user corrects the job.

Do not expand the Rust planar slicer as part of this migration. Preserve its
tests and fixtures as reference material and revalidate each port independently.

**Verified foundation:** Windows dependency and complete app/test builds passed
with CMake 3.31.6, VS 2022 17.14 / MSVC 14.44.35207, and SDK 10.0.26100.0.
All 7 native CTest suites passed locally (143.34 seconds). The console app
exported the L-bracket STL as planar G-code successfully. Fork-owned CI passed
on `549e65fd5c26acdd34d5362b6cfa6edadcf12e60`
([run 45322490518](https://github.com/slogan87/kinexis-slicer/actions/runs/45322490518)).
The local optional ATL path was subsequently verified by the complete build and
tests. GUI visual verification remains open: Windows automation timed out on
window discovery after retries and a helper reset. No simulator, zero-tilt
equivalence check, non-planar port, or physical validation is claimed by these
baseline results. See the fork's `doc/Kinexis-baseline.md` for build instructions.

**Simulation progress (2026-11-26):** the fork now contains a standalone C++17
kinematics harness under `tools/kinexis_sim`, with an explicit virtual bed
fixture, forward/inverse plane reconstruction, nozzle-tip rigid transforms,
and limits/rate checks for extrusion and non-extruding moves. Disabled and
zero-pose identity are tested within its motion representation. This does not
yet establish slicer G-code equivalence. The support model assumes ideal
vertical actuators with sliding contacts; the real Duet linkage and firmware
configuration remain unmeasured. Collision checks, tool solids, 3D motion
preview, and slicer integration are still open. The foundation follow-up CI
passed on `de3458760f99f4c8a14d397f6dec4a51d418e3ac`
([run 45332556022](https://github.com/slogan87/kinexis-slicer/actions/runs/45332556022)).
GUI retry displayed the L-bracket but interaction failed with Windows access
denied; GUI slice/export acceptance remains open.
The simulation commit `4e4c81647f171575e42b306b583786ba72634c54` passed its
Windows and Ubuntu CI jobs
([run 45694082655](https://github.com/slogan87/kinexis-slicer/actions/runs/45694082655)).
Locally, all 3 Windows CTest groups passed, including 225 C++ numerical and
validation checks and the Python command-line rejection tests.

**Planar replay progress (2026-11-27):** `kinexis-gcode-check` now decodes a
restricted, explicit G-code subset into simulator motion, tracking XYZ/E modes,
G92 offsets, retractions and dwells. The real L-bracket export replays 10,009
moves. Disabled and zero-tilt adapter paths preserve the entire source file
byte-for-byte after validation. All five local test groups passed, including
246 C++ checks and a fresh export from the built PrusaSlicer CLI. Standalone CI
uses the checked-in real export; native Windows CI also regenerates it.
This closes the first adapter-level planar regression, not the production
slicer's non-planar integration gate. Homing paths, heater waits, controller
dynamics and collision geometry remain outside the replay model. Unsupported
commands are rejected rather than skipped. Nonzero-tilt G-code generation is
still unimplemented; the adapter report never marks output printer-safe.

**Height-map trajectory progress (2026-11-27):** the standalone
`kinexis-coupon` generator now produces a two-layer quadratic-surface raster
with per-point Z and bed orientation. Its default software fixture has 2,730
samples. Geometry is subdivided using conservative bounds for linear machine
axis interpolation; local-tangent deposition, normal alignment, point travel
clearance and actuator/XYZ/E speed limits are checked. All six local test
groups passed, including independent reconstruction of interior motion and a
separate deposition integral. This is a sparse diagnostic trajectory with no
substrate/support or printable first-layer solution. Tool-body collisions,
controller dynamics, general mesh warping and nonzero-tilt printer output
remain unimplemented. Phase 2's physical coupon acceptance is still open.

**Virtual scene progress (2026-11-27):** `kinexis-scene-check` now places the
trajectory on a matched 2 mm substrate and checks finite tip/nozzle/hotend
primitives against a bed plane, substrate/material envelope, bed-fixed boxes
and world-fixed boxes across complete interpolated intervals. It rejects
uncertain overlaps and deliberate collisions, including mid-segment obstacles.
The default virtual scene passes 2,730 intervals and exports a watertight
4,096-triangle substrate with a 0.00060625 mm surface bound. All seven local
test groups pass. These fixtures are not measured hardware. Substrate
manufacturing, tip contact with all prior printed material, complete printer
geometry and physical commissioning remain open; `printer_safe` stays false.

**Chronological material progress (2026-11-27):** scene preflight now checks
the tip disk against deposited bead cells in time order, with conservative
continuous interval bounds. The default coupon has 10 tracks and 2,560 cells.
Only the active monotone straight track and stationary/vertical lift-off from
its fresh endpoint permit contact. Unrelated crossings, low lateral dragging,
low pose changes, reversals, excess priming and unrecovered extrusion reject.
All eight local test groups pass. This closes the first sparse-coupon
prior-track regression; arbitrary joins, physical bead behaviour, substrate
manufacturing and nonzero-tilt firmware export remain open.
The full native Windows build and fresh-export CI also passed on
`443875bbb07a563b2a6b84e65f24bab15e5090d5`
([run 45960077844](https://github.com/slogan87/kinexis-slicer/actions/runs/45960077844)).

### First deliverable: software simulation for Duet/RRF

The first runnable motion target is a three-actuator-bed simulation, independent
of a connected printer. Use a small fixed STL corpus (square/cube, overhang
L-bracket, and the existing cylinder-head regression). Store explicit simulated
actuator contact points, pivot, axis names, travel limits, and speed limits with
the fixtures. These are test values until physical measurements are available.

The simulation acceptance gate must check:

1. Disabled and zero-tilt transforms preserve the baseline planar motion and
   extrusion, with any permitted metadata differences explicitly excluded.
2. Positive/negative pitch, roll, and combined poses match independently
   calculated actuator heights and a forward/inverse pose round trip.
3. Extrusion, travel, retract/lift, and pose-transition moves remain finite and
   respect configured XYZ/actuator limits and motion rates. Reject bad output.
4. The 3D preview agrees with reconstructed machine motion and exposes the
   unvalidated collision assumptions; it must not label a path machine-safe
   merely because its coordinates are finite and non-negative.
5. RRF command dialect and actuator synchronization are checked against the
   eventual firmware configuration before hardware use. Software success does
   not close the commissioning or physical-print gates.

### Engineering corrections to the initial proposal

The transforms must cover travel moves, retractions, layer changes, wipe paths,
and any arc output, as well as extrusion. Subdivide curved moves to a specified
position/orientation tolerance; warping just segment endpoints is insufficient.

`det(Dφ)` describes local volume change, but is not by itself a complete
deposition rule. Derive extrusion from transformed segment length and deposited
cross-section/layer spacing, and validate conservation on test coupons. A
height-map shear has determinant one even when a surface path length changes.
Do not silently clamp tilt after warping positions: any orientation adjustment
must be checked again for geometry, clearance, and machine feasibility.

PrusaSlicer's entire pipeline assumes **flat layers**: slice the mesh with
horizontal planes, generate 2D toolpaths per layer, emit X/Y moves at a fixed Z.
Fighting that assumption everywhere is how you drown.

The winning architecture keeps Prusa's planar core **untouched** and brackets it
with two transforms:

```
   your mesh ──►  PRE-DEFORM (inverse warp φ⁻¹)  ──►  PrusaSlicer (unmodified,
                                                       flat slicing + perimeters
                                                       + infill + supports)
                                                            │
                                                            ▼
   5-axis G-code  ◄──  POST-DEFORM at G-code export:  every extrusion point
                       (x, y, layer_z) ──► φ(x,y,z) = (X,Y,Z), nozzle tilt
                       (A,B) from the warp's surface normal, flow scaled by the
                       local Jacobian, collision-checked.
```

- A smooth **deformation field φ** maps flat build space to the curved target
  space (and back). Pre-deforming the mesh by φ⁻¹ means that when Prusa slices it
  flat and we warp the toolpaths back by φ, the flat layers become the desired
  **curved** layers and the part comes out the right shape.
- You inherit **all** of Prusa's perimeter/infill/support/seam quality for free.
- The only new core-adjacent code is: the warp, the orientation math, flow
  compensation, 5-axis G-code, and collision/kinematics.

This is **Approach A (deformation)**. It handles a large, useful class of parts
(overhang reduction on shells, conformal surfaces, curved bridging) and gets you
a working, high-quality 5-axis slicer fastest.

**Approach B (native curved-layer slicing)** — slicing against curved surfaces
inside `TriangleMeshSlicer`, generating toolpaths in a surface parameterization —
is the deeper change needed for arbitrary topology where a single warp breaks
down. Build it *after* A, reusing the same 3D+orientation toolpath/G-code layer.

**Invariant to design in from day one:** toolpaths and G-code carry **per-point Z
and per-point orientation**. In Approach A this lives only at the G-code export
stage (ExtrusionPath stays 2D through Prusa's pipeline — far less invasive). In
Approach B it becomes native. Design the export/orientation layer so both feed it.

---

## 1. PrusaSlicer pipeline primer (where to cut)

`libslic3r` is the core (no GUI). Key stages and files:

| Stage | Classes / files | Notes for us |
|---|---|---|
| Model | `Model`, `ModelObject`, `ModelVolume`, `TriangleMesh` (`its` indexed set) | Where we inject the **pre-deform**. |
| Slice to layers | `TriangleMeshSlicer` / `slice_mesh_ex`, `PrintObject::slice()` → `Layer`, `LayerRegion`, `LayerSlices` (ExPolygons at constant Z) | The **planar assumption** lives here. Untouched in A; replaced in B. |
| Perimeters | `PerimeterGenerator`, `Arachne::WallToolPaths` | Keep as-is — this is Prusa's quality. |
| Infill | `Fill*` classes | Keep as-is. |
| Supports | `SupportMaterial`, `TreeSupport`/`OrganicSupport` | Keep as-is (this is what we spent 20 builds failing to match). |
| Seam | `SeamPlacer` | Keep; revisit for non-planar later. |
| Toolpaths | `ExtrusionEntity` → `ExtrusionPath`/`ExtrusionLoop` (2D `Points` + the `Layer`'s `print_z`) | **2D today.** In A we read these and warp at export. |
| G-code | `GCodeGenerator` (formerly `GCode`), `GCodeWriter` | **Primary injection point for A.** Extend the writer with Z-per-point + A/B axes. |
| Config | `PrintConfig`, `ConfigOptionXXX`, profiles | Add non-planar + machine-kinematics options here. |

The single highest-leverage fact: in Approach A you intercept at
**`GCodeGenerator`/`GCodeWriter`**, where 2D extrusion points + the layer's Z are
turned into machine moves. That's where φ, orientation, flow scaling, and the
5-axis output plug in — the rest of Prusa is untouched.

---

## 2. Phased implementation

Honest magnitude: this is a multi-month effort. Phasing keeps each step
independently testable and always leaves you with something that runs.

### Phase 0 — Fork, build, hygiene (foundation)
- Use the existing `slogan87/kinexis-slicer` fork of `prusa3d/PrusaSlicer`, keep
  full git history, and add `upstream` for periodic rebases.
- Get it building on your platform via Prusa's dep bundles / `build_linux.sh`
  (Boost, TBB, CGAL, Eigen, wxWidgets, OpenVDB, …). This alone is a real task.
- CI that builds the fork. Brand as Kinexis (later; don't churn early).
- **Rule:** keep every change modular and clearly fenced (`// KINEXIS:` blocks or
  separate files) so you can keep merging upstream. Never scatter edits.

### Phase 1 — 3D + orientation output plumbing (de-risk the machine first)
Before any non-planar math, prove you can drive the machine.
- Extend `GCodeWriter` with per-point XYZ and a `GCodeConfig`-driven
  kinematic/post-transform hook. First simulate the three-point bed's configured
  linear actuator axes (e.g. U/V/W); A/B words alone do not drive that machine.
- Add a **machine-kinematics** module, starting with the three-point tilting bed
  from Kinexis. Given a desired nozzle position and bed pose, produce axis values
  and reject unreachable poses. Trunnion and tilting-toolhead implementations
  (including tool-length compensation) follow after the first target works.
- Round-trip test: slice a normal flat part, run it through the new 3D/orientation
  path with **zero tilt**, confirm byte-for-byte-equivalent motion. This proves
  the plumbing without changing results.
- Output targets, in order: a deterministic software simulation, Duet/RRF
  three-actuator bed output, then Klipper. Use Open5x as a reference, but do
  not make its firmware choice a prerequisite for Kinexis.

### Phase 2 — Deformation-based non-planar (Approach A, the big win)
- Define the **deformation field** φ: start with a height-map warp
  `z' = z + h(x,y)` (raise/curve layers over a base surface), then generalize to a
  full 3D field. Must be smooth, invertible over the build region, and
  orientation-computable (need ∇φ for the surface normal → nozzle tilt).
- **Pre-deform** the mesh by φ⁻¹ before handing it to `PrintObject::slice()`.
- At **G-code export**, for each extrusion point `(x, y, print_z)`:
  1. `(X,Y,Z) = φ(x,y,print_z)` — the real curved position.
  2. Nozzle orientation = normal of the deformed layer surface at that point
     (from ∇φ) → `(A,B)`; validate against machine limits. Any adjusted
     orientation requires renewed feasibility and clearance checks.
  3. **Flow compensation:** derive extrusion from transformed segment length,
     deposited cross-section, and layer spacing. Use the Jacobian to analyze
     the deformation, but do not use its determinant alone as the flow rule.
  4. Emit the 5-axis move.
- Field authoring: auto-fit φ to reduce overhangs / follow a top surface
  (reuse Kinexis's overhang + top-surface analysis), plus manual control. Partition
  into multiple fields/regions where one global warp can't cover the part.

### Phase 3 — Collision + kinematic feasibility (port Kinexis IP)
- Port Kinexis's **nozzle-vs-part clearance** and **reachability** checks as a
  validation/adjustment pass over the 3D toolpaths: nozzle body, heater block, and
  gantry vs. already-printed material and the tilted part.
- On collision/unreachable: locally reduce tilt, re-order, or fall back to planar
  for that region. Surface warnings in the UI (Kinexis already models these).

### Phase 4 — Native curved-layer slicing (Approach B, hardest cases)
- Where a single deformation can't represent the target (undercuts, high-genus,
  wildly varying curvature): add curved-surface slicing in/next to
  `TriangleMeshSlicer` — iso-surfaces of a scalar field, or successive geodesic
  offsets of a base surface.
- Generate perimeters/infill in a **local surface parameterization**, then lift to
  3D — reusing the Phase 1–2 orientation + G-code layer unchanged.
- This is a genuine core extension; only invest once A is delivering value.

### Phase 5 — UI & preview
- Non-planar **preview** in the GUI: the current layer viewer assumes flat Z
  bands. Add curved-layer / 3D-path rendering and a tilt indicator (you built a
  version of this in Kinexis — same concepts, wxWidgets/OpenGL this time).
- Field-authoring UI, machine setup (kinematics, tool length, limits), feasibility
  warnings.

---

## 3. What to port from Kinexis (nothing is wasted)

The Rust **slicing** code retires (Prusa replaces it), but the 5-axis IP becomes
the spec and gets reimplemented in C++:

| Kinexis module | Fate |
|---|---|
| Planar slicing, perimeters, infill, supports | **Retire** — Prusa does these far better. |
| Tilt-band planner / auto-suggest from overhang & top-surface analysis | **Port** → the field-authoring / overhang-reduction logic in Phase 2. |
| Kinematics: 3-point bed, trunnion, tilting head + tool-length comp | **Port** → Phase 1 kinematics module. |
| Nozzle-collision & reachability checks | **Port** → Phase 3. |
| Machine-profile modeling, feasibility warnings | **Port** → Phase 3/5. |
| Firmware-aware output (RRF/Marlin/Klipper/LinuxCNC) | **Port/adapt** → Phase 1 post-processors (lead with Klipper). |

Treat the Kinexis repo as the **reference design + test cases** for the port.

---

## 4. Risks & open questions
- **Flow compensation under warp** is the make-or-break print-quality detail —
  budget real tuning time and test coupons.
- **Upstream drift:** Prusa moves fast. Modular changes + regular `upstream`
  rebases are non-negotiable or you'll fork off into an unmaintainable island.
- **Collision is genuinely hard** in continuous 5-axis; expect to iterate.
- **Build complexity:** getting a PrusaSlicer fork building is itself a Phase-0
  project; don't underestimate it.
- **Hardware/firmware loop:** simulation is the first gate. Physical print
  validation must wait for the Duet/RRF machine to be built and commissioned;
  Klipper is a later target, not a prerequisite.

## 5. Immediate next steps
1. Use the existing Kinexis PrusaSlicer fork and get it building locally + in CI (Phase 0).
2. Stand up the kinematics + 5-axis `GCodeWriter` and prove the zero-tilt
   round-trip (Phase 1) — this de-risks everything downstream.
3. Prototype the simplest height-map warp end-to-end on one test part (Phase 2 MVP):
   pre-deform → Prusa slice → warp toolpaths at export → software validation;
   print on Duet/RRF once the machine is built and commissioned.
4. Only then generalize the field, add collision, and tackle native curved slicing.

---

*This plan lives in the Kinexis (Rust) repo as the bridge document; carry it into
the new PrusaSlicer fork and evolve it there.*
