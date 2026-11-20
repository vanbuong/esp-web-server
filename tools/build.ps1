# Builds one firmware profile in WSL. Example: ./tools/build.ps1 esp32 size
param(
    [Parameter(Mandatory)] [string] $Profile,
    [Parameter(ValueFromRemainingArguments)] [string[]] $Actions
)
$sh = wsl -d Ubuntu-22.04 -- wslpath -a ((Join-Path $PSScriptRoot 'wsl/build.sh') -replace '\\', '/')
wsl -d Ubuntu-22.04 -- bash $sh $Profile @Actions
exit $LASTEXITCODE
