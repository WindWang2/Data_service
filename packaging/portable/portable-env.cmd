@echo off
rem Called inside a SETLOCAL scope; never modifies the machine environment.
set "PATH=%~dp0app;%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem"
set "QT_PLUGIN_PATH=%~dp0app"
set "QT_QPA_PLATFORM_PLUGIN_PATH=%~dp0app\platforms"
set "QT_QPA_PLATFORM="
set "QT_QPA_PLATFORMTHEME="
set "QT_STYLE_OVERRIDE="
set "QT_QPA_FONTDIR="
set "QML2_IMPORT_PATH="
set "QML_IMPORT_PATH="
set "PALEO_FREEXL_LIBRARY="
set "QT_FORCE_STDERR_LOGGING=1"
if not exist "%~dp0logs" mkdir "%~dp0logs"
exit /b 0
