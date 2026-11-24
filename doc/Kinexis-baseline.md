# Kinexis Windows migration baseline

The starting revision is `d9f1e73a32809fc21e481b8cfd8118ceedbe401e`, based on
PrusaSlicer 2.9.6. This branch prepares Phase 0; it adds no non-planar behavior.

## Build and verification

Use Windows x64, Visual Studio 2022 with C++ desktop tools and a Windows SDK,
Git, and **CMake 3.31.6**. The dependency recipes predate CMake 4; pinning the
tool avoids changing upstream dependency code simply to prepare a baseline.
CI installs the pinned CMake package with Python pip.

```powershell
python -m pip install cmake==3.31.6
./scripts/kinexis-build.ps1 -Jobs 2
```

For a portable CMake installation, pass `-CMake C:/path/to/cmake.exe`.
The script builds Release dependencies, configures the GUI and unit tests,
builds the app, and runs CTest with failure output and JUnit results. The first
baseline targets STL input and excludes optional STEP/OpenCascade support.
Pass `-WithStep` consistently to every stage to include that CAD import stack.
Eigen's own tests/documentation are disabled; slicer unit tests stay enabled. Each
native command's exit code is checked. Individual stages can be resumed using
`-Stage ConfigureDeps`, `Deps`, `Configure`, `Build`, or `Test`.
The job limit also caps MSVC `/MP` fan-out through a temporary `_CL_` value;
the original environment value is restored after each command. This prevents
dependency projects from silently spawning a compiler per logical CPU.
Keep dependency downloads enabled for a fresh build. Build directories are
`deps/build-kinexis` and `build/kinexis-baseline` and are Git-ignored.

The fork-owned `Kinexis Windows baseline` workflow uses the same script. It is
independent of Prusa's reusable workflows and needs no repository secrets.
It must run successfully on the pushed revision before Phase 0 is complete.
The inherited Windows/macOS/Flatpak automation references an inaccessible Prusa
reusable-workflow repository. Those templates are manual-only, and the inherited
analysis schedule is disabled. They are retained for reference; they are not
working Kinexis CI. The fork-owned Windows baseline is the automatic build.

After build/tests pass, launch the generated GUI executable under
`build/kinexis-baseline/src/Release`, load a known STL, slice, and export a
normal planar job. Record the revision, tool versions, test results, executable
path, and visual result. A successful configure is not a successful build.

## Next gate

Validation order is confirmed: software simulation first, Duet/RRF when the
three-actuator machine is built, then Klipper last. Record explicit simulator
fixtures and later the physical machine's axis names/signs, pivot, tool length,
and limits. Do not treat simulated measurements as commissioned hardware data.
Prove disabled and zero-tilt output equivalence on fixed fixtures
before enabling any nonzero tilt. Collision and reachability failures must
prevent export. Travel, wipes, retraction, and pose transitions need validation
alongside extrusion paths.
