# Switches a Kenshi install between AnarchyKenshi and the KenshiPure client.
#
#   Install:   KenshiPure on, AnarchyKenshi off
#     powershell -ExecutionPolicy Bypass -File scripts\install-client.ps1 -GameDir "<Kenshi folder>"
#   Uninstall: KenshiPure off, AnarchyKenshi back on (only if this script switched it off)
#     powershell -ExecutionPolicy Bypass -File scripts\install-client.ps1 -GameDir "<Kenshi folder>" -Uninstall
#   Server and name: -ServerHost <address> [-Port N] -PlayerName <name> (replace those lines of KenshiPure.cfg).
#   -Ask: asks for what is not given (Kenshi folder, server address, name); the package's .cmd files use it.
#   Without -GameDir the Steam library folders are searched for Kenshi.
#
# The same script runs from the repo (DLLs from build\) and from the multiplayer package made by
# scripts\make-package.ps1 (DLLs next to it; known Kenshi builds in builds.txt). Before installing it checks
# that the Kenshi exe the game will run (RE_Kenshi's copy when RE_Kenshi is on) is a build KenshiPure knows.
#
# KenshiPure starts as an Ogre plugin (KenshiPureLoader.dll), with or without RE_Kenshi.
# What it touches, and nothing else:
#   <game>\mods\KenshiPure\        KenshiPureLoader.dll, KenshiPure.dll (+ .pdb), KenshiPureCore.dll (the
#                                  network core, from scripts\build-net.ps1), and KenshiPure.cfg: the
#                                  network lines host=127.0.0.1, port=27850, name=player are added when
#                                  missing (lines already there, e.g. another server, are kept);
#                                  probe_write=1 is there only with -ProbeWrite (the live game probe may
#                                  heal, teleport and spawn items); standin* lines (a test tool that turns
#                                  the other players' bodies off) are removed; KenshiPureCheats.dll (+ .pdb,
#                                  the cheat window) only with -Cheats, else removed
#   <game>\Plugins_x64.cfg         adds / removes "Plugin=mods/KenshiPure/KenshiPureLoader" (right after
#                                  "Plugin=RE_Kenshi" if that is there, else before the first Plugin= line);
#                                  removes / restores AnarchyKenshi's "Plugin=mods/AnarchyKenshi/AnarchyLoader";
#                                  all other lines and their order stay as they are
#   <game>\data\mods.cfg           only removes "KenshiPure.mod" left by an older KenshiPure install
#                                  (which RE_Kenshi started through the mod list)
# Before the first change it saves <file>.kenshipure-backup once; existing backups (AnarchyKenshi's
# .before-anarchy / .anarchy-backup, earlier .kenshipure-backup) are never overwritten.
param(
    [string]$GameDir = "",
    [switch]$Uninstall,
    [switch]$ProbeWrite,  # allow the probe's write commands (heal, teleport, spawn_item)
    [switch]$Cheats,      # also install KenshiPureCheats.dll (the cheat window); without it, it is removed
    [string]$BuildDir = "",
    [string]$CoreDir = "",   # where KenshiPureCore.dll is (default build\net\Release)
    [string]$ServerHost = "",
    [int]$Port = 0,
    [string]$PlayerName = "",
    [switch]$Ask,          # ask for the Kenshi folder, server address and name (package .cmd files)
    [switch]$NoGameCheck   # for tests only: skip the "Kenshi is running" and the Kenshi version checks
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\CfgEdit.ps1")
# Package mode: the DLLs lie next to this script (scripts\make-package.ps1).
$PackageMode = Test-Path -LiteralPath (Join-Path $PSScriptRoot "KenshiPure.dll")
if ($PackageMode) {
    if (-not $BuildDir) { $BuildDir = $PSScriptRoot }
    if (-not $CoreDir) { $CoreDir = $PSScriptRoot }
    $BuildsFile = Join-Path $PSScriptRoot "builds.txt"
}
else {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
    if (-not $BuildDir) { $BuildDir = Join-Path $RepoRoot "build\client" }
    if (-not $CoreDir) { $CoreDir = Join-Path $RepoRoot "build\net\Release" }
    $BuildsFile = Join-Path $RepoRoot "client\sites.tsv"
}

# Kenshi in the Steam library folders (registry: Steam's folder; libraryfolders.vdf: the other libraries).
function Find-KenshiDir {
    $roots = @()
    foreach ($k in @("HKCU:\Software\Valve\Steam", "HKLM:\SOFTWARE\WOW6432Node\Valve\Steam", "HKLM:\SOFTWARE\Valve\Steam")) {
        try {
            $p = Get-ItemProperty -LiteralPath $k -ErrorAction Stop
            foreach ($n in @("SteamPath", "InstallPath")) { if ($p.$n) { $roots += ($p.$n -replace "/", "\") } }
        } catch { }
    }
    $libs = @()
    foreach ($r in ($roots | Select-Object -Unique)) {
        $libs += $r
        $vdf = Join-Path $r "steamapps\libraryfolders.vdf"
        if (Test-Path -LiteralPath $vdf) {
            foreach ($m in [regex]::Matches((Get-Content -Raw -LiteralPath $vdf), '"path"\s+"([^"]+)"')) { $libs += ($m.Groups[1].Value -replace "\\\\", "\") }
        }
    }
    foreach ($l in ($libs | Select-Object -Unique)) {
        $d = Join-Path $l "steamapps\common\Kenshi"
        if (Test-Path -LiteralPath (Join-Path $d "Plugins_x64.cfg")) { return $d }
    }
    return $null
}

function Test-PlayerName([string]$n) { return ($n -match '^[\x20-\x7E]{1,32}$') -and ($n -notmatch '[#=]') -and $n.Trim().Length -gt 0 }
function Test-ServerHost([string]$h) { return $h -match '^[A-Za-z0-9.:\-]{1,253}$' }

if (-not $GameDir) {
    $found = Find-KenshiDir
    if ($Ask) {
        if ($found) {
            Write-Host "Kenshi found: $found"
            $a = Read-Host "Press Enter to use this folder, or paste another Kenshi folder"
            $GameDir = if ($a.Trim()) { $a.Trim().Trim('"') } else { $found }
        }
        else { $GameDir = (Read-Host "Kenshi was not found. Paste your Kenshi folder (the one with kenshi_x64.exe)").Trim().Trim('"') }
    }
    elseif ($found) { $GameDir = $found }
    else { throw "Kenshi was not found in the Steam library folders; pass -GameDir <Kenshi folder>." }
}

$AkLine      = "Plugin=mods/AnarchyKenshi/AnarchyLoader"
$ReKenshi    = "Plugin=RE_Kenshi"
$OurLine     = "Plugin=mods/KenshiPure/KenshiPureLoader"
$OldModLine  = "KenshiPure.mod"
$BackupSfx   = ".kenshipure-backup"
$pluginsCfg  = Join-Path $GameDir "Plugins_x64.cfg"
$modsCfg     = Join-Path $GameDir "data\mods.cfg"
$modDir      = Join-Path $GameDir "mods\KenshiPure"
$stateFile   = Join-Path $modDir "install-state.json"
$Dlls        = @("KenshiPureLoader.dll", "KenshiPure.dll")
# Files of this and older KenshiPure installs (RE_Kenshi.json and KenshiPure.mod: the RE_Kenshi start).
$CoreDll     = "KenshiPureCore.dll"
$OurFiles    = @("KenshiPureLoader.dll", "KenshiPureLoader.pdb", "KenshiPure.dll", "KenshiPure.pdb", $CoreDll, "KenshiPure.mod",
                 "RE_Kenshi.json", "KenshiPure.cfg", "install-state.json", "KenshiPureCheats.dll", "KenshiPureCheats.pdb")

foreach ($f in @($pluginsCfg, $modsCfg)) {
    if (-not (Test-Path -LiteralPath $f)) { throw "Not a Kenshi folder (missing $f): $GameDir" }
}
if ($Cheats -and -not $Uninstall -and -not (Test-Path -LiteralPath (Join-Path $BuildDir "KenshiPureCheats.dll"))) {
    throw "-Cheats: there is no KenshiPureCheats.dll in $BuildDir (the multiplayer package has no cheats)."
}
if (-not $NoGameCheck) {
    $running = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match '^kenshi' }
    if ($running) { throw "Kenshi is running. Close the game first, then run this again." }
}

function Read-State {
    if (Test-Path -LiteralPath $stateFile) { return (Get-Content -Raw -LiteralPath $stateFile | ConvertFrom-Json) }
    return [pscustomobject]@{ removedAnarchyLine = $false }
}

# Removes the mod-list line of an older install (RE_Kenshi started KenshiPure through it).
function Remove-OldModLine {
    $mods = Read-CfgFile $modsCfg
    $i = Find-CfgLine $mods.Lines $OldModLine
    if ($i -ge 0) {
        Backup-Once $modsCfg $BackupSfx | Out-Null
        $mods.Lines = Remove-CfgLineAt $mods.Lines $i
        Write-CfgFile $modsCfg $mods
    }
}

# The exe the game will run: RE_Kenshi's 1.0.65 copy while RE_Kenshi is on, else Steam's kenshi_x64.exe.
function Assert-KnownBuild {
    $re = (Find-CfgLine (Read-CfgFile $pluginsCfg).Lines $ReKenshi) -ge 0
    $exe = if ($re) { Join-Path $GameDir "RE_Kenshi\Kenshi_x64.exe" } else { Join-Path $GameDir "kenshi_x64.exe" }
    if (-not (Test-Path -LiteralPath $exe)) { throw "Kenshi's exe is missing: $exe. Nothing was changed." }
    $known = @{}
    if (Test-Path -LiteralPath $BuildsFile) {
        foreach ($l in Get-Content -LiteralPath $BuildsFile) { if ($l -match '^#build\s+(\S+)\s+([0-9a-f]{64})\s*(.*)$') { $known[$Matches[2]] = "$($Matches[1]): $($Matches[3])".Trim() } }
    }
    $sha = (Get-FileHash -Algorithm SHA256 -LiteralPath $exe).Hash.ToLowerInvariant()
    if (-not $known.ContainsKey($sha)) {
        throw ("KenshiPure does not know this Kenshi version ($exe, SHA-256 $sha); it would stay off in the game. " +
               "Supported: " + (($known.Values | Sort-Object) -join "; ") + ". Nothing was changed.")
    }
    Write-Host "Kenshi version: $($known[$sha])$(if ($re) { ' - RE_Kenshi is on' })"
}

if (-not $Uninstall) {
    if (-not $NoGameCheck) { Assert-KnownBuild }
    # Server address and player name: given, asked, or kept from an earlier install.
    $cfgPath = Join-Path $modDir "KenshiPure.cfg"
    $old = @{}
    if (Test-Path -LiteralPath $cfgPath) {
        foreach ($l in Get-Content -LiteralPath $cfgPath) { if ($l -match '^\s*(host|port|name)\s*=\s*(.*?)\s*$') { $old[$Matches[1]] = $Matches[2] } }
    }
    if ($Ask -and -not $ServerHost) {
        $def = if ($old["host"] -and $old["host"] -ne "127.0.0.1") { $old["host"] } else { "" }
        while ($true) {
            $a = (Read-Host ("Server address (ask the host; e.g. 100.101.102.103)" + $(if ($def) { " [Enter = $def]" }))).Trim()
            if (-not $a) { $a = $def }
            if (Test-ServerHost $a) { $ServerHost = $a; break }
            Write-Host "That is not an address. Letters, digits, dots, colons and dashes only."
        }
    }
    if ($Ask -and -not $PlayerName) {
        $def = if ($old["name"] -and $old["name"] -ne "player") { $old["name"] } else { "" }
        while ($true) {
            $a = (Read-Host ("Your player name (the other player sees it on your character)" + $(if ($def) { " [Enter = $def]" }))).Trim()
            if (-not $a) { $a = $def }
            if (Test-PlayerName $a) { $PlayerName = $a; break }
            Write-Host "1 to 32 letters, digits or simple signs (no umlauts, no # or =)."
        }
    }
    if ($ServerHost -and -not (Test-ServerHost $ServerHost)) { throw "Not a server address: $ServerHost" }
    if ($PlayerName -and -not (Test-PlayerName $PlayerName)) { throw "Not a usable player name (1-32 plain characters, no # or =): $PlayerName" }
    if ($Port -lt 0 -or $Port -gt 65535) { throw "Not a port: $Port" }

    foreach ($d in $Dlls) {
        if (-not (Test-Path -LiteralPath (Join-Path $BuildDir $d))) { throw "Not built: $(Join-Path $BuildDir $d). Run scripts\build-plugin.ps1 first." }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $CoreDir $CoreDll))) { throw "Not built: $(Join-Path $CoreDir $CoreDll). Run scripts\build-net.ps1 first." }
    $state = Read-State
    Backup-Once $pluginsCfg $BackupSfx | Out-Null

    New-Item -ItemType Directory -Force $modDir | Out-Null
    foreach ($d in $Dlls) {
        Copy-Item -LiteralPath (Join-Path $BuildDir $d) -Destination (Join-Path $modDir $d) -Force
        $pdb = [IO.Path]::ChangeExtension((Join-Path $BuildDir $d), ".pdb")
        if (Test-Path -LiteralPath $pdb) { Copy-Item -LiteralPath $pdb -Destination $modDir -Force }
    }
    Copy-Item -LiteralPath (Join-Path $CoreDir $CoreDll) -Destination (Join-Path $modDir $CoreDll) -Force
    foreach ($c in @("KenshiPureCheats.dll", "KenshiPureCheats.pdb")) {
        $p = Join-Path $modDir $c
        if ($Cheats) { Copy-Item -LiteralPath (Join-Path $BuildDir $c) -Destination $p -Force }
        elseif (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
    }
    foreach ($old in @("RE_Kenshi.json", "KenshiPure.mod")) {
        $p = Join-Path $modDir $old
        if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
    }
    # Downloaded files carry Windows' "from the internet" mark; the game should load ours without asking.
    foreach ($f in @(Get-ChildItem -LiteralPath $modDir -Filter *.dll)) { try { Unblock-File -LiteralPath $f.FullName } catch { } }
    # KenshiPure.cfg: keep every line except probe_write (follows -ProbeWrite) and the standin* test keys (the
    # stand-in tool turns the other players' bodies off); host/port/name given to this script replace theirs;
    # missing network lines are added.
    $cfg = Join-Path $modDir "KenshiPure.cfg"
    $set = @{}
    if ($ServerHost) { $set["host"] = $ServerHost }
    if ($Port) { $set["port"] = "$Port" }
    if ($PlayerName) { $set["name"] = $PlayerName }
    $lines = @()
    if (Test-Path -LiteralPath $cfg) {
        $kept = @(Get-Content -LiteralPath $cfg | Where-Object { $_ -notmatch '^\s*probe_write\s*=' })
        $test = @($kept | Where-Object { $_ -match '^\s*standin\w*\s*=' })
        if ($test.Count -gt 0) { Write-Host "Removed test settings from KenshiPure.cfg: $($test -join ', ')" }
        foreach ($l in @($kept | Where-Object { $_ -notmatch '^\s*standin\w*\s*=' })) {
            if ($l -match '^\s*(host|port|name)\s*=' -and $set.ContainsKey($Matches[1])) { $k = $Matches[1]; $l = "$k=$($set[$k])"; $set.Remove($k) }
            $lines += $l
        }
    }
    foreach ($kv in @(@("host", "127.0.0.1"), @("port", "27850"), @("name", "player"))) {
        if (-not ($lines | Where-Object { $_ -match "^\s*$($kv[0])\s*=" })) {
            $v = if ($set.ContainsKey($kv[0])) { $set[$kv[0]] } else { $kv[1] }
            $lines += "$($kv[0])=$v"
        }
    }
    if ($ProbeWrite) { $lines += "probe_write=1" }
    Set-Content -LiteralPath $cfg -Value $lines -Encoding ASCII
    Remove-OldModLine

    $plugins = Read-CfgFile $pluginsCfg
    $ak = Find-CfgLine $plugins.Lines $AkLine
    if ($ak -ge 0) {
        $plugins.Lines = Remove-CfgLineAt $plugins.Lines $ak
        $state.removedAnarchyLine = $true
    }
    if ((Find-CfgLine $plugins.Lines $OurLine) -lt 0) {
        $re = Find-CfgLine $plugins.Lines $ReKenshi
        $at = $plugins.Lines.Count
        if ($re -ge 0) { $at = $re + 1 }
        else {
            for ($j = 0; $j -lt $plugins.Lines.Count; $j++) {
                if ($plugins.Lines[$j].Trim().StartsWith("Plugin=")) { $at = $j; break }
            }
        }
        $plugins.Lines = Insert-CfgLineAt $plugins.Lines $at $OurLine
    }
    Write-CfgFile $pluginsCfg $plugins
    $state | ConvertTo-Json | Set-Content -LiteralPath $stateFile -Encoding ASCII

    Write-Host "KenshiPure installed in $modDir (Ogre plugin line in Plugins_x64.cfg)"
    Write-Host ("RE_Kenshi: " + $(if ((Find-CfgLine $plugins.Lines $ReKenshi) -ge 0) { "on (KenshiPure loads right after it)" } else { "not on (not needed)" }))
    Write-Host ("AnarchyKenshi: " + $(if ($state.removedAnarchyLine) { "switched off (uninstall switches it back on)" } else { "was not on" }))
    Write-Host ("Live probe write commands: " + $(if ($ProbeWrite) { "ON (heal, teleport, spawn_item)" } else { "off" }))
    Write-Host ("Network (KenshiPure.cfg): " + (@($lines | Where-Object { $_ -match '^\s*(host|port|name)\s*=' }) -join ", "))
    Write-Host "Logs after a game start: $(Join-Path $modDir 'KenshiPureLoader.log') and KenshiPure.log"
}
else {
    $state = Read-State
    Remove-OldModLine

    $plugins = Read-CfgFile $pluginsCfg
    $i = Find-CfgLine $plugins.Lines $OurLine
    if ($i -ge 0) { $plugins.Lines = Remove-CfgLineAt $plugins.Lines $i }
    if ($state.removedAnarchyLine -and (Find-CfgLine $plugins.Lines $AkLine) -lt 0) {
        # Where AnarchyKenshi's own enable script puts it: before the first Plugin= line.
        $first = $plugins.Lines.Count
        for ($j = 0; $j -lt $plugins.Lines.Count; $j++) {
            if ($plugins.Lines[$j].Trim().StartsWith("Plugin=")) { $first = $j; break }
        }
        $plugins.Lines = Insert-CfgLineAt $plugins.Lines $first $AkLine
    }
    Write-CfgFile $pluginsCfg $plugins

    # Remove our files; keep the logs so a failed run can still be read.
    foreach ($f in $OurFiles) {
        $p = Join-Path $modDir $f
        if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
    }
    if ((Test-Path -LiteralPath $modDir) -and -not (Get-ChildItem -LiteralPath $modDir)) { Remove-Item -LiteralPath $modDir }

    Write-Host "KenshiPure removed."
    Write-Host ("AnarchyKenshi: " + $(if ($state.removedAnarchyLine) { "switched back on" } else { "left as it was" }))
}
