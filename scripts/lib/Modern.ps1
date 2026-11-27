# Paths of the modern toolchain that scripts/setup-modern.ps1 puts in tools/modern/ (gitignored).
# Dot-source this file; it defines $Mt (a hashtable of paths) relative to the repo root.

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Mt = @{}
$Mt.Root      = Join-Path $RepoRoot "tools\modern"
$Mt.Downloads = Join-Path $Mt.Root "dl"
$Mt.Vs        = Join-Path $Mt.Root "vs"
$Mt.SdkImage  = Join-Path $Mt.Root "sdk"
$Mt.Sdk       = Join-Path $Mt.SdkImage "Windows Kits\10"
$Mt.Marker    = Join-Path $Mt.Root "msvc-sdk.ok"
$Mt.CMake     = Join-Path $Mt.Root "cmake-4.5.1-windows-x86_64\bin\cmake.exe"
$Mt.Ninja     = Join-Path $Mt.Root "ninja\ninja.exe"
$Mt.Enet      = Join-Path $Mt.Root "enet-1.3.18"
$Mt.Build     = Join-Path $RepoRoot "build\net"

# Puts MSVC, the Windows SDK, CMake and Ninja into this process's PATH/INCLUDE/LIB (only this
# PowerShell process; nothing global changes). Sets $Mt.Cl, $Mt.MsvcVersion, $Mt.SdkVersion.
function Use-Modern {
    if (-not (Test-Path -LiteralPath $Mt.Marker)) {
        throw "Modern toolchain not found in $($Mt.Root). Run: powershell -ExecutionPolicy Bypass -File scripts\setup-modern.ps1"
    }
    $msvc = Get-ChildItem -Directory (Join-Path $Mt.Vs "VC\Tools\MSVC") | Sort-Object Name | Select-Object -Last 1
    $sdkv = Get-ChildItem -Directory (Join-Path $Mt.Sdk "Include") | Sort-Object Name | Select-Object -Last 1
    $Mt.MsvcVersion = $msvc.Name
    $Mt.SdkVersion = $sdkv.Name
    $vc = $msvc.FullName
    $inc = Join-Path $Mt.Sdk "Include\$($sdkv.Name)"
    $lib = Join-Path $Mt.Sdk "Lib\$($sdkv.Name)"
    $Mt.Cl = Join-Path $vc "bin\Hostx64\x64\cl.exe"
    $env:PATH    = "$vc\bin\Hostx64\x64;$(Join-Path $Mt.Sdk "bin\$($sdkv.Name)\x64");$(Split-Path $Mt.CMake);$(Split-Path $Mt.Ninja);$env:SystemRoot\system32;$env:SystemRoot;$env:SystemRoot\System32\WindowsPowerShell\v1.0"
    $env:INCLUDE = "$vc\include;$inc\ucrt;$inc\shared;$inc\um;$inc\winrt;$inc\cppwinrt"
    $env:LIB     = "$vc\lib\x64;$lib\ucrt\x64;$lib\um\x64"
    $env:VSCMD_SKIP_SENDTELEMETRY = "1"
}
