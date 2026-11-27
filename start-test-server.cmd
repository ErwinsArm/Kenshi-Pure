@echo off
rem Double-click: starts the KenshiPure relay server and the test bot, each in its own window,
rem for this PC only. Close a window (or press Ctrl+C in it) to stop it.
rem Their output also goes to build\net\kp-server.log and build\net\kp-bot.log (gitignored),
rem so the logs can be read after a test. Build them first: scripts\build-net.ps1 (docs\setup.md).
cd /d "%~dp0"
if not exist "build\net\Release\kp-server.exe" (
    echo kp-server.exe is not built yet. Run: powershell -ExecutionPolicy Bypass -File scripts\build-net.ps1
    pause
    exit /b 1
)
start "KenshiPure server" powershell -NoProfile -ExecutionPolicy Bypass -Command "& '.\build\net\Release\kp-server.exe' --local --log-poses | Tee-Object -FilePath 'build\net\kp-server.log'"
rem Wait a second so the server listens before the bot connects (ping works without a console input).
ping -n 2 127.0.0.1 >nul
start "KenshiPure test bot" powershell -NoProfile -ExecutionPolicy Bypass -Command "& '.\build\net\Release\kp-bot.exe' --name bot --around-first --radius 300 --speed 60 | Tee-Object -FilePath 'build\net\kp-bot.log'"
