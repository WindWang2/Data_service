param(
    [string]$BuildDirectory=(Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'build'),
    [string]$QtRoot=$env:QT_ROOT,
    [string]$DependencyRoot=$env:PALEO_DEPENDENCY_ROOT,
    [string]$CTest='ctest',
    [string]$Configuration='Release'
)
$ErrorActionPreference='Stop'
# Upstream binary workbooks are intentionally absent from this source-only repository.
# Run the application's synthetic parser tests; do not claim upstream fixture coverage.
if(-not (Test-Path -LiteralPath (Join-Path $BuildDirectory 'CTestTestfile.cmake'))){throw 'Build the main project with BUILD_TESTING=ON first.'}
if($QtRoot){$env:PATH="$(Join-Path $QtRoot 'bin');$env:PATH";$env:QT_PLUGIN_PATH=Join-Path $QtRoot 'plugins'}
if($DependencyRoot){$env:PATH="$(Join-Path $DependencyRoot 'bin');$env:PATH"}
$env:QT_QPA_PLATFORM='offscreen'
& $CTest --test-dir $BuildDirectory -C $Configuration --output-on-failure --no-tests=error -R '^(aux_parser_tests|format_compatibility_tests|spreadsheet_xml_tests)$'
if($LASTEXITCODE -ne 0){throw 'Synthetic parser tests failed'}
Write-Output 'Application synthetic parser tests passed. Upstream binary-fixture tests were not run.'
