# Windows wrapper: runs sign_csr.sh with Git for Windows' bash and OpenSSL (no WSL needed).
$bash = Join-Path $env:ProgramFiles 'Git\bin\bash.exe'
if (-not (Test-Path $bash)) { Write-Error "Git for Windows bash not found: $bash"; exit 1 }
& $bash ((Join-Path $PSScriptRoot 'sign_csr.sh') -replace '\\', '/') @args
exit $LASTEXITCODE
