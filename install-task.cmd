@echo off
setlocal

set "ROOT=%~dp0"
for %%I in ("%ROOT%TFTDx11Watcher.exe") do set "WATCHER=%%~fI"

if not exist "%WATCHER%" (
    echo TFTDx11Watcher.exe was not found.
    echo Run build.cmd first.
    exit /b 1
)

schtasks.exe /Create /TN "TFT DX11 Watcher" /TR "\"%WATCHER%\"" /SC ONLOGON /RL LIMITED /F
if errorlevel 1 (
    echo Could not create the scheduled task.
    echo Run from a normal user session, or remove an existing task with the same name.
    exit /b 1
)

schtasks.exe /Run /TN "TFT DX11 Watcher" >nul
if errorlevel 1 (
    echo Task was created, but could not be started immediately.
    echo It will start at the next user logon.
    exit /b 1
)

echo Scheduled task installed and watcher started.
exit /b 0
