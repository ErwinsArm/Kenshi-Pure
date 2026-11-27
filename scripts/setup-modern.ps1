# Sets up the modern C++ toolchain for server, bot, tests and (from M3 on) the client core in
# tools\modern\ (gitignored). Nothing is installed into Windows and no admin rights are needed:
#   - MSVC 14.44 (VS 2022 17.14 Build Tools compiler, x64 host and target) and Windows SDK
#     10.0.26100, unpacked from Microsoft's own VS 2022 package manifest. The manifest is pinned
#     by SHA-256; every package from it is checked against the SHA-256 the manifest lists.
#     (Same packages the VS Build Tools installer would fetch for the C++ workload, files only.)
#   - CMake 4.5.1 and Ninja 1.13.2 (portable zips, SHA-256 from their official release pages).
#   - ENet 1.3.18 sources (MIT; upstream release tarball, pinned by SHA-256), built by net\CMakeLists.txt.
# Running this script means accepting Microsoft's Visual Studio Build Tools licence
# (https://go.microsoft.com/fwlink/?LinkId=2179911) for these files; nothing of them is redistributed.
#
#   powershell -ExecutionPolicy Bypass -File scripts\setup-modern.ps1             # set up (~1 GB)
#   powershell -ExecutionPolicy Bypass -File scripts\setup-modern.ps1 -Uninstall  # delete tools\modern
param([switch]$Uninstall)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
. (Join-Path $PSScriptRoot "lib\Modern.ps1")
$License = "https://go.microsoft.com/fwlink/?LinkId=2179911"

if ($Uninstall) {
    if (Test-Path -LiteralPath $Mt.Root) { Remove-Item -Recurse -Force -LiteralPath $Mt.Root }
    Write-Host "Removed $($Mt.Root)"
    return
}

# Sources. Change URL and SHA-256 together.
# VS 2022 17.14.45 package manifest (URL from https://aka.ms/vs/17/release/channel; SHA-256 of the
# file as served, measured: the channel's own hash field does not match the served bytes).
$VsMan = @{ Url = "https://download.visualstudio.microsoft.com/download/pr/95995f57-8355-4ff5-8d90-a7e1669fb3b3/da49c304df0fff4e75c20849171840a06840e845ce814104d709dfb5cd461668/VisualStudio.vsman"
            Sha = "d1d8109899558618035724949673f69f8c1b5704f542a2e76f31d87e425d5710"; File = "VisualStudio-17.14.45.vsman" }
$MsvcPkgs = @("Tools.HostX64.TargetX64.base", "Tools.HostX64.TargetX64.Res.base", "CRT.Headers.base", "CRT.x64.Desktop.base", "CRT.x64.Store.base") |
    ForEach-Object { "Microsoft.VC.14.44.17.14.$_" }
$SdkPkg = "Win11SDK_10.0.26100"
$SdkMsis = @("Windows SDK for Windows Store Apps Tools-x86_en-us.msi",
             "Windows SDK for Windows Store Apps Headers-x86_en-us.msi",
             "Windows SDK for Windows Store Apps Headers OnecoreUap-x86_en-us.msi",
             "Windows SDK for Windows Store Apps Libs-x86_en-us.msi",
             "Universal CRT Headers Libraries and Sources-x86_en-us.msi",
             "Windows SDK Desktop Headers x64-x86_en-us.msi",
             "Windows SDK Desktop Libs x64-x86_en-us.msi")
$CMake = @{ Url = "https://github.com/Kitware/CMake/releases/download/v4.5.1/cmake-4.5.1-windows-x86_64.zip"
            Sha = "bfe6eadb245dc8213e0d3feea316b520cb2aae92f5667ffd76059ba6b05c430c"; File = "cmake-4.5.1-windows-x86_64.zip" }
$Ninja = @{ Url = "https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip"
            Sha = "07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65"; File = "ninja-1.13.2-win.zip" }
# ENet's own release tarball (the site serves no valid HTTPS; the SHA-256 pin protects it).
$Enet = @{ Url = "http://enet.bespin.org/download/enet-1.3.18.tar.gz"
           Sha = "2a8a0c5360d68bb4fcd11f2e4c47c69976e8d2c85b109dd7d60b1181a4f85d36"; File = "enet-1.3.18.tar.gz" }

function Get-Pinned($d, [string]$dir = $Mt.Downloads) {
    New-Item -ItemType Directory -Force $dir | Out-Null
    $path = Join-Path $dir $d.File
    if (Test-Path -LiteralPath $path) {
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLower() -eq $d.Sha.ToLower()) { return $path }
        Remove-Item -LiteralPath $path
    }
    Write-Host "Downloading $($d.File)"
    Invoke-WebRequest -UseBasicParsing -Uri $d.Url -OutFile $path
    $got = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLower()
    if ($got -ne $d.Sha.ToLower()) { Remove-Item -LiteralPath $path; throw "SHA-256 mismatch for $($d.File): got $got" }
    return $path
}

function Get-Package($man, [string]$id) {
    $p = @($man.packages | Where-Object { $_.id -eq $id -and (-not $_.language -or $_.language -eq "en-US") })
    if ($p.Count -lt 1) { throw "package $id not in the pinned manifest" }
    return $p[0]
}

# Unzips the "Contents/" part of a VSIX (a zip) into $dest.
function Expand-Vsix([string]$vsix, [string]$dest) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($vsix)
    try {
        foreach ($e in $zip.Entries) {
            $name = [Uri]::UnescapeDataString($e.FullName)
            if (-not $name.StartsWith("Contents/") -or $name.EndsWith("/")) { continue }
            $out = Join-Path $dest ($name.Substring(9) -replace "/", "\")
            New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($e, $out, $true)
        }
    } finally { $zip.Dispose() }
}

# 1. MSVC and Windows SDK from the pinned VS manifest.
Write-Host "MSVC and the Windows SDK are Microsoft's; using them means accepting the VS Build Tools licence: $License"
# One marker per part (tools\modern\done\<part>), so a changed list re-fetches only what is new.
$doneDir = Join-Path $Mt.Root "done"
$todo = @($MsvcPkgs + $SdkPkg | Where-Object { -not (Test-Path -LiteralPath (Join-Path $doneDir $_)) })
if ($todo.Count -gt 0) {
    $manPath = Get-Pinned $VsMan
    $man = Get-Content -Raw -LiteralPath $manPath | ConvertFrom-Json
    New-Item -ItemType Directory -Force $doneDir | Out-Null
    $tmp = Join-Path $Mt.Downloads "pkg"
}
foreach ($id in @($MsvcPkgs | Where-Object { $todo -contains $_ })) {
    $pkg = Get-Package $man $id
    foreach ($pl in $pkg.payloads) {
        $f = Get-Pinned @{ Url = $pl.url; Sha = $pl.sha256; File = (Split-Path -Leaf $pl.fileName) } $tmp
        Write-Host "Unpacking $id"
        Expand-Vsix $f $Mt.Vs
        Remove-Item -LiteralPath $f
    }
    Set-Content -LiteralPath (Join-Path $doneDir $id) -Value "ok" -Encoding ascii
}
if ($todo -contains $SdkPkg) {
    $sdk = Get-Package $man $SdkPkg
    $byName = @{}
    foreach ($pl in $sdk.payloads) { $byName[($pl.fileName -replace "\\", "/")] = $pl }
    $sdkDl = Join-Path $Mt.Downloads "sdk"
    $msiPaths = @()
    foreach ($m in $SdkMsis) {
        $pl = $byName["Installers/$m"]
        if (-not $pl) { throw "SDK installer $m not in the pinned manifest" }
        $msi = Get-Pinned @{ Url = $pl.url; Sha = $pl.sha256; File = $m } $sdkDl
        $msiPaths += $msi
        # The MSI names its cabinets as plain strings; fetch each one the manifest lists.
        $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($msi))
        foreach ($cab in ([regex]::Matches($text, "[0-9a-f]{32}\.cab") | ForEach-Object { $_.Value } | Sort-Object -Unique)) {
            $cpl = $byName["Installers/$cab"]
            if ($cpl) { Get-Pinned @{ Url = $cpl.url; Sha = $cpl.sha256; File = $cab } $sdkDl | Out-Null }
        }
    }
    New-Item -ItemType Directory -Force $Mt.SdkImage | Out-Null
    foreach ($msi in $msiPaths) {
        Write-Host "Unpacking $(Split-Path -Leaf $msi)"
        $log = Join-Path $Mt.Downloads ((Split-Path -Leaf $msi) + ".log")
        $p = Start-Process msiexec.exe -Wait -PassThru -ArgumentList "/a `"$msi`" /qn TARGETDIR=`"$($Mt.SdkImage)`" /L*v `"$log`""
        if ($p.ExitCode -ne 0) { throw "msiexec /a $msi failed ($($p.ExitCode)); see $log" }
    }
    Remove-Item -Recurse -Force -LiteralPath $sdkDl -ErrorAction SilentlyContinue
    Get-ChildItem -LiteralPath $Mt.SdkImage -Filter *.msi | Remove-Item
    Set-Content -LiteralPath (Join-Path $doneDir $SdkPkg) -Value "ok" -Encoding ascii
}
if ($todo.Count -gt 0) {
    Remove-Item -Recurse -Force -LiteralPath $tmp -ErrorAction SilentlyContinue
    Set-Content -LiteralPath $Mt.Marker -Value "MSVC 14.44 / SDK 10.0.26100 from $($VsMan.File)" -Encoding ascii
}

# 2. CMake, Ninja, ENet.
if (-not (Test-Path -LiteralPath $Mt.CMake)) {
    $z = Get-Pinned $CMake
    Expand-Archive -LiteralPath $z -DestinationPath $Mt.Root -Force
    Remove-Item -LiteralPath $z
}
if (-not (Test-Path -LiteralPath $Mt.Ninja)) {
    $z = Get-Pinned $Ninja
    Expand-Archive -LiteralPath $z -DestinationPath (Split-Path $Mt.Ninja) -Force
}
if (-not (Test-Path -LiteralPath (Join-Path $Mt.Enet "include\enet\enet.h"))) {
    $z = Get-Pinned $Enet
    & "$env:SystemRoot\System32\tar.exe" -xzf $z -C $Mt.Root
    if ($LASTEXITCODE -ne 0) { throw "unpacking ENet failed" }
}

# 3. Smoke test: cl compiles and links a C++20 program, CMake and Ninja run.
Use-Modern
$smoke = Join-Path $Mt.Root "smoke"
New-Item -ItemType Directory -Force $smoke | Out-Null
Set-Content -LiteralPath "$smoke\s.cpp" -Value "#include <span>`n#include <cstdio>`nint main(){int a[2]={1,2};std::span<int> s(a);std::printf(`"%zu`",s.size());return 0;}" -Encoding ascii
$ErrorActionPreference = "Continue"
$out = (& $Mt.Cl /nologo /std:c++20 /EHsc "/Fo$smoke\\" "/Fe$smoke\s.exe" "$smoke\s.cpp" 2>&1 | ForEach-Object { "$_" }) -join "`n"
$run = if (Test-Path "$smoke\s.exe") { (& "$smoke\s.exe") -join "" } else { "" }
$cmakeV = ((& $Mt.CMake --version) | Select-Object -First 1)
$ninjaV = (& $Mt.Ninja --version)
Remove-Item -Recurse -Force -LiteralPath $smoke
if ($run -ne "2") { throw "MSVC smoke test failed:`n$out" }
Write-Host "Ready: MSVC $($Mt.MsvcVersion), SDK $($Mt.SdkVersion), $cmakeV, Ninja $ninjaV, ENet 1.3.18"
Write-Host "Build server, bot and tests with: powershell -ExecutionPolicy Bypass -File scripts\build-net.ps1"
