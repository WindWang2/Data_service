@echo off
setlocal
call "%~dp0portable-env.cmd"
if exist "%~dp0api_example_project\data.sqlite" goto ready
echo Creating a small SYNTHETIC example project on this computer...
"%~dp0app\create_api_demo_project.exe" "%~dp0api_example_project" 1>>"%~dp0logs\synthetic-setup.log" 2>&1
if errorlevel 1 goto failed
:ready
start "" "%~dp0app\paleo_data_demo.exe" --project "%~dp0api_example_project" --example section --run-example 1>>"%~dp0logs\application.log" 2>&1
exit /b 0
:failed
echo Example setup failed. See logs\synthetic-setup.log.
echo If this folder is read-only, extract the package to a writable local folder.
echo Existing projects will never be overwritten. See the testing guide.
pause
exit /b 1
