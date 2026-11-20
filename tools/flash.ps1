# Flashes a firmware build made in WSL, from Windows, and optionally opens the serial monitor.
#   ./tools/flash.ps1 -Port COM17                 # flash build/esp32
#   ./tools/flash.ps1 -Port COM17 -Erase -Monitor # wipe all settings first, then watch the log
# Why not `idf.py flash`: the build folder was configured in WSL (Linux paths), so Windows idf.py
# would try to reconfigure it. esptool + the generated flash_args file need no configuration.
param(
    [Parameter(Mandatory)] [string] $Port,
    [string] $Profile = 'esp32',
    [switch] $Erase,      # erase the whole flash first (settings, keys, per-device password)
    [switch] $Monitor     # open the serial monitor afterwards (Ctrl+] quits)
)
$ErrorActionPreference = 'Stop'
$eim = Get-Content 'C:\Espressif\tools\eim_idf.json' -Raw | ConvertFrom-Json
$idf = $eim.idfInstalled | Where-Object { $_.id -eq $eim.idfSelectedId } | Select-Object -First 1
if (-not $idf) { $idf = $eim.idfInstalled | Select-Object -First 1 }
. $idf.activationScript *> $null          # puts the IDF Python venv and toolchains on PATH

$build = Join-Path $PSScriptRoot "..\build\$Profile"
$elf = Get-ChildItem (Join-Path $build '*.elf') | Select-Object -First 1
Push-Location $build
try {
    $erase = @(); if ($Erase) { $erase = @('--erase-all') }
    python -m esptool --chip auto -p $Port -b 460800 --before default-reset --after hard-reset `
        write-flash @erase '@flash_args'
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally { Pop-Location }
if ($Monitor) { python -m esp_idf_monitor -p $Port $elf.FullName }
