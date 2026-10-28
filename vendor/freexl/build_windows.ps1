param(
    [string]$OutputDirectory=(Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'build/freexl-runtime'),
    [string]$CompilerSetup=$env:PALEO_MSVC_SETUP,
    [switch]$Trace
)
$ErrorActionPreference='Stop'
if($CompilerSetup){
    $setupPath=(Resolve-Path -LiteralPath $CompilerSetup).Path
    $lines=& cmd.exe /d /c "`"$setupPath`" >nul && set"
    if($LASTEXITCODE -ne 0){throw 'MSVC setup failed'}
    foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
}
foreach($tool in @('cl.exe','lib.exe','link.exe')){
    if(-not (Get-Command $tool -ErrorAction SilentlyContinue)){throw "Missing $tool. Use an x64 MSVC developer shell or pass -CompilerSetup <setup.bat>."}
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$out=(Resolve-Path -LiteralPath $OutputDirectory).Path
$source=Join-Path $PSScriptRoot 'freexl-2.0.0'
$windows=Join-Path $PSScriptRoot 'windows'
& lib.exe /nologo /machine:x64 "/def:$windows/iconv.def" "/out:$out/iconv.lib"
if($LASTEXITCODE -ne 0){throw 'iconv import library failed'}
$arguments=@('/nologo','/O2','/MD','/W3','/D_CRT_SECURE_NO_WARNINGS','/DDLL_EXPORT','/DOMIT_XMLDOC',"/I$source","/I$source/headers","/I$windows",'/c',"$source/src/freexl.c","/Fo$out/freexl.obj")
if($Trace){$arguments+='/DPALEO_FREEXL_TRACE'}
& cl.exe @arguments
if($LASTEXITCODE -ne 0){throw 'FreeXL compilation failed'}
& link.exe /nologo /dll "/out:$out/freexl.dll" "/implib:$out/freexl.lib" "$out/freexl.obj" "$out/iconv.lib"
if($LASTEXITCODE -ne 0){throw 'FreeXL link failed'}
Write-Output "Built $out/freexl.dll (XLS BIFF; XLSX uses the application's separate parser)."
Write-Output 'At runtime, iconv-2.dll must be accessible beside the application or on PATH.'
