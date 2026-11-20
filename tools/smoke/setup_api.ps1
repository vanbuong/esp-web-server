# Smoke test for the setup-mode JSON API. Run on a PC joined to the device's setup Wi-Fi.
#   ./tools/smoke/setup_api.ps1 -Ssid "<home ssid>" -WifiPass "<home password>"
# Needs a freshly erased device (step "password", default login admin/admin).
# Exit code = number of failed checks.
param(
    [string] $Base = 'http://192.168.4.1',
    [Parameter(Mandatory)] [string] $Ssid,
    [string] $WifiPass = '',
    [string] $NewPassword = 'Learning-123'
)

$tmp = Join-Path ([IO.Path]::GetTempPath()) "gw_smoke_$PID"
New-Item -ItemType Directory -Force $tmp | Out-Null
$script:fails = 0

# One HTTP request through curl.exe. Bodies go through a file because Windows PowerShell 5.1
# mangles double quotes in arguments to native programs.
function Invoke-Api([string] $Method, [string] $Path, [string] $Body = $null,
                    [string] $Cookie = $null, [string] $ContentType = 'application/json') {
    $out = Join-Path $tmp 'body.txt'; $hdr = Join-Path $tmp 'headers.txt'
    $args = @('-s', '-m', '15', '-o', $out, '-D', $hdr, '-w', '%{http_code}', '-X', $Method)
    if ($Cookie) { $args += @('-b', "gw_session=$Cookie") }
    if ($null -ne $Body) {
        $bf = Join-Path $tmp 'req.txt'
        [IO.File]::WriteAllText($bf, $Body, (New-Object Text.UTF8Encoding($false)))
        $args += @('-H', "Content-Type: $ContentType", '--data-binary', "@$bf")
    }
    $code = & curl.exe @args "$Base$Path"
    $text = if (Test-Path $out) { [IO.File]::ReadAllText($out) } else { '' }
    $set = $null
    if (Test-Path $hdr) {
        $m = Select-String -Path $hdr -Pattern 'gw_session=([0-9a-f]{64})' | Select-Object -First 1
        if ($m) { $set = $m.Matches[0].Groups[1].Value }
    }
    Remove-Item $out, $hdr -ErrorAction SilentlyContinue
    [pscustomobject]@{ Code = [int]$code; Body = $text; Cookie = $set }
}

function Test-Check([string] $Name, [bool] $Ok, [string] $Detail = '') {
    if ($Ok) { Write-Host "PASS  $Name" -ForegroundColor Green }
    else { Write-Host "FAIL  $Name  $Detail" -ForegroundColor Red; $script:fails++ }
}

$r = Invoke-Api GET '/api/status'
Test-Check 'status: step password' ($r.Code -eq 200 -and $r.Body -match '"step":"password"') "$($r.Code) $($r.Body)"

$r = Invoke-Api GET '/api/setup/wifi/scan'
Test-Check 'scan before login -> 401' ($r.Code -eq 401) "$($r.Code)"

$r = Invoke-Api POST '/api/login' '{"user":"admin","password":"wrong"}'
Test-Check 'wrong login -> 401' ($r.Code -eq 401) "$($r.Code) $($r.Body)"

$r = Invoke-Api POST '/api/login' '{"user":"admin","password":"admin"}'
$c1 = $r.Cookie
Test-Check 'default login -> must_change_password' ($r.Code -eq 200 -and $r.Body -match '"must_change_password":true' -and $c1) "$($r.Code) $($r.Body)"

$r = Invoke-Api GET '/api/setup/wifi/scan' -Cookie $c1
Test-Check 'scan before password change -> 409 wrong_step' ($r.Code -eq 409 -and $r.Body -match 'wrong_step') "$($r.Code) $($r.Body)"

$r = Invoke-Api POST '/api/setup/password' '{"new_password":"short"}' -Cookie $c1
Test-Check 'weak password -> 400' ($r.Code -eq 400 -and $r.Body -match 'weak_password') "$($r.Code) $($r.Body)"

$r = Invoke-Api POST '/api/setup/password' ('{"new_password":"' + $NewPassword + '"}') -Cookie $c1
$c2 = $r.Cookie
Test-Check 'password change -> 200 + new cookie' ($r.Code -eq 200 -and $c2 -and $c2 -ne $c1) "$($r.Code) $($r.Body)"

$r = Invoke-Api GET '/api/setup/wifi/scan' -Cookie $c1
Test-Check 'old cookie rejected -> 401' ($r.Code -eq 401) "$($r.Code)"

$r = Invoke-Api POST '/api/login' '{"user":"admin","password":"admin"}'
Test-Check 'default password no longer works -> 401' ($r.Code -eq 401) "$($r.Code)"

$r = Invoke-Api POST '/api/login' ('{"user":"admin","password":"' + $NewPassword + '"}')
$c3 = $r.Cookie
Test-Check 'login with new password -> 200' ($r.Code -eq 200 -and $r.Body -match '"must_change_password":false' -and $c3) "$($r.Code) $($r.Body)"

$r = Invoke-Api GET '/api/setup/wifi/scan' -Cookie $c3
Test-Check 'scan -> JSON array' ($r.Code -eq 200 -and $r.Body.StartsWith('[')) "$($r.Code) $($r.Body)"
if ($r.Code -eq 200) { Write-Host "      networks: $(($r.Body | ConvertFrom-Json).ssid -join ', ')" }

$r = Invoke-Api POST '/api/setup/wifi/test' ('{"x":"' + ('a' * 600) + '"}') -Cookie $c3
Test-Check '600-byte body -> 413' ($r.Code -eq 413) "$($r.Code) $($r.Body)"

$r = Invoke-Api POST '/api/setup/wifi/test' '{"ssid":"x","password":"12345678"}' -Cookie $c3 -ContentType 'text/plain'
Test-Check 'text/plain -> 415' ($r.Code -eq 415) "$($r.Code) $($r.Body)"

$body = (@{ ssid = $Ssid; password = $WifiPass } | ConvertTo-Json -Compress)
$r = Invoke-Api POST '/api/setup/wifi/test' $body -Cookie $c3
Test-Check 'wifi test -> 202' ($r.Code -eq 202) "$($r.Code) $($r.Body)"

$r = Invoke-Api POST '/api/setup/wifi/test' $body -Cookie $c3
Test-Check 'second wifi test -> 409 test_running' ($r.Code -eq 409 -and $r.Body -match 'test_running') "$($r.Code) $($r.Body)"

# The setup AP may switch channel while the device joins the network: the PC can drop off
# for a few seconds, so connection errors (code 0) are retried.
$state = ''; $deadline = (Get-Date).AddSeconds(60)
while ((Get-Date) -lt $deadline) {
    $r = Invoke-Api GET '/api/setup/wifi/result' -Cookie $c3
    if ($r.Code -eq 200 -and $r.Body -match '"state":"(ok|failed)"') { $state = $Matches[1]; break }
    Start-Sleep -Seconds 2
}
Test-Check 'wifi test result ok' ($state -eq 'ok') "state=$state $($r.Body)"

if ($state -eq 'ok') {
    $r = Invoke-Api GET '/api/status' -Cookie $c3
    Test-Check 'status: step tls' ($r.Body -match '"step":"tls"') "$($r.Body)"
}

Remove-Item -Recurse -Force $tmp
Write-Host "`n$script:fails check(s) failed"
exit $script:fails
