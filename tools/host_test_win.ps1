# Runs the host unit tests natively on Windows with a MinGW gcc (no WSL needed).
#   ./tools/host_test_win.ps1                       # gcc from PATH
#   ./tools/host_test_win.ps1 -Gcc E:\...\c\bin\gcc.exe
# Covers gw_core (45 tests). The 10 NVS storage tests need WSL: ./tools/host_test.ps1 runs everything.
# Exit code = number of failures.
param(
    [string] $Gcc = '',
    [string] $IdfPath = ''
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

# --- tools ---------------------------------------------------------------------------------
if (-not $Gcc) { $Gcc = (Get-Command gcc -ErrorAction SilentlyContinue).Source }
if (-not $Gcc -or -not (Test-Path $Gcc)) { Write-Error 'MinGW gcc not found: pass -Gcc <path to gcc.exe>'; exit 1 }
if (-not $IdfPath) { $IdfPath = $env:IDF_PATH }
if (-not $IdfPath -and (Test-Path 'C:\Espressif\tools\eim_idf.json')) {
    $eim = Get-Content 'C:\Espressif\tools\eim_idf.json' -Raw | ConvertFrom-Json
    $sel = $eim.idfInstalled | Where-Object { $_.id -eq $eim.idfSelectedId } | Select-Object -First 1
    if ($sel) { $IdfPath = $sel.path }
}
if (-not $IdfPath -or -not (Test-Path "$IdfPath\components\mbedtls")) { Write-Error 'ESP-IDF not found: pass -IdfPath <esp-idf folder>'; exit 1 }
function Find-Tool([string] $name, [string] $pattern) {
    $t = (Get-Command $name -ErrorAction SilentlyContinue).Source
    if (-not $t) { $t = Get-ChildItem $pattern -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName }
    if (-not $t) { Write-Error "$name not found (install it or the ESP-IDF tools)"; exit 1 }
    return $t
}
$cmake = Find-Tool 'cmake' 'C:\Espressif\tools\cmake\*\bin\cmake.exe'
$ninja = Find-Tool 'ninja' 'C:\Espressif\tools\ninja\*\ninja.exe'
$bash = Join-Path $env:ProgramFiles 'Git\bin\bash.exe'
$cjson = Join-Path $repo 'managed_components\espressif__cjson\cJSON'
if (-not (Test-Path "$cjson\cJSON.c")) { Write-Error "cJSON not downloaded yet ($cjson): run ./tools/build.ps1 esp32 once"; exit 1 }

# The test .exe needs the compiler's runtime DLLs (libgcc, libwinpthread) on PATH.
$env:PATH = (Split-Path $Gcc) + ';' + $env:PATH

# --- fixtures (throwaway certificates; never committed) -----------------------------------
& $bash ((Join-Path $repo 'tools/pki/make_test_fixtures.sh') -replace '\\', '/') ((Join-Path $repo 'test/host/fixtures') -replace '\\', '/') | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Error 'fixture generation failed (needs Git for Windows bash + openssl)'; exit 1 }

# --- configure, build, run ------------------------------------------------------------------
# Native tools write progress to stderr; in Windows PowerShell 'Stop' would turn that into errors.
$ErrorActionPreference = 'Continue'
$src = Join-Path $repo 'test\host_win'
$bld = Join-Path $src 'build'
$log = Join-Path $src 'build.log'
function ConvertTo-CMakePath([string] $p) { $p -replace '\\', '/' }   # CMake reads \ as an escape
cmd /c "`"$cmake`" -G Ninja -S `"$src`" -B `"$bld`" -DCMAKE_MAKE_PROGRAM=`"$(ConvertTo-CMakePath $ninja)`" -DCMAKE_C_COMPILER=`"$(ConvertTo-CMakePath $Gcc)`" -DGW_IDF_PATH=`"$(ConvertTo-CMakePath $IdfPath)`" -DGW_CJSON_DIR=`"$(ConvertTo-CMakePath $cjson)`" > `"$log`" 2>&1"
if ($LASTEXITCODE -eq 0) { cmd /c "`"$cmake`" --build `"$bld`" >> `"$log`" 2>&1" }
if ($LASTEXITCODE -ne 0) {
    Write-Host "host_win build FAILED - see $log"
    Select-String -Path $log -Pattern 'error|Error' | Select-Object -First 20 | ForEach-Object { $_.Line }
    exit 1
}
& (Join-Path $bld 'host_test_win.exe')
exit $LASTEXITCODE
