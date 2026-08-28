@echo off
setlocal

cd /d "%~dp0"

where.exe cl.exe >nul 2>&1
if errorlevel 1 (
    echo cl.exe was not found.
    echo Run this script from a Visual Studio Developer Command Prompt.
    exit /b 1
)

where.exe rc.exe >nul 2>&1
if errorlevel 1 (
    echo rc.exe was not found.
    echo Run this script from a Visual Studio Developer Command Prompt.
    exit /b 1
)

rc.exe /nologo /fo TFTDx11Watcher.res TFTDx11Watcher.rc
if errorlevel 1 (
    echo Resource compilation failed.
    exit /b 1
)

cl.exe /nologo /O2 /GL /W4 /WX TFTDx11Watcher.c TFTDx11Watcher.res ^
    /link /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO /SUBSYSTEM:WINDOWS shell32.lib ^
    /OUT:TFTDx11Watcher.exe
if errorlevel 1 (
    if exist TFTDx11Watcher.res del /q TFTDx11Watcher.res
    echo Build failed.
    exit /b 1
)

if exist TFTDx11Watcher.obj del /q TFTDx11Watcher.obj
if exist TFTDx11Watcher.res del /q TFTDx11Watcher.res
echo Build succeeded: TFTDx11Watcher.exe
exit /b 0
