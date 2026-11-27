# Builds the KenshiPure server (kp-server), test bot (kp-bot) and their tests into build\net\
# (gitignored), then runs the tests. Needs the toolchain from scripts\setup-modern.ps1.
#
#   powershell -ExecutionPolicy Bypass -File scripts\build-net.ps1            # build + test
#   powershell -ExecutionPolicy Bypass -File scripts\build-net.ps1 -NoTest    # build only
param([switch]$NoTest, [ValidateSet("Release", "Debug")][string]$Config = "Release")
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Modern.ps1")
Use-Modern

$src = Join-Path $RepoRoot "net"
$out = Join-Path $Mt.Build $Config
& $Mt.CMake -S $src -B $out -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DCMAKE_MAKE_PROGRAM=$($Mt.Ninja)" `
    "-DCMAKE_C_COMPILER=cl" "-DCMAKE_CXX_COMPILER=cl" "-DENET_DIR=$($Mt.Enet)" | Out-Host
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
& $Mt.CMake --build $out | Out-Host
if ($LASTEXITCODE -ne 0) { throw "build failed" }
Write-Host "Built: $out\kp-server.exe, $out\kp-bot.exe"
if ($NoTest) { return }
$ctest = Join-Path (Split-Path $Mt.CMake) "ctest.exe"
& $ctest --test-dir $out --output-on-failure | Out-Host
if ($LASTEXITCODE -ne 0) { throw "tests failed" }
