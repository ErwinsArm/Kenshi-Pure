# Builds the multiplayer package: one folder and a .zip that a player installs on a PC without build tools or
# this repo, and that the host also starts the server from. Nothing is uploaded; the result stays in build\package\.
#
#   powershell -ExecutionPolicy Bypass -File scripts\make-package.ps1
#
# Needs the client built (scripts\build-plugin.ps1) and the network programs (scripts\build-net.ps1).
# Contents (KenshiPure-<version>\):
#   Install KenshiPure.cmd, Uninstall KenshiPure.cmd   double-click wrappers around files\install-client.ps1 -Ask
#   files\                 KenshiPureLoader.dll, KenshiPure.dll, KenshiPureCore.dll, install-client.ps1,
#                          lib\CfgEdit.ps1, builds.txt (the Kenshi builds KenshiPure knows, from client\sites.tsv)
#   server\                kp-server.exe, Start server.cmd (for the host)
#   README.txt, LICENSE.txt, SHA256SUMS.txt
# Never in it: the cheat DLL, .pdb files (they name this PC's folders), anything naming this PC's user.
param([string]$OutDir = "")
$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $OutDir) { $OutDir = Join-Path $RepoRoot "build\package" }

$client = Join-Path $RepoRoot "build\client"
$net = Join-Path $RepoRoot "build\net\Release"
$need = @((Join-Path $client "KenshiPureLoader.dll"), (Join-Path $client "KenshiPure.dll"),
          (Join-Path $net "KenshiPureCore.dll"), (Join-Path $net "kp-server.exe"))
foreach ($f in $need) { if (-not (Test-Path -LiteralPath $f)) { throw "Not built: $f (scripts\build-plugin.ps1, scripts\build-net.ps1)" } }

$src = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "client\KenshiPure.cpp")
if ($src -notmatch '#define KP_VERSION "([^"]+)"') { throw "KP_VERSION not found in client\KenshiPure.cpp" }
$version = $Matches[1]
$name = "KenshiPure-$version"
$pkg = Join-Path $OutDir $name
$zip = Join-Path $OutDir "$name.zip"
# Only our own output folder is replaced.
if (Test-Path -LiteralPath $pkg) { Remove-Item -LiteralPath $pkg -Recurse -Force }
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
$files = Join-Path $pkg "files"
$server = Join-Path $pkg "server"
New-Item -ItemType Directory -Force (Join-Path $files "lib"), $server | Out-Null

Copy-Item -LiteralPath (Join-Path $client "KenshiPureLoader.dll"), (Join-Path $client "KenshiPure.dll"), (Join-Path $net "KenshiPureCore.dll") -Destination $files
Copy-Item -LiteralPath (Join-Path $RepoRoot "scripts\install-client.ps1") -Destination $files
Copy-Item -LiteralPath (Join-Path $RepoRoot "scripts\lib\CfgEdit.ps1") -Destination (Join-Path $files "lib")
$builds = @(Get-Content -LiteralPath (Join-Path $RepoRoot "client\sites.tsv") | Where-Object { $_ -match '^#build\s' })
if ($builds.Count -eq 0) { throw "no #build lines in client\sites.tsv" }
Set-Content -LiteralPath (Join-Path $files "builds.txt") -Value $builds -Encoding ASCII
Copy-Item -LiteralPath (Join-Path $net "kp-server.exe") -Destination $server
Copy-Item -LiteralPath (Join-Path $RepoRoot "LICENSE") -Destination (Join-Path $pkg "LICENSE.txt")
# Third-party code compiled into the binaries: MinHook (KenshiPure.dll, BSD-2), ENet (KenshiPureCore.dll, kp-server.exe, MIT).
$notices = @(
    @("MinHook 1.3.4 (in KenshiPure.dll)", (Join-Path $RepoRoot "tools\build\minhook-1.3.4\LICENSE.txt")),
    @("ENet 1.3.18 (in KenshiPureCore.dll and kp-server.exe)", (Join-Path $RepoRoot "tools\modern\enet-1.3.18\LICENSE")))
$nt = "Third-party code in this package and its licences`r`n"
foreach ($n in $notices) {
    if (-not (Test-Path -LiteralPath $n[1])) { throw "licence text missing: $($n[1]) (scripts\setup-build.ps1, scripts\setup-modern.ps1)" }
    $nt += "`r`n==== $($n[0]) ====`r`n`r`n" + (Get-Content -Raw -LiteralPath $n[1]).Trim() + "`r`n"
}
[IO.File]::WriteAllText((Join-Path $pkg "THIRD-PARTY-NOTICES.txt"), $nt, [Text.Encoding]::UTF8)

function Write-Text([string]$path, [string]$text) {
    [IO.File]::WriteAllText($path, ($text -replace "`r?`n", "`r`n"), [Text.Encoding]::ASCII)
}

Write-Text (Join-Path $pkg "Install KenshiPure.cmd") @'
@echo off
title Install KenshiPure
cd /d "%~dp0"
echo Installs KenshiPure (two-player Kenshi) into your Kenshi folder. Close Kenshi first.
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "files\install-client.ps1" -Ask
echo.
pause
'@

Write-Text (Join-Path $pkg "Uninstall KenshiPure.cmd") @'
@echo off
title Uninstall KenshiPure
cd /d "%~dp0"
echo Removes KenshiPure from your Kenshi folder (AnarchyKenshi comes back on if the install switched it off).
echo Close Kenshi first. The logs in mods\KenshiPure stay.
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "files\install-client.ps1" -Uninstall -Ask
echo.
pause
'@

Write-Text (Join-Path $server "Start server.cmd") @'
@echo off
title KenshiPure server
cd /d "%~dp0"
echo KenshiPure server on UDP port 27850.
echo Keep this window open while you play. Close it to stop the server.
echo If Windows asks about the firewall: tick both boxes (private and public networks) and allow.
echo Everything shown here is also added to kp-server.log next to this file.
echo.
powershell -NoProfile -ExecutionPolicy Bypass -Command "& '.\kp-server.exe' --port 27850 --log-poses | ForEach-Object { $_; Add-Content -LiteralPath 'kp-server.log' -Value $_ -Encoding ASCII }"
pause
'@

$repoUrl = ""
try { $repoUrl = (& git -C $RepoRoot remote get-url origin 2>$null) } catch { }
if ($repoUrl -notmatch '^https://') { $repoUrl = "(see the project's GitHub page)" }
$buildList = ($builds | ForEach-Object { if ($_ -match '^#build\s+\S+\s+[0-9a-f]{64}\s+(.*)$') { "  - " + $Matches[1] } }) -join "`n"

Write-Text (Join-Path $pkg "README.txt") @"
KenshiPure $version - two-player Kenshi (test build)
=====================================================

Each player sees the other's character, shown as "[KP] <name>", walking where the other walks.
The worlds are not shared otherwise: NPCs, items, buildings and time are each player's own.

Kenshi versions this build knows:
$buildList
GOG is not supported yet. The installer checks your version and changes nothing if it is not one of these.


THE PLAYER WHO JOINS
--------------------
1. Close Kenshi.
2. Unzip this whole folder somewhere, e.g. on the Desktop.
3. Double-click "Install KenshiPure.cmd". If Windows asks whether to run it, choose "Run" / "More info - Run anyway".
   The DLLs are not signed; if your antivirus quarantines one, KenshiPure stays off (tell the host).
   - It finds Kenshi in Steam by itself (press Enter), or asks you to paste the Kenshi folder.
   - Server address: the host tells you (with Tailscale it starts with 100.).
   - Player name: what the other player will see on your character (plain letters, no umlauts).
   If AnarchyKenshi is installed, it is switched off while KenshiPure is installed.
4. Start Kenshi through Steam as usual. The very first start can take a few minutes.
5. Start a new game with the start "The Wanderer" (both players), so you both begin in The Hub.

Removing it: close Kenshi, double-click "Uninstall KenshiPure.cmd".
Changing the server address or name: run "Install KenshiPure.cmd" again.


THE HOST
--------
1. Install KenshiPure on your own PC the same way (server address: 127.0.0.1).
2. Before playing, double-click "server\Start server.cmd" and leave its window open.
   If Windows asks about the firewall, tick both boxes and allow.
3. Your friend reaches your PC over a private network (recommended: Tailscale, both of you signed in;
   your PC's address is shown in the Tailscale app). A forwarded router port (UDP 27850) works too,
   but then anyone on the internet who finds it can join: the server has no password yet.


WHILE YOU PLAY
--------------
- Game speed is always x1 from the moment you connect until you close Kenshi (also if the connection drops):
  the pause key, the faster-speed buttons and keys, talking to NPCs and the build menu do not pause or speed up.
  Only loading and the character editor of a new game still pause. This keeps both worlds in step.
- The other player only shows up while you are both near each other (same town or close by).
- Your character's save does not keep the other player's character; it comes back when both play again.


AFTER PLAYING (for the person fixing problems)
----------------------------------------------
Logs: <Kenshi folder>\mods\KenshiPure\KenshiPure.log and KenshiPureLoader.log, and the host's
server\kp-server.log. They contain player names and positions, no passwords.


WHAT THE INSTALL CHANGES
------------------------
- <Kenshi folder>\mods\KenshiPure\  our files, settings (KenshiPure.cfg) and logs
- <Kenshi folder>\Plugins_x64.cfg   one line added (and AnarchyKenshi's line removed while installed);
                                    the first install saves Plugins_x64.cfg.kenshipure-backup
Nothing else on the PC is changed.


Source code and licence: $repoUrl (GNU AGPL-3.0, see LICENSE.txt).
"@

# Checks: no cheats, no .pdb, nothing that names this PC's user or user folder.
$all = @(Get-ChildItem -LiteralPath $pkg -Recurse -File)
foreach ($f in $all) {
    if ($f.Name -match '(?i)cheat|\.pdb$') { throw "package check: $($f.Name) must not be in the package" }
}
$needles = @($env:USERPROFILE, $env:USERNAME, $RepoRoot) | Where-Object { $_ -and $_.Length -ge 3 } | Select-Object -Unique
foreach ($f in $all) {
    $bytes = [IO.File]::ReadAllBytes($f.FullName)
    $ascii = [Text.Encoding]::ASCII.GetString($bytes)
    $utf16 = [Text.Encoding]::Unicode.GetString($bytes)
    foreach ($n in $needles) {
        if ($ascii.IndexOf($n, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or $utf16.IndexOf($n, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "package check: $($f.FullName) contains '$n' (this PC's user or folder)"
        }
    }
}

$sums = foreach ($f in ($all | Sort-Object FullName)) {
    $rel = $f.FullName.Substring($pkg.Length + 1)
    "$((Get-FileHash -Algorithm SHA256 -LiteralPath $f.FullName).Hash.ToLowerInvariant())  $rel"
}
Set-Content -LiteralPath (Join-Path $pkg "SHA256SUMS.txt") -Value $sums -Encoding ASCII

Compress-Archive -Path $pkg -DestinationPath $zip
Write-Host "Package: $pkg"
Write-Host "Zip:     $zip ($([math]::Round((Get-Item -LiteralPath $zip).Length / 1MB, 2)) MB)"
Write-Host "Checks:  no cheats, no .pdb, no user name or user folder of this PC in any file"
