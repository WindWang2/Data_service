@echo off
setlocal
call "%~dp0portable-env.cmd"
set "QT_QPA_PLATFORM=offscreen"
set "QT_QPA_FONTDIR=%SystemRoot%\Fonts"
set "pdsCheck=%~dp0logs\check-%RANDOM%-%RANDOM%"
if exist "%pdsCheck%" goto collision
mkdir "%pdsCheck%"
set "pdsLog=%pdsCheck%\self-check.txt"
set "pdsFailures=0"
echo Paleo Data Service portable self-check >"%pdsLog%"
ver >>"%pdsLog%"
echo Logs: "%pdsCheck%"
call :run service_tests.exe
call :run text_parser_tests.exe
call :run aux_parser_tests.exe
call :run seismic_service_tests.exe
call :run seismic_hd_tests.exe
call :run api_demo_tests.exe
call :run spreadsheet_xml_tests.exe
call :run core_asset_tests.exe
call :run format_compatibility_tests.exe
call :run image_preview_tests.exe
call :run segyio_access_tests.exe
call :run seismic_processing_tests.exe
call :run seismic_ui_tests.exe
call :run segyio_bounds_tests.exe
call :run file_digest_tests.exe
call :run import_performance_tests.exe
echo Creating isolated synthetic test data...
"%~dp0app\create_api_demo_project.exe" "%pdsCheck%\fixture" >>"%pdsLog%" 2>&1
if not "%errorlevel%"=="0" goto generatorFailed
call :gui curve 1
call :gui section 1
call :gui slice 1
goto finished
:generatorFailed
set /a pdsFailures+=1 >nul
echo FAIL synthetic project generation >>"%pdsLog%"
:finished
echo FAILURES=%pdsFailures% >>"%pdsLog%"
echo.
if "%pdsFailures%"=="0" (echo PASS - all portable checks completed.) else (echo FAIL - inspect self-check.txt for details.)
echo Logs: "%pdsCheck%"
if /i not "%~1"=="--no-pause" pause
exit /b %pdsFailures%
:run
echo Checking %~1...
echo --- %~1 --- >>"%pdsLog%"
"%~dp0app\%~1" >>"%pdsLog%" 2>&1
if not "%errorlevel%"=="0" goto runFailed
echo PASS %~1 >>"%pdsLog%"
exit /b 0
:runFailed
set /a pdsFailures+=1 >nul
echo FAIL %~1 >>"%pdsLog%"
exit /b 0
:gui
echo Checking %~1 data and rendering...
start "" /b /wait "%~dp0app\paleo_data_demo.exe" --project "%pdsCheck%\fixture" --example "%~1" --example-resource "%~2" --example-first 200 --run-example --smoke-ui "%pdsCheck%\%~1.png" --export-result "%pdsCheck%\%~1.json" >>"%pdsLog%" 2>&1
if not "%errorlevel%"=="0" goto guiFailed
if not exist "%pdsCheck%\%~1.json" goto guiFailed
echo PASS GUI %~1 >>"%pdsLog%"
exit /b 0
:guiFailed
set /a pdsFailures+=1 >nul
echo FAIL GUI %~1 >>"%pdsLog%"
exit /b 0
:collision
echo A test folder already exists. Please run this check again.
if /i not "%~1"=="--no-pause" pause
exit /b 1
