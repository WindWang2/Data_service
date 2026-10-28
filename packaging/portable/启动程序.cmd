@echo off
setlocal
call "%~dp0portable-env.cmd"
if not exist "%~dp0app\paleo_data_demo.exe" goto missing
start "" "%~dp0app\paleo_data_demo.exe" --tab data 1>>"%~dp0logs\application.log" 2>&1
exit /b 0
:missing
echo Program files are missing. Extract the entire ZIP before running.
pause
exit /b 1
