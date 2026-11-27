# Kinexis Windows migration baseline

The starting revision is `d9f1e73a32809fc21e481b8cfd8118ceedbe401e`, based on
PrusaSlicer 2.9.6. This branch prepares Phase 0; it adds no non-planar behavior.

## Build and verification

Use Windows x64, Visual Studio 2022 with C++ desktop tools, **C++ ATL for latest
v143 build tools (x86 & x64)**, and a Windows SDK,
Git, and **CMake 3.31.6**. The dependency recipes predate CMake 4; pinning the
tool avoids changing upstream dependency code simply to prepare a baseline.
CI installs the pinned CMake package with Python pip.
The build script initializes Visual Studio's x64 developer environment itself,
so Windows SDK libraries are found from a normal PowerShell session too.
If `atlbase.h` is missing, add `Microsoft.VisualStudio.Component.VC.ATL` through
Visual Studio Installer > Modify > Individual components. The script checks for
it before compiling. Installing this component requires Windows administrator
approval; it is not installed automatically by the build script.
Alternatively, pass `-AtlRoot C:/path/to/atlmfc` with matching Microsoft ATL
headers and x64 libraries from your Visual Studio toolset. That folder must
contain `include/atlbase.h` and `lib/x64/atls.lib`. Include/library search paths
are changed only for the invoked build commands and restored afterwards. Keep
SDK files outside version control; the script does not download or redistribute
them. This route supports workspace-local dependencies without changing the
Visual Studio installation.

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
The application configuration is Release-only, matching the dependency bundle;
requesting Debug as well would require a second OpenVDB/Boost dependency build.
CI caches completed dependency bundles by compiler version and build inputs,
before compiling the app, so later app failures do not discard that setup work.
It must run successfully on the pushed revision before Phase 0 is complete.
The inherited Windows/macOS/Flatpak automation references an inaccessible Prusa
reusable-workflow repository. Those templates are manual-only, and the inherited
analysis schedule is disabled. They are retained for reference; they are not
working Kinexis CI. The fork-owned Windows baseline is the automatic build.

After build/tests pass, launch the generated GUI executable under
`build/kinexis-baseline/src/Release`, load a known STL, slice, and export a
normal planar job. Record the revision, tool versions, test results, executable
path, and visual result. A successful configure is not a successful build.

## Verification record (2026-11-24)

Local configuration used CMake 3.31.6, Visual Studio 2022 17.14, MSVC
14.44.35207, and Windows SDK 10.0.26100.0. The Release dependency bundle built
successfully, and the complete application/test build returned exit code zero.
The generated console executable started and exported
`test/overhang_lbracket.stl` from the Rust reference repository as planar G-code
with exit code zero. Its unsupported-bridge warning is expected for this fixture
with default supports disabled. This is a startup/export smoke test, not a
zero-tilt comparison or a physical-print validation.

GUI launch was attempted with an isolated workspace data directory. Visual
verification remains open: the Windows automation helper timed out on
`list_windows`, including after a kernel reset. Do not mark the GUI gate passed
from the CLI result.

All **7 CTest suites passed** locally (143.34 seconds, zero failures). The
fork-owned Windows workflow also passed on pushed commit
`549e65fd5c26acdd34d5362b6cfa6edadcf12e60`:
[run 45322490518](https://github.com/slogan87/kinexis-slicer/actions/runs/45322490518).
The optional workspace-local `-AtlRoot` path was verified by the successful
local application/test build and CTest run; that option was added after the
referenced CI revision. GUI verification still keeps the full Phase 0 gate open.

## Next gate

Update (2026-11-26): the final foundation follow-up commit
`de3458760f99f4c8a14d397f6dec4a51d418e3ac` passed
[CI run 45332556022](https://github.com/slogan87/kinexis-slicer/actions/runs/45332556022).
The GUI retry displayed the loaded L-bracket and configuration wizard, but
input failed with `GetCursorPos failed: Access is denied. (0x80070005)`.
Startup/model loading were observed; GUI slice/export remains unverified.

The first standalone C++ simulation is in
[`tools/kinexis_sim`](../tools/kinexis_sim/README.md). Its identity checks cover
the simulator representation, not PrusaSlicer's G-code pipeline. Its ideal
sliding-support fixture is not a physical machine profile.

Simulation commit `4e4c81647f171575e42b306b583786ba72634c54` passed
[Windows and Ubuntu CI](https://github.com/slogan87/kinexis-slicer/actions/runs/45694082655).
Its local Windows build passed all three CTest groups, including 225 C++ checks
and the command-line rejection suite. The parent PrusaSlicer GUI pipeline was
not changed by this standalone harness.

Planar adapter update (2026-11-27): local verification now passes five CTest
groups, including 246 C++ checks, modal G-code regression tests, and a fresh
PrusaSlicer CLI export. The 10,009-move L-bracket export is byte-identical after
both disabled and zero-tilt replay. Homing is counted but its path is explicitly
excluded from validation. This tests the external adapter; it adds no nonzero
tilt export to the PrusaSlicer GUI. The Windows baseline workflow now includes
fresh-export replay after the native app/tests are built.

Validation order is confirmed: software simulation first, Duet/RRF when the
three-actuator machine is built, then Klipper last. Record explicit simulator
fixtures and later the physical machine's axis names/signs, pivot, tool length,
and limits. Do not treat simulated measurements as commissioned hardware data.
Prove disabled and zero-tilt output equivalence on fixed fixtures
before enabling any nonzero tilt. Collision and reachability failures must
prevent export. Travel, wipes, retraction, and pose transitions need validation
alongside extrusion paths.
