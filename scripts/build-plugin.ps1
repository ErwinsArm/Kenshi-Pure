# Builds the KenshiPure client plugin (build\client\KenshiPure.dll) with the VC++ 2010 x64 toolset
# from scripts\setup-build.ps1, then checks the result.
#   1. client\sites.tsv changed since its last proof -> scripts\sites.mjs check re-proves every row in
#      both Kenshi exes (needs the game folder; -GameDir, else KENSHI_DIR, else Steam's default).
#   2. scripts\check-fields.mjs: no raw access to game fields (only KP_FIELD).
#   3. scripts\sites.mjs gen -> build\client\gen\ (table, field ids, ml64 jump slots), compile, link.
#   4. The DLL must export RE_Kenshi's entry point, import Kenshi's C runtime (MSVCR100.dll) and
#      nothing newer, and must not import KenshiLib.dll.
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
        $gameArgs = @(); if ($GameDir) { $gameArgs = @("--game", $GameDir) }
        & $node (Join-Path $RepoRoot "scripts\sites.mjs") check @gameArgs | Select-Object -Last 3
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

$dll = Join-Path $out "KenshiPure.dll"
$sources = @(Get-ChildItem -LiteralPath $src -Filter *.cpp | ForEach-Object { $_.FullName }) + @(Join-Path $gen "kp_sites_gen.cpp")
# /MD: Kenshi's own CRT (game calls hand std::string across). /EHa: __try beside C++.
$common = @("/nologo", "/c", "/O2", "/DNDEBUG", "/DUNICODE", "/D_UNICODE", "/MD", "/W3", "/Z7", "/I$src", "/I$gen")
& $Tc.Cl @($common + @("/EHa", "/wd4482", "/Fo$obj\\") + $sources)
if ($LASTEXITCODE -ne 0) { throw "compile failed ($LASTEXITCODE)" }
$mhObj = Join-Path $obj "minhook"
New-Item -ItemType Directory -Force $mhObj | Out-Null
& $Tc.Cl @($common + @("/Fo$mhObj\\") + (Get-MinHookSources))
if ($LASTEXITCODE -ne 0) { throw "MinHook compile failed ($LASTEXITCODE)" }
& $Tc.Ml64 /nologo /c "/Fo$obj\kp_thunks.obj" (Join-Path $gen "kp_thunks.asm")
if ($LASTEXITCODE -ne 0) { throw "ml64 failed ($LASTEXITCODE)" }

$objs = @(Get-ChildItem -LiteralPath $obj -Recurse -Filter *.obj | ForEach-Object { $_.FullName })
$linkArgs = @("/nologo", "/DLL", "/OUT:$dll", "/MANIFEST:NO", "/DEBUG", "/OPT:REF", "/OPT:ICF",
              "/PDB:$(Join-Path $out 'KenshiPure.pdb')") + $objs + @("bcrypt.lib", "user32.lib", "ws2_32.lib")
& $Tc.Link @linkArgs
if ($LASTEXITCODE -ne 0) { Stop-ToolchainHelpers; throw "link failed ($LASTEXITCODE)" }

# 4. Checks on the result.
$imports = (& $Tc.Dumpbin /nologo /imports $dll) -join "`n"
$exports = (& $Tc.Dumpbin /nologo /exports $dll) -join "`n"
Stop-ToolchainHelpers  # link and dumpbin leave mspdbsrv.exe running
$problems = @()
if ($exports -notmatch "\?startPlugin@@YAXXZ") { $problems += "export ?startPlugin@@YAXXZ missing" }
if ($imports -notmatch "MSVCR100\.dll") { $problems += "does not import MSVCR100.dll (wrong CRT)" }
if ($imports -match "MSVCR1[1-9]0|VCRUNTIME|ucrtbase") { $problems += "imports a newer CRT" }
if ($imports -match "KenshiLib\.dll") { $problems += "imports KenshiLib.dll" }
if ($problems.Count -gt 0) { throw ("build check failed: " + ($problems -join "; ")) }

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $dll).Hash.ToLower()
Write-Host "Built $dll"
Write-Host "  sha256 $hash"
Write-Host "  checks: sites.tsv proven, no raw game fields, exports startPlugin, imports MSVCR100.dll, no KenshiLib.dll"
