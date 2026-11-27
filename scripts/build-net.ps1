# Builds the KenshiPure server (kp-server), test bot (kp-bot), the client's network core
# (KenshiPureCore.dll, installed next to the plugin by scripts\install-client.ps1) and their tests
# into build\net\<Config>\ (gitignored), checks the core DLL, then runs the tests (kp-core-test takes
# ~1 min: it holds a 60 s stall). Needs the toolchain from scripts\setup-modern.ps1.
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
Write-Host "Built: $out\kp-server.exe, $out\kp-bot.exe, $out\KenshiPureCore.dll"

# The core DLL: one export, static CRT (no VC++ runtime DLL needed and none shared with the plugin).
$dumpbin = Join-Path (Split-Path $Mt.Cl) "dumpbin.exe"
$coreDll = Join-Path $out "KenshiPureCore.dll"
$exports = (& $dumpbin /nologo /exports $coreDll) -join "`n"
$imports = (& $dumpbin /nologo /imports $coreDll) -join "`n"
$problems = @()
if ($exports -notmatch "\bkpCoreGetApi\b") { $problems += "does not export kpCoreGetApi" }
if (([regex]::Matches($exports, "(?m)^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]{8}\s+\S+")).Count -ne 1) { $problems += "exports more than kpCoreGetApi" }
if ($imports -match "(?i)MSVCR|MSVCP|VCRUNTIME|ucrtbase|api-ms-win-crt") { $problems += "imports a C runtime DLL (must be static)" }
if ($problems.Count -gt 0) { throw ("KenshiPureCore.dll check failed: " + ($problems -join "; ")) }
Write-Host "Checks: KenshiPureCore.dll exports only kpCoreGetApi, static CRT"
if ($NoTest) { return }
$ctest = Join-Path (Split-Path $Mt.CMake) "ctest.exe"
& $ctest --test-dir $out --output-on-failure | Out-Host
if ($LASTEXITCODE -ne 0) { throw "tests failed" }
