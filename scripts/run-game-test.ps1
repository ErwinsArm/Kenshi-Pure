# Runs one in-game test of KenshiPure without anyone at the game: starts the relay server and the test
# bot, starts Kenshi windowed (on the second monitor if there is one), lets KenshiPure press OK in
# Kenshi's start dialog and load a save at
# the main menu (mods\KenshiPure\autoload.txt, read once and deleted by the plugin), waits, then
# closes Kenshi WITHOUT saving and restores kenshi.cfg. Prints the KenshiPure log lines of the run.
#
#   powershell -ExecutionPolicy Bypass -File scripts\run-game-test.ps1 -GameDir <Kenshi folder> -Save "<save name>"
#              [-Seconds 200] [-BotArgs "--around-first --radius 300 --speed 60"] [-Grep "standin:"]
#
# Needs KenshiPure installed (scripts\install-client.ps1) and the network programs built
# (scripts\build-net.ps1). Kenshi must be closed. Kenshi is started through Steam (steam://rungameid).
param(
    [Parameter(Mandatory = $true)][string]$GameDir,
    [Parameter(Mandatory = $true)][string]$Save,
    [int]$Seconds = 200,
    [string]$BotArgs = "--around-first --radius 300 --speed 60",
    [string]$Grep = "standin:|autoload:|exception|FAIL|net: (welcomed|connection)",
    [int]$StartTimeout = 240
)
$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$mod = Join-Path $GameDir "mods\KenshiPure"
$cfg = Join-Path $GameDir "kenshi.cfg"
$log = Join-Path $mod "KenshiPure.log"
$net = Join-Path $RepoRoot "build\net"
if (Get-Process kenshi_x64 -ErrorAction SilentlyContinue) { throw "Kenshi is running - close it first." }
if (-not (Test-Path (Join-Path $mod "KenshiPure.dll"))) { throw "KenshiPure is not installed in $GameDir (scripts\install-client.ps1)." }
if (-not (Test-Path (Join-Path $GameDir "save\$Save"))) { throw "No save named '$Save' in $GameDir\save." }

Add-Type @"
using System; using System.Runtime.InteropServices;
public static class KpWin {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
"@
Add-Type -AssemblyName System.Windows.Forms

$backup = "$cfg.kp-test-backup"
# A run that was killed before its cleanup leaves the backup and autoload.txt: restore them first.
if (Test-Path $backup) { Move-Item $backup $cfg -Force; Write-Host "restored kenshi.cfg left over by an aborted run" }
Remove-Item (Join-Path $mod "autoload.txt") -ErrorAction SilentlyContinue
Get-Process kp-server, kp-bot -ErrorAction SilentlyContinue | Stop-Process -Force -Confirm:$false
$server = $null; $bot = $null
try {
    # Windowed for the test; the user's kenshi.cfg comes back in finally.
    Copy-Item $cfg $backup -Force
    $text = Get-Content $cfg -Raw
    $text = $text -replace "(?m)^Full Screen=.*$", "Full Screen=No" -replace "(?m)^Video Mode=.*$", "Video Mode=1600 x 900 @ 32-bit colour"
    Set-Content -Path $cfg -Value $text -NoNewline -Encoding ascii
    Set-Content -Path (Join-Path $mod "autoload.txt") -Value $Save -NoNewline -Encoding ascii

    $server = Start-Process -FilePath (Join-Path $net "Release\kp-server.exe") -ArgumentList "--local", "--log-poses" `
        -RedirectStandardOutput (Join-Path $net "kp-server.log") -RedirectStandardError (Join-Path $net "kp-server.err.log") -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 1
    $bot = Start-Process -FilePath (Join-Path $net "Release\kp-bot.exe") -ArgumentList (@("--name", "bot") + ($BotArgs -split " ")) `
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
    $target = [System.Windows.Forms.Screen]::AllScreens | Where-Object { -not $_.Primary } | Select-Object -First 1
    if ($p -and $target) {
        [KpWin]::SetWindowPos($p.MainWindowHandle, [IntPtr]::Zero, $target.Bounds.X + 40, $target.Bounds.Y + 40, 0, 0, 0x0001 -bor 0x0004 -bor 0x0010) | Out-Null
        Write-Host "Kenshi window moved to $($target.DeviceName)"
    }
    Write-Host "First frame; running the test for $Seconds s"
    Start-Sleep -Seconds $Seconds
}
finally {
    Get-Process kenshi_x64 -ErrorAction SilentlyContinue | Stop-Process -Force -Confirm:$false   # no save on the way out
    foreach ($p in @($bot, $server)) { if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -Confirm:$false } }
    Start-Sleep -Seconds 2
    if (Test-Path $backup) { Move-Item $backup $cfg -Force }
    Remove-Item (Join-Path $mod "autoload.txt") -ErrorAction SilentlyContinue
}
Write-Host "--- KenshiPure.log ($Grep)"
Select-String -Path $log -Pattern $Grep | ForEach-Object { $_.Line }
