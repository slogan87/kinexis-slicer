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
- Fork `prusa3d/PrusaSlicer` into a **new repo** (`kinexis-slicer`), keep full
  git history, add `upstream` remote for periodic rebases.
- Get it building on your platform via Prusa's dep bundles / `build_linux.sh`
  (Boost, TBB, CGAL, Eigen, wxWidgets, OpenVDB, …). This alone is a real task.
- CI that builds the fork. Brand as Kinexis (later; don't churn early).
- **Rule:** keep every change modular and clearly fenced (`// KINEXIS:` blocks or
  separate files) so you can keep merging upstream. Never scatter edits.

### Phase 1 — 3D + orientation output plumbing (de-risk the machine first)
Before any non-planar math, prove you can drive the machine.
- Extend `GCodeWriter` to emit **X Y Z A B** (or your rotary axis names) and add a
  `GCodeConfig`-driven kinematic/post-transform hook.
- Add a **machine-kinematics** module (port from Kinexis): tilting bed (3-point),
  trunnion (rotary bed), tilting toolhead (with tool-length compensation). Given a
  desired nozzle position + tilt, produce axis values; flag unreachable poses.
- Round-trip test: slice a normal flat part, run it through the new 3D/orientation
  path with **zero tilt**, confirm byte-for-byte-equivalent motion. This proves
  the plumbing without changing results.
- Output target: Klipper 5-axis (study Open5x's kinematics), plus a generic
  post-processor. Klipper is the pragmatic motion base.

### Phase 2 — Deformation-based non-planar (Approach A, the big win)
- Define the **deformation field** φ: start with a height-map warp
  `z' = z + h(x,y)` (raise/curve layers over a base surface), then generalize to a
  full 3D field. Must be smooth, invertible over the build region, and
  orientation-computable (need ∇φ for the surface normal → nozzle tilt).
- **Pre-deform** the mesh by φ⁻¹ before handing it to `PrintObject::slice()`.
- At **G-code export**, for each extrusion point `(x, y, print_z)`:
  1. `(X,Y,Z) = φ(x,y,print_z)` — the real curved position.
  2. Nozzle orientation = normal of the deformed layer surface at that point
     (from ∇φ) → `(A,B)`; clamp to machine limits and branch-angle style rules.
  3. **Flow compensation:** scale extrusion `E` by the local Jacobian
     `det(∂φ)` so layer stretch/compression under the warp keeps consistent
     deposition (critical for print quality — non-planar work lives or dies here).
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
- **Hardware/firmware loop:** you need a real 5-axis machine + Klipper kinematics
  to validate; simulation only goes so far.

## 5. Immediate next steps
1. Fork PrusaSlicer, get it building locally + in CI (Phase 0).
2. Stand up the kinematics + 5-axis `GCodeWriter` and prove the zero-tilt
   round-trip (Phase 1) — this de-risks everything downstream.
3. Prototype the simplest height-map warp end-to-end on one test part (Phase 2 MVP):
   pre-deform → Prusa slice → warp toolpaths at export → Klipper 5-axis → print.
4. Only then generalize the field, add collision, and tackle native curved slicing.

---

*This plan lives in the Kinexis (Rust) repo as the bridge document; carry it into
the new PrusaSlicer fork and evolve it there.*
