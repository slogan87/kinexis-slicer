param(
    [ValidateSet('All', 'ConfigureDeps', 'Deps', 'Configure', 'Build', 'Test')]
    [string]$Stage = 'All',
    [string]$CMake = 'cmake',
    [switch]$WithStep,
    [ValidateRange(1, 32)]
    [int]$Jobs = 2
)

$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path $PSScriptRoot -Parent
$depsBuild = Join-Path $sourceRoot 'deps/build-kinexis'
$appBuild = Join-Path $sourceRoot 'build/kinexis-baseline'
$prefix = Join-Path $depsBuild 'destdir/usr/local'
$CMake = (Get-Command $CMake -ErrorAction Stop).Source
$ctest = Join-Path (Split-Path $CMake -Parent) 'ctest.exe'
$stepOption = if ($WithStep) { 'ON' } else { 'OFF' }
$dependencyExcludes = if ($WithStep) { '' } else { 'OCCT' }

function Invoke-Checked {
    param([string]$Executable, [string[]]$Arguments)
    # Some dependency projects add /MP without a limit. _CL_ is appended by
    # MSVC, so cap compiler fan-out as well as the outer MSBuild job count.
    $savedCompilerTail = $env:_CL_
    try {
        $env:_CL_ = "$savedCompilerTail /MP$Jobs".Trim()
        & $Executable @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Command failed ($LASTEXITCODE): $Executable $($Arguments -join ' ')"
        }
    } finally {
        $env:_CL_ = $savedCompilerTail
    }
}

$versionText = & $CMake --version
if ($LASTEXITCODE -ne 0 -or $versionText[0] -notmatch '^cmake version 3\.31\.6$') {
    throw 'Use CMake 3.31.6 for this baseline. CI installs cmake==3.31.6; pass -CMake with the executable path locally.'
}

# Match upstream's x64 Native Tools prompt, including SDK library discovery.
# Keep the pinned CMake path resolved above when the developer shell changes PATH.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsRoot = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vsRoot) {
    throw 'Visual Studio 2022 C++ tools were not found.'
}
$compilerVersion = (Get-Content (Join-Path $vsRoot 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt')).Trim()
$atlHeader = Join-Path $vsRoot "VC/Tools/MSVC/$compilerVersion/atlmfc/include/atlbase.h"
if ($Stage -ne 'Test' -and -not (Test-Path -LiteralPath $atlHeader)) {
    throw 'Install C++ ATL for latest v143 build tools (x86 & x64) in Visual Studio Installer > Modify > Individual components (Microsoft.VisualStudio.Component.VC.ATL).'
}
& (Join-Path $vsRoot 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vsRoot -Arch amd64 -HostArch amd64 -SkipAutomaticLocation

if ($Stage -in 'All', 'ConfigureDeps') {
    Invoke-Checked $CMake @('-S', (Join-Path $sourceRoot 'deps'), '-B', $depsBuild,
        '-G', 'Visual Studio 17 2022', '-A', 'x64', '-DDEP_DEBUG=OFF', "-DDEP_MAX_THREADS=$Jobs",
        "-DPrusaSlicer_deps_PACKAGE_EXCLUDES=$dependencyExcludes")
}
if ($Stage -in 'All', 'Deps') {
    Invoke-Checked $CMake @('--build', $depsBuild, '--config', 'Release', '--parallel', "$Jobs")
}
if ($Stage -in 'All', 'Configure') {
    Invoke-Checked $CMake @('-S', $sourceRoot, '-B', $appBuild,
        '-G', 'Visual Studio 17 2022', '-A', 'x64', "-DCMAKE_PREFIX_PATH=$prefix",
        '-DCMAKE_CONFIGURATION_TYPES=Release',
        '-DSLIC3R_BUILD_TESTS=ON', '-DSLIC3R_GUI=ON', "-DSLIC3R_ENABLE_FORMAT_STEP=$stepOption")
}
if ($Stage -in 'All', 'Build') {
    Invoke-Checked $CMake @('--build', $appBuild, '--config', 'Release', '--parallel', "$Jobs")
}
if ($Stage -in 'All', 'Test') {
    # The dependency bundle includes runtime DLLs (e.g. GMP/MPFR).
    $savedPath = $env:PATH
    try {
        $env:PATH = "$(Join-Path $prefix 'bin');$savedPath"
        Invoke-Checked $ctest @('--test-dir', $appBuild, '-C', 'Release',
            '--output-on-failure', '--no-tests=error', '--output-junit', (Join-Path $appBuild 'test-results.xml'))
    } finally {
        $env:PATH = $savedPath
    }
}
