# Builds the KenshiPure client (build\client\KenshiPure.dll and the Ogre plugin that starts it,
# KenshiPureLoader.dll) with the VC++ 2010 x64 toolset from scripts\setup-build.ps1, then checks them.
#   1. client\sites.tsv changed since its last proof -> scripts\sites.mjs check re-proves every row in
#      both Kenshi exes (needs the game folder; -GameDir, else KENSHI_DIR, else Steam's default).
#   2. scripts\check-fields.mjs: no raw access to game fields (only KP_FIELD).
#   3. scripts\sites.mjs gen -> build\client\gen\ (table, field ids, ml64 jump slots), compile, link.
#   4. Both DLLs import Kenshi's C runtime (MSVCR100.dll) and nothing newer, not KenshiLib.dll;
#      KenshiPure.dll exports kpStart (not RE_Kenshi's startPlugin), the loader dllStartPlugin/dllStopPlugin.
#   5. build\client\sites-test.exe runs the run-time resolver (client\Sites.cpp) on both exe files
#      (read-only, nothing is run) and must agree with sites.tsv in known and unknown-build mode.
#
#   powershell -ExecutionPolicy Bypass -File scripts\build-plugin.ps1 [-GameDir "<Kenshi folder>"] [-SkipSiteCheck]
param([string]$GameDir = "", [switch]$SkipSiteCheck)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Toolchain.ps1")
Use-Toolchain

$src = Join-Path $RepoRoot "client"
$out = Join-Path $RepoRoot "build\client"
$obj = Join-Path $out "obj"
$gen = Join-Path $out "gen"
$node = Get-ToolNode
if (-not $GameDir) {
    $GameDir = if ($env:KENSHI_DIR) { $env:KENSHI_DIR } else { Join-Path ${env:ProgramFiles(x86)} "Steam\steamapps\common\Kenshi" }
}
if (Test-Path -LiteralPath $obj) { Remove-Item -Recurse -Force -LiteralPath $obj }
New-Item -ItemType Directory -Force $obj, $gen | Out-Null

# 1. Re-prove the address table when it changed.
$tsv = Join-Path $src "sites.tsv"
$stamp = Join-Path $out "sites.tsv.proven"
$tsvHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $tsv).Hash
$proven = (Test-Path -LiteralPath $stamp) -and ((Get-Content -Raw -LiteralPath $stamp).Trim() -eq $tsvHash)
if (-not $proven) {
    if ($SkipSiteCheck) { Write-Host "sites.tsv changed and was NOT re-proven (-SkipSiteCheck)" }
    else {
        & $node (Join-Path $RepoRoot "scripts\sites.mjs") check --game $GameDir | Select-Object -Last 3
        if ($LASTEXITCODE -ne 0) { throw "sites.tsv: rows not proven in both exes (scripts\sites.mjs check)" }
        Set-Content -LiteralPath $stamp -Value $tsvHash -Encoding ASCII
    }
}

# 2. Raw game-field access check.
& $node (Join-Path $RepoRoot "scripts\check-fields.mjs")
if ($LASTEXITCODE -ne 0) { throw "raw game-field access in client\ (use KP_FIELD)" }

# 3. Generate, compile, assemble, link.
& $node (Join-Path $RepoRoot "scripts\sites.mjs") gen $gen | Out-Null
if ($LASTEXITCODE -ne 0) { throw "sites.mjs gen failed" }

$common = @("/nologo", "/c", "/O2", "/DNDEBUG", "/DUNICODE", "/D_UNICODE", "/MD", "/W3", "/Z7", "/I$src", "/I$gen")

# Compiles files into obj\<name>\ (each output separately, so a file may be part of several).
function Invoke-Compile([string]$name, [string[]]$files, [string[]]$extra = @()) {
    $dir = Join-Path $obj $name
    New-Item -ItemType Directory -Force $dir | Out-Null
    & $Tc.Cl @($common + $extra + @("/Fo$dir\") + $files) | Out-Host   # keep cl's output out of the return value
    if ($LASTEXITCODE -ne 0) { throw "compile of $name failed ($LASTEXITCODE)" }
    return @(Get-ChildItem -LiteralPath $dir -Filter *.obj | ForEach-Object { $_.FullName })
}
function Invoke-Link([string]$outFile, [string[]]$objs, [string[]]$libs, [string[]]$extra = @()) {
    $pdb = [IO.Path]::ChangeExtension($outFile, ".pdb")
    & $Tc.Link @(@("/nologo", "/OUT:$outFile", "/MANIFEST:NO", "/DEBUG", "/OPT:REF", "/OPT:ICF", "/PDB:$pdb") + $extra + $objs + $libs)
    if ($LASTEXITCODE -ne 0) { Stop-ToolchainHelpers; throw "link of $outFile failed ($LASTEXITCODE)" }
}

# /MD: Kenshi's own CRT (game calls hand std::string across). /EHa: __try beside C++.
$clientSrc = @(Get-ChildItem -LiteralPath $src -Filter *.cpp | ForEach-Object { $_.FullName }) + @(Join-Path $gen "kp_sites_gen.cpp")
$clientObjs = Invoke-Compile "client" $clientSrc @("/EHa", "/wd4482")
$mhObjs = Invoke-Compile "minhook" (Get-MinHookSources)
$thunks = Join-Path $obj "kp_thunks.obj"
& $Tc.Ml64 /nologo /c "/Fo$thunks" (Join-Path $gen "kp_thunks.asm") | Out-Null
if ($LASTEXITCODE -ne 0) { throw "ml64 failed ($LASTEXITCODE)" }

$dll = Join-Path $out "KenshiPure.dll"
Invoke-Link $dll ($clientObjs + $mhObjs + $thunks) @("bcrypt.lib", "user32.lib", "ws2_32.lib") @("/DLL")

# The loader: its own files plus the client's log and kenshi.cfg reader.
$loaderDir = Join-Path $RepoRoot "loader"
$loaderSrc = @(Get-ChildItem -LiteralPath $loaderDir -Filter *.cpp | ForEach-Object { $_.FullName }) +
             @((Join-Path $src "Log.cpp"), (Join-Path $src "Display.cpp"))
$loaderObjs = Invoke-Compile "loader" $loaderSrc @("/EHsc", "/I$loaderDir")
$loader = Join-Path $out "KenshiPureLoader.dll"
Invoke-Link $loader ($loaderObjs + $mhObjs) @("bcrypt.lib", "user32.lib") @("/DLL")

# The resolver test: client\Sites.cpp against the exe files.
$testObjs = Invoke-Compile "sites-test" @((Join-Path $src "test\SitesTest.cpp"), (Join-Path $src "Sites.cpp"),
                                          (Join-Path $src "Log.cpp"), (Join-Path $gen "kp_sites_gen.cpp")) @("/EHa", "/wd4482")
$testExe = Join-Path $out "sites-test.exe"
Invoke-Link $testExe ($testObjs + $thunks) @("user32.lib") @("/SUBSYSTEM:CONSOLE")

# 4. Checks on the results.
function Get-Dump([string]$file, [string]$what) { return (& $Tc.Dumpbin /nologo $what $file) -join "`n" }
$problems = @()
foreach ($f in @($dll, $loader)) {
    $imports = Get-Dump $f "/imports"
    $n = Split-Path $f -Leaf
    if ($imports -notmatch "MSVCR100\.dll") { $problems += "$n does not import MSVCR100.dll (wrong CRT)" }
    if ($imports -match "MSVCR1[1-9]0|VCRUNTIME|ucrtbase") { $problems += "$n imports a newer CRT" }
    if ($imports -match "KenshiLib\.dll") { $problems += "$n imports KenshiLib.dll" }
}
$exp = Get-Dump $dll "/exports"
if ($exp -notmatch "\bkpStart\b") { $problems += "KenshiPure.dll does not export kpStart" }
if ($exp -match "startPlugin") { $problems += "KenshiPure.dll still exports RE_Kenshi's startPlugin" }
$lexp = Get-Dump $loader "/exports"
foreach ($e in @("dllStartPlugin", "dllStopPlugin")) { if ($lexp -notmatch "\b$e\b") { $problems += "KenshiPureLoader.dll does not export $e" } }
Stop-ToolchainHelpers  # link and dumpbin leave mspdbsrv.exe running
if ($problems.Count -gt 0) { throw ("build check failed: " + ($problems -join "; ")) }

# 5. Resolver test against both exes, when they are on this PC.
$e65 = Join-Path $GameDir "RE_Kenshi\Kenshi_x64.exe"
$e68 = Join-Path $GameDir "kenshi_x64.exe"
if ((Test-Path -LiteralPath $e65) -and (Test-Path -LiteralPath $e68)) {
    & $testExe $e65 $e68
    if ($LASTEXITCODE -ne 0) { throw "sites-test failed: the run-time resolver disagrees with sites.tsv" }
} else { Write-Host "sites-test skipped: Kenshi exes not found in $GameDir" }

foreach ($f in @($dll, $loader)) {
    Write-Host "Built $f"
    Write-Host "  sha256 $((Get-FileHash -Algorithm SHA256 -LiteralPath $f).Hash.ToLower())"
}
Write-Host "Checks: sites.tsv proven, no raw game fields, MSVCR100.dll only, no KenshiLib.dll, entry points exported, resolver test"
