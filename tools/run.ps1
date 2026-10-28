param(
    [Parameter(Mandatory=$true)][string]$Program,
    [string]$QtRoot=$env:QT_ROOT,
    [string]$DependencyRoot=$env:PALEO_DEPENDENCY_ROOT,
    [string[]]$RuntimeDirectories=@(),
    [Parameter(ValueFromRemainingArguments=$true)][string[]]$ProgramArguments
)
$ErrorActionPreference='Stop'
$directories=@($RuntimeDirectories)
if($QtRoot){$directories+=(Join-Path $QtRoot 'bin');$env:QT_PLUGIN_PATH=Join-Path $QtRoot 'plugins'}
if($DependencyRoot){$directories+=(Join-Path $DependencyRoot 'bin')}
if($directories.Count){$env:PATH=($directories -join ';')+';'+$env:PATH}
& $Program @ProgramArguments
exit $LASTEXITCODE
