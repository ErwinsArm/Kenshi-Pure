# Runs one in-game test of KenshiPure without anyone at the game: starts the relay server and the test
# bot, starts Kenshi windowed (on the second monitor if there is one), lets KenshiPure press OK in
# Kenshi's start dialog and load a save at
# the main menu (mods\KenshiPure\autoload.txt, read once and deleted by the plugin), waits, then
# closes Kenshi WITHOUT saving and restores kenshi.cfg. Prints the KenshiPure log lines of the run.
#
#   powershell -ExecutionPolicy Bypass -File scripts\run-game-test.ps1 -GameDir <Kenshi folder> -Save "<save name>"
#              [-Seconds 200] [-BotArgs "--around-first --radius 300 --speed 60"] [-Grep "standin:"]
#              [-Cfg "remote_drive=steer;remote_save_guard=0"] [-Steps "at=90 save SAVE NAME;at=150 load SAVE NAME"]
#              [-BotSeconds 120] [-BotRestart 150] [-SaveCopy] [-NoReKenshi]
#
# Steps also take "at=<s> stall <seconds>" (the game's main thread sleeps, as in a long load) and
# "at=<s> quit" (the game window gets WM_CLOSE, as from its close button); after a quit the script waits
# for Kenshi to exit by itself and prints its exit code, the loader's stop line and the server's leave line.
# -NoReKenshi leaves the Plugin=RE_Kenshi line out of Plugins_x64.cfg for this run (Steam's own exe runs,
# without RE_Kenshi); the file comes back afterwards.
# -Cfg sets KenshiPure.cfg keys for this run (the file comes back afterwards); standin= lines are dropped
# unless -Cfg sets standin. -Steps are timed steps for the plugin (seconds after the world was entered;
# "SAVE NAME" is replaced by the save this run loads). -BotSeconds makes the bot leave after that long;
# -BotRestart starts it again that many seconds after the first frame. -SaveCopy loads a copy of the save
# ("KP TEST <save>", deleted afterwards), so a run that saves never touches the original.
#
# Needs KenshiPure installed (scripts\install-client.ps1) and the network programs built
# (scripts\build-net.ps1). Kenshi must be closed. Kenshi is started through Steam (steam://rungameid).
param(
    [Parameter(Mandatory = $true)][string]$GameDir,
    [Parameter(Mandatory = $true)][string]$Save,
    [int]$Seconds = 200,
    [string]$BotArgs = "--around-first --radius 300 --speed 60",
    [string]$Grep = "standin:|remote:|autoload:|exception|FAIL|net: (welcomed|connection|player)",
    [int]$StartTimeout = 240,
    [string]$Cfg = "",
    [string]$Steps = "",
    [int]$BotSeconds = 0,
    [int]$BotRestart = 0,
    [switch]$SaveCopy,
    [switch]$NoReKenshi
)
$ErrorActionPreference = "Stop"
$cfgSets = $Cfg   # PowerShell names ignore case: $cfg below is kenshi.cfg and would overwrite -Cfg
$RepoRoot = Split-Path -Parent $PSScriptRoot
$mod = Join-Path $GameDir "mods\KenshiPure"
$cfg = Join-Path $GameDir "kenshi.cfg"
$log = Join-Path $mod "KenshiPure.log"
$net = Join-Path $RepoRoot "build\net"
$saveRoot = Join-Path $GameDir "save"
if (Get-Process kenshi_x64 -ErrorAction SilentlyContinue) { throw "Kenshi is running - close it first." }
if (-not (Test-Path (Join-Path $mod "KenshiPure.dll"))) { throw "KenshiPure is not installed in $GameDir (scripts\install-client.ps1)." }
if (-not (Test-Path (Join-Path $saveRoot $Save))) { throw "No save named '$Save' in $saveRoot." }

Add-Type @"
using System; using System.Runtime.InteropServices;
public static class KpWin {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
"@
Add-Type -AssemblyName System.Windows.Forms

$backup = "$cfg.kp-test-backup"
$kpCfg = Join-Path $mod "KenshiPure.cfg"
$kpBackup = "$kpCfg.kp-test-backup"
$plugins = Join-Path $GameDir "Plugins_x64.cfg"
$pluginsBackup = "$plugins.kp-test-backup"
$copyName = "KP TEST $Save"
$copyDir = Join-Path $saveRoot $copyName
# Deletes the save copy: only a folder directly under save\ whose name starts with "KP TEST ".
function Remove-SaveCopy {
    $leaf = Split-Path -Leaf $copyDir
    if ((Split-Path -Parent $copyDir) -eq $saveRoot -and $leaf.StartsWith("KP TEST ") -and $leaf -ne $Save -and (Test-Path -LiteralPath $copyDir)) {
        Remove-Item -LiteralPath $copyDir -Recurse -Force -Confirm:$false
    }
}

# A run that was killed before its cleanup leaves backups, autoload.txt and a save copy: restore them first.
if (Test-Path $backup) { Move-Item $backup $cfg -Force; Write-Host "restored kenshi.cfg left over by an aborted run" }
if (Test-Path $kpBackup) { Move-Item $kpBackup $kpCfg -Force; Write-Host "restored KenshiPure.cfg left over by an aborted run" }
if (Test-Path $pluginsBackup) { Move-Item $pluginsBackup $plugins -Force; Write-Host "restored Plugins_x64.cfg left over by an aborted run" }
Remove-Item (Join-Path $mod "autoload.txt") -ErrorAction SilentlyContinue
Remove-SaveCopy
Get-Process kp-server, kp-bot -ErrorAction SilentlyContinue | Stop-Process -Force -Confirm:$false
$server = $null; $bot = $null; $bot2 = $null; $game = $null; $exitedAt = $null
# Waits up to $sec seconds; returns early when Kenshi has exited by itself (a quit step).
function Wait-Run([int]$sec) {
    $until = (Get-Date).AddSeconds($sec)
    while ((Get-Date) -lt $until) {
        if ($script:game -and $script:game.HasExited) { $script:exitedAt = Get-Date; return $false }
        Start-Sleep -Seconds 1
    }
    return $true
}
try {
    # Windowed for the test; the user's kenshi.cfg comes back in finally.
    Copy-Item $cfg $backup -Force
    $text = Get-Content $cfg -Raw
    $text = $text -replace "(?m)^Full Screen=.*$", "Full Screen=No" -replace "(?m)^Video Mode=.*$", "Video Mode=1600 x 900 @ 32-bit colour"
    Set-Content -Path $cfg -Value $text -NoNewline -Encoding ascii

    if ($NoReKenshi) {
        Copy-Item $plugins $pluginsBackup -Force
        $pl = @(Get-Content $plugins | Where-Object { $_.Trim() -ne "Plugin=RE_Kenshi" })
        Set-Content -Path $plugins -Value $pl -Encoding ascii
        Write-Host "Plugins_x64.cfg for this run: without RE_Kenshi"
    }

    # KenshiPure.cfg for this run (back in finally).
    Copy-Item $kpCfg $kpBackup -Force
    $sets = @{}
    foreach ($kv in ($cfgSets -split ";")) { if ($kv -match "^\s*([^=]+?)\s*=(.*)$") { $sets[$Matches[1]] = $Matches[2].Trim() } }
    $lines = @()
    foreach ($l in @(Get-Content $kpCfg)) {
        if ($l -match "^\s*([^=#]+?)\s*=") {
            $key = $Matches[1]
            if ($sets.ContainsKey($key)) { continue }
            if ($key -eq "standin") { continue }
        }
        $lines += $l
    }
    foreach ($k in $sets.Keys) { $lines += "$k=$($sets[$k])" }
    Set-Content -Path $kpCfg -Value $lines -Encoding ascii
    Write-Host "KenshiPure.cfg for this run: $($lines -join ' | ')"

    $loadName = $Save
    if ($SaveCopy) {
        Copy-Item -LiteralPath (Join-Path $saveRoot $Save) -Destination $copyDir -Recurse -Force
        $loadName = $copyName
        Write-Host "loading the copy '$copyName'"
    }
    $auto = @($loadName)
    foreach ($s in ($Steps -split ";")) { if ($s.Trim()) { $auto += $s.Trim().Replace("SAVE NAME", $loadName) } }
    Set-Content -Path (Join-Path $mod "autoload.txt") -Value ($auto -join "`n") -NoNewline -Encoding ascii

    $server = Start-Process -FilePath (Join-Path $net "Release\kp-server.exe") -ArgumentList "--local", "--log-poses" `
        -RedirectStandardOutput (Join-Path $net "kp-server.log") -RedirectStandardError (Join-Path $net "kp-server.err.log") -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 1
    $botArgList = @("--name", "bot") + ($BotArgs -split " ")
    if ($BotSeconds -gt 0) { $botArgList += @("--seconds", "$BotSeconds") }
    $bot = Start-Process -FilePath (Join-Path $net "Release\kp-bot.exe") -ArgumentList $botArgList `
        -RedirectStandardOutput (Join-Path $net "kp-bot.log") -RedirectStandardError (Join-Path $net "kp-bot.err.log") -WindowStyle Hidden -PassThru

    $startedAt = Get-Date
    Start-Process "steam://rungameid/233860"
    Write-Host "Kenshi starting through Steam; waiting for the first frame (at most $StartTimeout s)"
    # Kenshi's start dialog (OK button) is pressed by the plugin itself while autoload.txt is pending.
    $framed = $false
    while (((Get-Date) - $startedAt).TotalSeconds -lt $StartTimeout) {
        Start-Sleep -Seconds 2
        if ((Test-Path $log) -and (Get-Item $log).LastWriteTime -gt $startedAt -and (Select-String -Path $log -Pattern "frame: first tick" -Quiet)) { $framed = $true; break }
    }
    if (-not $framed) { throw "No first frame within $StartTimeout s (a launcher window waiting for a click?)." }
    # The game window exists now (the start dialog was another window): put it on the second monitor.
    $p = Get-Process kenshi_x64 -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    $game = $p
    if ($game) { $null = $game.Handle }   # keeps a handle, so the exit code can be read after it exits
    $target = [System.Windows.Forms.Screen]::AllScreens | Where-Object { -not $_.Primary } | Select-Object -First 1
    if ($p -and $target) {
        [KpWin]::SetWindowPos($p.MainWindowHandle, [IntPtr]::Zero, $target.Bounds.X + 40, $target.Bounds.Y + 40, 0, 0, 0x0001 -bor 0x0004 -bor 0x0010) | Out-Null
        Write-Host "Kenshi window moved to $($target.DeviceName)"
    }
    Write-Host "First frame; running the test for $Seconds s"
    if ($BotRestart -gt 0 -and $BotRestart -lt $Seconds) {
        if (Wait-Run $BotRestart) {
            Write-Host "starting the bot again"
            $bot2 = Start-Process -FilePath (Join-Path $net "Release\kp-bot.exe") -ArgumentList $botArgList `
                -RedirectStandardOutput (Join-Path $net "kp-bot2.log") -RedirectStandardError (Join-Path $net "kp-bot2.err.log") -WindowStyle Hidden -PassThru
            $null = Wait-Run ($Seconds - $BotRestart)
        }
    }
    else { $null = Wait-Run $Seconds }
    if ($exitedAt) { Write-Host ("Kenshi exited by itself {0:N0} s after the first frame, exit code {1}" -f ($exitedAt - $startedAt).TotalSeconds, $game.ExitCode) }
    elseif ($Steps -match "\bquit\b") { Write-Host "Kenshi did NOT exit after the quit step - it is killed now" }
}
finally {
    Get-Process kenshi_x64 -ErrorAction SilentlyContinue | Stop-Process -Force -Confirm:$false   # no save on the way out
    foreach ($p in @($bot, $bot2, $server)) { if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -Confirm:$false } }
    Start-Sleep -Seconds 2
    if (Test-Path $backup) { Move-Item $backup $cfg -Force }
    if (Test-Path $kpBackup) { Move-Item $kpBackup $kpCfg -Force }
    if (Test-Path $pluginsBackup) { Move-Item $pluginsBackup $plugins -Force }
    Remove-Item (Join-Path $mod "autoload.txt") -ErrorAction SilentlyContinue
    Remove-SaveCopy
}
Write-Host "--- KenshiPure.log ($Grep)"
Select-String -Path $log -Pattern $Grep | ForEach-Object { $_.Line }
if ($Steps -match "\bquit\b") {
    Write-Host "--- KenshiPureLoader.log (stop) and kp-server.log (leave)"
    Select-String -Path (Join-Path $mod "KenshiPureLoader.log") -Pattern "stopping KenshiPure|startRendering" | ForEach-Object { $_.Line }
    Select-String -Path (Join-Path $net "kp-server.log") -Pattern "left \(" | ForEach-Object { $_.Line }
    Get-ChildItem $GameDir -Filter "crashDump*.dmp" | Where-Object { $_.LastWriteTime -gt $startedAt } | ForEach-Object { Write-Host "CRASH DUMP: $($_.Name)" }
}
