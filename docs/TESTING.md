# Testing

## 1. Unit tests on the PC (no board)

```powershell
./tools/host_test.ps1
```

Builds `test/host` for ESP-IDF's `linux` target in WSL and runs the Unity tests of `gw_core` and
`gw_store`. First it generates throwaway certificates into `test/host/fixtures/` (git-ignored) with
`tools/pki/make_test_fixtures.sh`. Expected end of output: `54 Tests 0 Failures 0 Ignored` / `OK`.

| File | What it covers |
|---|---|
| `test_dev_id.c` | ID from MAC (4/6 digits), name joining, buffer limits |
| `test_password.c` | PBKDF2 against published test vectors, verify, password length rule |
| `test_auth.c` | default vs changed password, lockout timing, idle timeout, token checks, LRU eviction, cookies |
| `test_setup.c` | wizard order: no skipping, redo earlier steps, sanitizing stored values |
| `test_wifi_util.c` | scan dedupe/sort/cap, JSON escaping of odd SSIDs, Wi-Fi credential rules, setup password alphabet |
| `test_store.c` | NVS defaults, round trips, factory reset keeps `fctry`; TLS record (6 KB), pending key |
| `test_tls_check.c` | EC/RSA/PKCS#8 keys, certificate chains, CRLF + whitespace, and every rejection code |
| `test_tls_csr.c` | device key is P-256 and random, CSR subject/SAN/public key, bad key |

## 2. Firmware build

```powershell
./tools/build.ps1 esp32 size
```

Expected: `Project build complete`, no warnings, `Total image size` well below 1.5 MB (0x180000).

## 3. On the board — setup mode

```powershell
./tools/flash.ps1 -Port COM17 -Erase -Monitor
```

| # | Check | Expected |
|---|---|---|
| H1 | Boot log | `gateway 0.1.0 booting`, `id=XXXX host=gatewayXXXX ssid=Gateway-Setup-XXXX` matching the board's MAC |
| H2 | Setup AP | phone/PC sees `Gateway-Setup-XXXX`; joining with `setup1234` works; log shows `setup client connected` |
| H3 | Per-device password | menuconfig → Gateway → Setup Wi-Fi → per device; reflash (no erase): `Setup Wi-Fi password: XXXXXXXXXXXX` printed, same after reboot and after erasing only `nvs` |
| H4 | Captive DNS | on a PC joined to the setup AP: `nslookup example.com 192.168.4.1` → `192.168.4.1` |
| H5 | mDNS | `ping gatewayXXXX.local` → `192.168.4.1` (Windows 10+, macOS, Linux with Avahi) |
| H6 | Captive page | a phone joining the setup AP opens the setup page by itself |
| H7 | Setup API | `./tools/smoke/setup_api.ps1 -Ssid "<your wifi>" -WifiPass "<password>"` → `0 check(s) failed` |
| H8 | Persistence | after H7, reset the board: `/api/status` still says `"step":"tls"`, login needs the new password |

H7 covers: login, wrong password, forced password change, old sessions ending, step order (409),
body limits (413/415), scan, test-connect, duplicate test (409), save + step advance.

## 4. On the board — TLS step and normal mode

Needs your CA (`./tools/pki/make_ca.ps1`), a client certificate (`./tools/pki/make_client.ps1 laptop1`),
and — for N3 onward — this PC on the **same network as the gateway**. A guest Wi-Fi usually isolates
clients from each other, so the PC could not reach the gateway there.

| # | Check | Expected |
|---|---|---|
| N1 | Setup TLS step + finish | fresh board: `./tools/smoke/setup_api.ps1 -Ssid .. -WifiPass .. -Pki tools/pki/out` → `0 check(s) failed`; serial: `TLS identity stored (CN=gatewayXXXX …)`, `setup finished — rebooting` |
| N2 | Normal-mode boot | serial: `station mode, joining …`, `connected, IP …`, `HTTPS on https://gatewayXXXX.local/ (mutual TLS)`, `time synced` (if the network has internet); the setup AP is gone |
| N3 | HTTPS API | `python tools/smoke/normal_api.py --host gatewayXXXX --ca tools/pki/out/ca.crt --cert tools/pki/out/laptop1.crt --key tools/pki/out/laptop1.key --password <admin pw>` → `0 check(s) failed` (covers: no-cert handshake fails, client CN, login, `Secure` cookie, `http://` → 301, 20 failed handshakes without memory loss) |
| N4 | Browser, mutual TLS | `ca.crt` trusted + `laptop1.p12` imported: `https://gatewayXXXX.local/api/status` shows a padlock and JSON; without the `.p12`: connection error |
| N5 | Memory | two browser tabs open: `heap_free` in `/api/status` (logged in) stays ≥ 30 KB |
| N6 | Reconnect | switch the router off or move away: serial `disconnected …, retry in N s` with N doubling up to 60; back in range: reconnects |
| N7 | Renewal | `normal_api.py … --renew --pki tools/pki/out` → `0 check(s) failed` (password change round trip, new CSR while the old cert keeps serving, mismatched cert rejected, new cert served after reboot) |
| N8 | Factory reset | `normal_api.py … --factory-reset` → `0 check(s) failed`; the board comes back in setup mode with the same per-device setup password |
| N9 | Server-auth build | menuconfig → Gateway → Normal mode → server authentication only: browser shows the padlock without asking for a certificate; `normal_api.py` without `--cert/--key` passes |
