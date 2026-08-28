@echo off
setlocal

schtasks.exe /End /TN "TFT DX11 Watcher" >nul 2>&1
schtasks.exe /Delete /TN "TFT DX11 Watcher" /F >nul 2>&1
taskkill.exe /IM TFTDx11Watcher.exe /T /F >nul 2>&1

echo Scheduled task removed and TFTDx11Watcher.exe stopped if it was running.
echo Engine.ini was not deleted or reset.
exit /b 0
