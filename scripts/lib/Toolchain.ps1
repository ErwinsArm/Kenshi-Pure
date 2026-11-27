# Paths of the plugin toolchain that scripts/setup-build.ps1 puts in tools/build/ (gitignored).
# Dot-source this file; it defines $Tc (a hashtable of paths) relative to the repo root.

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Tc = @{}
$Tc.Root      = Join-Path $RepoRoot "tools\build"
$Tc.Downloads = Join-Path $Tc.Root "dl"
$Tc.Sdk71     = Join-Path $Tc.Root "sdk71"
$Tc.Vc        = Join-Path $Tc.Sdk71 "Program Files\Microsoft Visual Studio 10.0\VC"
$Tc.Vc64      = Join-Path $Tc.Sdk71 "Program Files(64)\Microsoft Visual Studio 10.0\VC"
$Tc.Ide       = Join-Path $Tc.Sdk71 "Program Files\Microsoft Visual Studio 10.0\Common7\IDE"
$Tc.WinSdk    = Join-Path $Tc.Sdk71 "Program Files\Microsoft SDKs\Windows\v7.1"
$Tc.KenshiLibBin = Join-Path $Tc.Root "kenshilib"
$Tc.KenshiLibSrc = Join-Path $Tc.Root "kenshilib-src\KenshiLib-0.5.0"
$Tc.Boost     = Join-Path $Tc.Root "boost_1_60_0"
$Tc.Cl        = Join-Path $Tc.Vc64 "bin\amd64\cl.exe"
$Tc.Dumpbin   = Join-Path $Tc.Vc64 "bin\amd64\dumpbin.exe"
$Tc.Ml64      = Join-Path $Tc.Vc64 "bin\amd64\ml64.exe"
$Tc.Link      = Join-Path $Tc.Vc64 "bin\amd64\link.exe"
$Tc.MinHook   = Join-Path $Tc.Root "minhook-1.3.4"
$Tc.Iced      = Join-Path $Tc.Root "iced-x86\package\iced_x86.js"
$Tc.KenshiLibRvas = Join-Path $Tc.KenshiLibBin "RE_Kenshi\RVAs"

# MinHook's C files (compiled into each of our DLLs that hooks).
function Get-MinHookSources {
    @("src\hook.c", "src\buffer.c", "src\trampoline.c", "src\hde\hde64.c") | ForEach-Object { Join-Path $Tc.MinHook $_ }
}

# Node for our build scripts: the portable one from scripts\setup-rea.ps1, else node on PATH.
function Get-ToolNode {
    $n = Join-Path $RepoRoot "tools\rea\node\node.exe"
    if (Test-Path -LiteralPath $n) { return $n }
    $c = Get-Command node -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    throw "No Node found. Run scripts\setup-rea.ps1 (it installs a portable Node) or install Node."
}

# Puts the v100 x64 compiler, CRT, Windows SDK 7.1, KenshiLib and Boost into this process's
# PATH/INCLUDE/LIB (only this PowerShell process; nothing global changes).
function Use-Toolchain {
    if (-not (Test-Path -LiteralPath $Tc.Cl)) {
        throw "VC++ 2010 compiler not found in $($Tc.Root). Run: powershell -ExecutionPolicy Bypass -File scripts\setup-build.ps1"
    }
    $env:PATH    = "$($Tc.Vc64)\bin\amd64;$($Tc.Ide);$env:SystemRoot\system32;$env:SystemRoot"
    $env:INCLUDE = "$($Tc.MinHook)\include;$($Tc.Vc)\include;$($Tc.WinSdk)\Include;$($Tc.KenshiLibSrc)\Include;$($Tc.KenshiLibSrc)\Include\ogre;$($Tc.Boost)"
    $env:LIB     = "$($Tc.Vc64)\lib\amd64;$($Tc.Vc)\lib\amd64;$($Tc.WinSdk)\Lib\x64;$($Tc.KenshiLibBin)"
}

# The v100 linker leaves mspdbsrv.exe running, which locks tools\build; stop ours only.
function Stop-ToolchainHelpers {
    Get-Process mspdbsrv -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and $_.Path.StartsWith($Tc.Root, [StringComparison]::OrdinalIgnoreCase) } |
        Stop-Process -Force -ErrorAction SilentlyContinue
}
