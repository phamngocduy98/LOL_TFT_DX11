@echo off
setlocal

set "ROOT=%~dp0"
if not exist "%ROOT%TFTDx11Watcher.exe" (
    echo TFTDx11Watcher.exe was not found.
    echo Run build.cmd first.
    exit /b 1
)

start "" /b "%ROOT%TFTDx11Watcher.exe"
timeout /t 1 /nobreak >nul

set "ENGINE=%LOCALAPPDATA%\TFT\Saved\Config\WindowsClient\Engine.ini"
echo.
echo Current Engine.ini: "%ENGINE%"
echo ------------------------------------------------------------
if exist "%ENGINE%" (
    type "%ENGINE%"
) else (
    echo Engine.ini is not present yet. The watcher may still be initializing.
)
echo ------------------------------------------------------------
echo.
echo Launch TFT normally from the Riot/League client.
echo After TFT exits, inspect Engine.ini again to verify the DX11 block was restored.
echo The watcher does not launch TFT.
pause
exit /b 0
