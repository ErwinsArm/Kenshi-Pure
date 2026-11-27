# Switches a Kenshi install between AnarchyKenshi and the KenshiPure client.
#
#   Install:   KenshiPure on, AnarchyKenshi off
#     powershell -ExecutionPolicy Bypass -File scripts\install-client.ps1 -GameDir "<Kenshi folder>"
#   Uninstall: KenshiPure off, AnarchyKenshi back on (only if this script switched it off)
#     powershell -ExecutionPolicy Bypass -File scripts\install-client.ps1 -GameDir "<Kenshi folder>" -Uninstall
#
# KenshiPure starts as an Ogre plugin (KenshiPureLoader.dll), with or without RE_Kenshi.
# What it touches, and nothing else:
#   <game>\mods\KenshiPure\        KenshiPureLoader.dll, KenshiPure.dll (+ .pdb), and KenshiPure.cfg:
#                                  with -ProbeWrite it holds probe_write=1 (the live game probe may heal,
#                                  teleport and spawn items); without it, no cfg; KenshiPureCheats.dll
#                                  (+ .pdb, the cheat window) only with -Cheats, else removed
#   <game>\Plugins_x64.cfg         adds / removes "Plugin=mods/KenshiPure/KenshiPureLoader" (right after
#                                  "Plugin=RE_Kenshi" if that is there, else before the first Plugin= line);
#                                  removes / restores AnarchyKenshi's "Plugin=mods/AnarchyKenshi/AnarchyLoader";
#                                  all other lines and their order stay as they are
#   <game>\data\mods.cfg           only removes "KenshiPure.mod" left by an older KenshiPure install
#                                  (which RE_Kenshi started through the mod list)
# Before the first change it saves <file>.kenshipure-backup once; existing backups (AnarchyKenshi's
# .before-anarchy / .anarchy-backup, earlier .kenshipure-backup) are never overwritten.
param(
    [Parameter(Mandatory = $true)][string]$GameDir,
    [switch]$Uninstall,
    [switch]$ProbeWrite,  # allow the probe's write commands (heal, teleport, spawn_item)
    [switch]$Cheats,      # also install KenshiPureCheats.dll (the cheat window); without it, it is removed
    [string]$BuildDir = "",
    [switch]$NoGameCheck  # for tests only: skip the "Kenshi is running" check
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\CfgEdit.ps1")
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $BuildDir) { $BuildDir = Join-Path $RepoRoot "build\client" }

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
$OurFiles    = @("KenshiPureLoader.dll", "KenshiPureLoader.pdb", "KenshiPure.dll", "KenshiPure.pdb", "KenshiPure.mod",
                 "RE_Kenshi.json", "KenshiPure.cfg", "install-state.json", "KenshiPureCheats.dll", "KenshiPureCheats.pdb")

foreach ($f in @($pluginsCfg, $modsCfg)) {
    if (-not (Test-Path -LiteralPath $f)) { throw "Not a Kenshi folder (missing $f): $GameDir" }
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

if (-not $Uninstall) {
    foreach ($d in $Dlls) {
        if (-not (Test-Path -LiteralPath (Join-Path $BuildDir $d))) { throw "Not built: $(Join-Path $BuildDir $d). Run scripts\build-plugin.ps1 first." }
    }
    $state = Read-State
    Backup-Once $pluginsCfg $BackupSfx | Out-Null

    New-Item -ItemType Directory -Force $modDir | Out-Null
    foreach ($d in $Dlls) {
        Copy-Item -LiteralPath (Join-Path $BuildDir $d) -Destination (Join-Path $modDir $d) -Force
        $pdb = [IO.Path]::ChangeExtension((Join-Path $BuildDir $d), ".pdb")
        if (Test-Path -LiteralPath $pdb) { Copy-Item -LiteralPath $pdb -Destination $modDir -Force }
    }
    foreach ($c in @("KenshiPureCheats.dll", "KenshiPureCheats.pdb")) {
        $p = Join-Path $modDir $c
        if ($Cheats) { Copy-Item -LiteralPath (Join-Path $BuildDir $c) -Destination $p -Force }
        elseif (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
    }
    foreach ($old in @("RE_Kenshi.json", "KenshiPure.mod")) {
        $p = Join-Path $modDir $old
        if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
    }
    $cfg = Join-Path $modDir "KenshiPure.cfg"
    if ($ProbeWrite) { Set-Content -LiteralPath $cfg -Value "probe_write=1" -Encoding ASCII }
    elseif (Test-Path -LiteralPath $cfg) { Remove-Item -LiteralPath $cfg -Force }
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
