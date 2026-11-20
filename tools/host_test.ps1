# Runs the host (PC) unit tests in WSL. Exit code = test binary exit code (number of failures).
$sh = wsl -d Ubuntu-22.04 -- wslpath -a ((Join-Path $PSScriptRoot 'wsl/host_test.sh') -replace '\\', '/')
wsl -d Ubuntu-22.04 -- bash $sh
exit $LASTEXITCODE
