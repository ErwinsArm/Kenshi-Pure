# Switches a Kenshi install between AnarchyKenshi and the KenshiPure client plugin.
#
#   Install:   KenshiPure on, AnarchyKenshi off
#     powershell -ExecutionPolicy Bypass -File scripts\install-client.ps1 -GameDir "<Kenshi folder>"
#   Uninstall: KenshiPure off, AnarchyKenshi back on (only if this script switched it off)
#     powershell -ExecutionPolicy Bypass -File scripts\install-client.ps1 -GameDir "<Kenshi folder>" -Uninstall
#
# What it touches, and nothing else:
#   <game>\mods\KenshiPure\        our plugin folder (KenshiPure.dll, KenshiPure.mod, RE_Kenshi.json, and
#                                  KenshiPure.cfg: with -ProbeWrite it holds probe_write=1, which lets the
#                                  live game probe heal, teleport and spawn items; without it, no cfg)
#   <game>\data\mods.cfg           adds / removes the line "KenshiPure.mod" (the game's mod list)
#   <game>\Plugins_x64.cfg         removes / restores AnarchyKenshi's line
#                                  "Plugin=mods/AnarchyKenshi/AnarchyLoader"; all other lines and their
#                                  order stay as they are (RE_Kenshi must stay: it loads our plugin)
# Before the first change it saves <file>.kenshipure-backup once; existing backups (AnarchyKenshi's
# .before-anarchy / .anarchy-backup, earlier .kenshipure-backup) are never overwritten.
param(
    [Parameter(Mandatory = $true)][string]$GameDir,
    [switch]$Uninstall,
    [switch]$ProbeWrite,  # allow the probe's write commands (heal, teleport, spawn_item)
    [string]$DllPath = "",
    [switch]$NoGameCheck  # for tests only: skip the "Kenshi is running" check
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\CfgEdit.ps1")
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $DllPath) { $DllPath = Join-Path $RepoRoot "build\client\KenshiPure.dll" }

$AkLine      = "Plugin=mods/AnarchyKenshi/AnarchyLoader"
$ReKenshi    = "Plugin=RE_Kenshi"
$ModLine     = "KenshiPure.mod"
$BackupSfx   = ".kenshipure-backup"
$pluginsCfg  = Join-Path $GameDir "Plugins_x64.cfg"
$modsCfg     = Join-Path $GameDir "data\mods.cfg"
$modDir      = Join-Path $GameDir "mods\KenshiPure"
$stateFile   = Join-Path $modDir "install-state.json"

foreach ($f in @($pluginsCfg, $modsCfg)) {
    if (-not (Test-Path -LiteralPath $f)) { throw "Not a Kenshi folder (missing $f): $GameDir" }
}
if (-not $NoGameCheck) {
    $running = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match '^kenshi' }
    if ($running) { throw "Kenshi is running. Close the game first, then run this again." }
}

# An empty FCS mod file (format 17), so the game lists KenshiPure in its mod list. Same bytes as the
# empty .mod in KenshiLib_Examples' HelloWorld.
$EmptyMod = [byte[]](0x11,0,0,0, 0x1e,0,0,0, 1,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 1,0,0,0,
                     0,0,0,0, 0,0, 9,0,0,0, 0,0,0,0)

function Read-State {
    if (Test-Path -LiteralPath $stateFile) { return (Get-Content -Raw -LiteralPath $stateFile | ConvertFrom-Json) }
    return [pscustomobject]@{ removedAnarchyLine = $false }
}

if (-not $Uninstall) {
    if (-not (Test-Path -LiteralPath $DllPath)) {
        throw "Plugin not built: $DllPath. Run scripts\build-plugin.ps1 first."
    }
    $plugins = Read-CfgFile $pluginsCfg
    if ((Find-CfgLine $plugins.Lines $ReKenshi) -lt 0) {
        throw "RE_Kenshi is not enabled in $pluginsCfg ('$ReKenshi' missing). Install RE_Kenshi first; it loads KenshiPure."
    }
    $state = Read-State

    Backup-Once $pluginsCfg $BackupSfx | Out-Null
    Backup-Once $modsCfg $BackupSfx | Out-Null

    New-Item -ItemType Directory -Force $modDir | Out-Null
    Copy-Item -LiteralPath $DllPath -Destination (Join-Path $modDir "KenshiPure.dll") -Force
    $pdb = [IO.Path]::ChangeExtension($DllPath, ".pdb")
    if (Test-Path -LiteralPath $pdb) { Copy-Item -LiteralPath $pdb -Destination (Join-Path $modDir "KenshiPure.pdb") -Force }
    Copy-Item -LiteralPath (Join-Path $RepoRoot "client\mod\RE_Kenshi.json") -Destination (Join-Path $modDir "RE_Kenshi.json") -Force
    [IO.File]::WriteAllBytes((Join-Path $modDir "KenshiPure.mod"), $EmptyMod)
    $cfg = Join-Path $modDir "KenshiPure.cfg"
    if ($ProbeWrite) { Set-Content -LiteralPath $cfg -Value "probe_write=1" -Encoding ASCII }
    elseif (Test-Path -LiteralPath $cfg) { Remove-Item -LiteralPath $cfg -Force }

    $mods = Read-CfgFile $modsCfg
    if ((Find-CfgLine $mods.Lines $ModLine) -lt 0) {
        $mods.Lines = Insert-CfgLineAt $mods.Lines $mods.Lines.Count $ModLine
        if ($mods.Lines.Count -eq 1) { $mods.EndsWithEol = $true }
        Write-CfgFile $modsCfg $mods
    }

    $ak = Find-CfgLine $plugins.Lines $AkLine
    if ($ak -ge 0) {
        $plugins.Lines = Remove-CfgLineAt $plugins.Lines $ak
        Write-CfgFile $pluginsCfg $plugins
        $state.removedAnarchyLine = $true
    }
    $state | ConvertTo-Json | Set-Content -LiteralPath $stateFile -Encoding ASCII

    Write-Host "KenshiPure installed in $modDir"
    Write-Host ("AnarchyKenshi: " + $(if ($state.removedAnarchyLine) { "switched off (uninstall switches it back on)" } else { "was not on" }))
    Write-Host ("Live probe write commands: " + $(if ($ProbeWrite) { "ON (heal, teleport, spawn_item)" } else { "off" }))
    Write-Host "Log after a game start: $(Join-Path $modDir 'KenshiPure.log')"
}
else {
    $state = Read-State

    $mods = Read-CfgFile $modsCfg
    $i = Find-CfgLine $mods.Lines $ModLine
    if ($i -ge 0) {
        $mods.Lines = Remove-CfgLineAt $mods.Lines $i
        Write-CfgFile $modsCfg $mods
    }

    if ($state.removedAnarchyLine) {
        $plugins = Read-CfgFile $pluginsCfg
        if ((Find-CfgLine $plugins.Lines $AkLine) -lt 0) {
            # Where AnarchyKenshi's own enable script puts it: before the first Plugin= line.
            $first = $plugins.Lines.Count
            for ($j = 0; $j -lt $plugins.Lines.Count; $j++) {
                if ($plugins.Lines[$j].Trim().StartsWith("Plugin=")) { $first = $j; break }
            }
            $plugins.Lines = Insert-CfgLineAt $plugins.Lines $first $AkLine
            Write-CfgFile $pluginsCfg $plugins
        }
    }

    # Remove our files; keep the logs so a failed run can still be read.
    foreach ($f in @("KenshiPure.dll", "KenshiPure.pdb", "KenshiPure.mod", "RE_Kenshi.json", "KenshiPure.cfg", "install-state.json")) {
        $p = Join-Path $modDir $f
        if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
    }
    if ((Test-Path -LiteralPath $modDir) -and -not (Get-ChildItem -LiteralPath $modDir)) { Remove-Item -LiteralPath $modDir }

    Write-Host "KenshiPure removed."
    Write-Host ("AnarchyKenshi: " + $(if ($state.removedAnarchyLine) { "switched back on" } else { "left as it was" }))
}
