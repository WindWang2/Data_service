param(
    [string]$BuildDirectory=(Join-Path $PSScriptRoot 'build-examples'),
    [string]$SdkDirectory=(Join-Path $PSScriptRoot 'sdk'),
    [string]$QtRoot=$env:QT_ROOT,
    [string]$DependencyRoot=$env:PALEO_DEPENDENCY_ROOT,
    [string]$CompilerSetup=$env:PALEO_MSVC_SETUP,
    [string]$CMake='cmake',
    [string]$Generator='Ninja',
    [string]$Ninja='',
    [string]$Configuration='Release'
)
$ErrorActionPreference='Stop'
if($CompilerSetup){
    $setupPath=(Resolve-Path -LiteralPath $CompilerSetup).Path
    $lines=& cmd.exe /d /c "`"$setupPath`" >nul && set"
    if($LASTEXITCODE -ne 0){throw 'Compiler environment initialization failed'}
    foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
}
if(-not (Test-Path -LiteralPath (Join-Path $SdkDirectory 'lib/cmake/PaleoDataService/PaleoDataServiceConfig.cmake'))){
    throw 'Install the SDK first: cmake --install <build-directory> --prefix <sdk-directory>'
}
$prefix=@($SdkDirectory);if($QtRoot){$prefix+=$QtRoot};if($DependencyRoot){$prefix+=$DependencyRoot}
$configureArguments=@('-S',(Join-Path $PSScriptRoot 'examples/getting_started'),'-B',$BuildDirectory,'-G',$Generator,"-DCMAKE_BUILD_TYPE=$Configuration","-DCMAKE_PREFIX_PATH=$($prefix -join ';')")
if($Ninja){$configureArguments+="-DCMAKE_MAKE_PROGRAM=$Ninja"}
if($DependencyRoot){
    $configureArguments+="-DZLIB_ROOT=$DependencyRoot"
    $zlibLibrary=Join-Path $DependencyRoot 'lib/zlib.lib'
    if(Test-Path -LiteralPath $zlibLibrary){$configureArguments+=@("-DZLIB_INCLUDE_DIR=$(Join-Path $DependencyRoot 'include')","-DZLIB_LIBRARY=$zlibLibrary")}
}
& $CMake @configureArguments
if($LASTEXITCODE -ne 0){throw 'Example configuration failed'}
& $CMake --build $BuildDirectory --config $Configuration --parallel 8
if($LASTEXITCODE -ne 0){throw 'Example compilation failed'}
Write-Output "Examples built: $BuildDirectory"
