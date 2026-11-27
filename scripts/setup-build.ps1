# Sets up the toolchain for the KenshiPure client plugin in tools\build\ (gitignored):
#   - VC++ 2010 (v100) x64 compiler + CRT and Windows SDK 7.1 headers/libs, unpacked from
#     Microsoft's Windows SDK 7.1 ISO with "msiexec /a" (an administrative image: files only,
#     nothing is installed or registered in Windows).
#   - KenshiLib 0.5.0 (the version RE_Kenshi 0.3.5 ships): release zip for KenshiLib.lib, git tag
#     for the headers (pinned by commit).
#   - Boost 1.60.0 headers (KenshiLib's headers need them).
# Every download is checked against a pinned SHA-256. Re-running skips finished parts.
#
#   powershell -ExecutionPolicy Bypass -File scripts\setup-build.ps1            # set up
#   powershell -ExecutionPolicy Bypass -File scripts\setup-build.ps1 -KeepIso   # keep the 571 MB ISO
#   powershell -ExecutionPolicy Bypass -File scripts\setup-build.ps1 -Uninstall # delete tools\build
param([switch]$Uninstall, [switch]$KeepIso)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
. (Join-Path $PSScriptRoot "lib\Toolchain.ps1")

if ($Uninstall) {
    Stop-ToolchainHelpers
    if (Test-Path -LiteralPath $Tc.Root) { Remove-Item -Recurse -Force -LiteralPath $Tc.Root }
    Write-Host "Removed $($Tc.Root)"
    return
}

# Sources. Change URL and SHA-256 together.
$Iso = @{ Url = "https://download.microsoft.com/download/F/1/0/F10113F5-B750-4969-A255-274341AC6BCE/GRMSDKX_EN_DVD.iso"
          Sha = "9f495e52f33d68125277cbca3565ce57904af7d506758a14691fa6e2479ad65c"; File = "GRMSDKX_EN_DVD.iso" }
$KlZip = @{ Url = "https://github.com/BFrizzleFoShizzle/KenshiLib/releases/download/v0.5.0/KenshiLib_v0.5.0.zip"
            Sha = "ad4534e92a3a244cf25d724d6a695d42ef8770e4b7a1f91ed53affcac171d0c4"; File = "KenshiLib_v0.5.0.zip" }
$KlGit = @{ Url = "https://github.com/BFrizzleFoShizzle/KenshiLib.git"; Tag = "v0.5.0"
            Commit = "11cd65d557dea76c6540239acf467e0e06e09cf7" }
$Boost = @{ Url = "https://archives.boost.io/release/1.60.0/source/boost_1_60_0.tar.gz"
            Sha = "21ef30e7940bc09a0b77a6e59a8eee95f01a766aa03cdfa02f8e167491716ee4"; File = "boost_1_60_0.tar.gz" }
# The SDK's packages that hold the x64 compiler, CRT, and Windows headers/libs.
$Msis = @("vc_stdx86", "vc_stdamd64", "WinSDKBuild_amd64", "WinSDK_amd64")

function Get-Pinned($d) {
    New-Item -ItemType Directory -Force $Tc.Downloads | Out-Null
    $path = Join-Path $Tc.Downloads $d.File
    if (Test-Path -LiteralPath $path) {
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLower() -eq $d.Sha) { return $path }
        Remove-Item -LiteralPath $path
    }
    Write-Host "Downloading $($d.Url)"
    Invoke-WebRequest -UseBasicParsing -Uri $d.Url -OutFile $path
    $got = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLower()
    if ($got -ne $d.Sha) { Remove-Item -LiteralPath $path; throw "SHA-256 mismatch for $($d.File): got $got" }
    return $path
}

# 1. VC++ 2010 compiler and Windows SDK 7.1 (files only).
if (-not (Test-Path -LiteralPath $Tc.Cl)) {
    $isoPath = Get-Pinned $Iso
    $image = Mount-DiskImage -ImagePath $isoPath -PassThru
    try {
        $drive = ($image | Get-Volume).DriveLetter
        New-Item -ItemType Directory -Force $Tc.Sdk71 | Out-Null
        foreach ($m in $Msis) {
            Write-Host "Unpacking $m"
            $log = Join-Path $Tc.Sdk71 "$m.log"
            $p = Start-Process msiexec.exe -Wait -PassThru -ArgumentList `
                "/a `"${drive}:\Setup\$m\$m.msi`" /qn TARGETDIR=`"$($Tc.Sdk71)`" /L*v `"$log`""
            if ($p.ExitCode -ne 0) { throw "msiexec /a $m failed ($($p.ExitCode)); see $log" }
        }
    } finally {
        Dismount-DiskImage -ImagePath $isoPath | Out-Null
    }
    if (-not (Test-Path -LiteralPath $Tc.Cl)) { throw "cl.exe not found after unpacking: $($Tc.Cl)" }
    if (-not $KeepIso) { Remove-Item -LiteralPath $isoPath }
}

# 2. KenshiLib binaries (KenshiLib.lib) and headers.
if (-not (Test-Path -LiteralPath (Join-Path $Tc.KenshiLibBin "KenshiLib.lib"))) {
    $zip = Get-Pinned $KlZip
    Expand-Archive -LiteralPath $zip -DestinationPath $Tc.KenshiLibBin -Force
}
if (-not (Test-Path -LiteralPath (Join-Path $Tc.KenshiLibSrc "Include\core\Functions.h"))) {
    if (Test-Path -LiteralPath $Tc.KenshiLibSrc) { Remove-Item -Recurse -Force -LiteralPath $Tc.KenshiLibSrc }
    New-Item -ItemType Directory -Force (Split-Path $Tc.KenshiLibSrc) | Out-Null
    git -c advice.detachedHead=false clone --quiet --depth 1 --branch $KlGit.Tag $KlGit.Url $Tc.KenshiLibSrc
    if ($LASTEXITCODE -ne 0) { throw "git clone of KenshiLib failed" }
    $head = (git -C $Tc.KenshiLibSrc rev-parse HEAD).Trim()
    if ($head -ne $KlGit.Commit) { throw "KenshiLib tag $($KlGit.Tag) is at $head, expected $($KlGit.Commit)" }
}

# 3. Boost 1.60.0 headers (only the boost\ folder is unpacked).
if (-not (Test-Path -LiteralPath (Join-Path $Tc.Boost "boost\version.hpp"))) {
    $tar = Get-Pinned $Boost
    Write-Host "Unpacking Boost headers (takes a few minutes)"
    & "$env:SystemRoot\System32\tar.exe" -xzf $tar -C $Tc.Root "boost_1_60_0/boost"
    if ($LASTEXITCODE -ne 0) { throw "tar failed unpacking Boost" }
    Remove-Item -LiteralPath $tar
}

# 4. Smoke test: the compiler runs.
Use-Toolchain
$ErrorActionPreference = "Continue"
$banner = (& $Tc.Cl 2>&1 | Select-Object -First 1 | ForEach-Object { "$_" }) -join ""
Write-Host "Ready: $banner"
Write-Host "Build the plugin with: powershell -ExecutionPolicy Bypass -File scripts\build-plugin.ps1"
