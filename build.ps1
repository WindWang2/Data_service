param(
    [string]$BuildDirectory=(Join-Path $PSScriptRoot 'build'),
    [string]$Configuration='Release',
    [string[]]$Targets=@(),
    [string]$QtRoot=$env:QT_ROOT,
    [string]$DependencyRoot=$env:PALEO_DEPENDENCY_ROOT,
    [string]$CompilerSetup=$env:PALEO_MSVC_SETUP,
    [string]$CMake='cmake',
    [string]$Generator='Ninja',
    [string]$Ninja='',
    [switch]$SkipFreeXL
)
$ErrorActionPreference='Stop'
# Run in a compiler developer shell, or provide a setup .bat file explicitly.
if($CompilerSetup){
    $setupPath=(Resolve-Path -LiteralPath $CompilerSetup).Path
    $lines=& cmd.exe /d /c "`"$setupPath`" >nul && set"
    if($LASTEXITCODE -ne 0){throw 'Compiler environment initialization failed'}
    foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
}
if($env:OS -eq 'Windows_NT' -and -not (Get-Command cl.exe -ErrorAction SilentlyContinue)){
    throw 'Open an x64 Visual Studio developer shell, or pass -CompilerSetup <setup.bat>.'
}
$prefix=@();if($QtRoot){$prefix+=$QtRoot;$env:PATH="$(Join-Path $QtRoot 'bin');$env:PATH"}
if($DependencyRoot){$prefix+=$DependencyRoot;$env:PATH="$(Join-Path $DependencyRoot 'bin');$env:PATH"}
 $configureArguments=@('-S',$PSScriptRoot,'-B',$BuildDirectory,'-G',$Generator,"-DCMAKE_BUILD_TYPE=$Configuration")
if($prefix.Count){$configureArguments+="-DCMAKE_PREFIX_PATH=$($prefix -join ';')"}
if($Ninja){$configureArguments+="-DCMAKE_MAKE_PROGRAM=$Ninja"}
if($DependencyRoot){
    $configureArguments+="-DZLIB_ROOT=$DependencyRoot"
    $zlibLibrary=Join-Path $DependencyRoot 'lib/zlib.lib'
    if(Test-Path -LiteralPath $zlibLibrary){$configureArguments+=@("-DZLIB_INCLUDE_DIR=$(Join-Path $DependencyRoot 'include')","-DZLIB_LIBRARY=$zlibLibrary")}
}
& $CMake @configureArguments
if($LASTEXITCODE -ne 0){throw 'CMake configure failed'}
 $configureArguments=@('--build',$BuildDirectory,'--config',$Configuration,'--parallel','8')
if($Targets.Count){$configureArguments+=@('--target')+$Targets}
& $CMake @configureArguments
if($LASTEXITCODE -ne 0){throw 'CMake build failed'}
if($env:OS -eq 'Windows_NT' -and -not $SkipFreeXL){
    $freexlBuild=Join-Path $BuildDirectory 'freexl-runtime'
    & (Join-Path $PSScriptRoot 'vendor/freexl/build_windows.ps1') -OutputDirectory $freexlBuild
    $binaryDirectory=$BuildDirectory
    if(Test-Path -LiteralPath (Join-Path $BuildDirectory $Configuration)){$binaryDirectory=Join-Path $BuildDirectory $Configuration}
    Copy-Item -LiteralPath (Join-Path $freexlBuild 'freexl.dll') -Destination (Join-Path $binaryDirectory 'freexl.dll') -Force
}
Write-Output "Build ready: $BuildDirectory"
