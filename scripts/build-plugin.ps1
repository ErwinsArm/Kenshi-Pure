# Builds the KenshiPure client plugin (build\client\KenshiPure.dll) with the VC++ 2010 x64
# toolset from scripts\setup-build.ps1, then checks the result: it must export RE_Kenshi's entry
# point and use Kenshi's C runtime (MSVCR100.dll), never a second static one.
#
#   powershell -ExecutionPolicy Bypass -File scripts\build-plugin.ps1
param()
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Toolchain.ps1")
Use-Toolchain

$src = Join-Path $RepoRoot "client"
$out = Join-Path $RepoRoot "build\client"
$obj = Join-Path $out "obj"
New-Item -ItemType Directory -Force $obj | Out-Null

$sources = Get-ChildItem -LiteralPath $src -Filter *.cpp | ForEach-Object { $_.FullName }
$dll = Join-Path $out "KenshiPure.dll"

# /MD: Kenshi's own CRT (KenshiLib hands std::string across). /EHa: __try in SampleAbi beside C++.
$clArgs = @("/nologo", "/c", "/O2", "/DNDEBUG", "/DUNICODE", "/D_UNICODE", "/wd4482",
            "/MD", "/EHa", "/W3", "/Z7", "/Fo$obj\\") + $sources
& $Tc.Cl @clArgs
if ($LASTEXITCODE -ne 0) { throw "compile failed ($LASTEXITCODE)" }

$objs = Get-ChildItem -LiteralPath $obj -Filter *.obj | ForEach-Object { $_.FullName }
$linkArgs = @("/nologo", "/DLL", "/OUT:$dll", "/MANIFEST:NO", "/DEBUG", "/OPT:REF", "/OPT:ICF",
              "/PDB:$(Join-Path $out 'KenshiPure.pdb')") + $objs + @("KenshiLib.lib", "bcrypt.lib", "user32.lib", "ws2_32.lib")
& (Join-Path $Tc.Vc64 "bin\amd64\link.exe") @linkArgs
if ($LASTEXITCODE -ne 0) { Stop-ToolchainHelpers; throw "link failed ($LASTEXITCODE)" }

$imports = (& $Tc.Dumpbin /nologo /imports $dll) -join "`n"
$exports = (& $Tc.Dumpbin /nologo /exports $dll) -join "`n"
Stop-ToolchainHelpers  # link and dumpbin leave mspdbsrv.exe running
$problems = @()
if ($exports -notmatch "\?startPlugin@@YAXXZ") { $problems += "export ?startPlugin@@YAXXZ missing" }
if ($imports -notmatch "MSVCR100\.dll") { $problems += "does not import MSVCR100.dll (wrong CRT)" }
if ($imports -match "MSVCR1[1-9]0|VCRUNTIME|ucrtbase") { $problems += "imports a newer CRT" }
if ($imports -notmatch "KenshiLib\.dll") { $problems += "does not import KenshiLib.dll" }
if ($problems.Count -gt 0) { throw ("build check failed: " + ($problems -join "; ")) }

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $dll).Hash.ToLower()
Write-Host "Built $dll"
Write-Host "  sha256 $hash"
Write-Host "  checks: exports startPlugin, imports MSVCR100.dll and KenshiLib.dll"
