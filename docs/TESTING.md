# Testing

## 1. Unit tests on the PC (no board)

```powershell
./tools/host_test.ps1
```

Builds `test/host` for ESP-IDF's `linux` target in WSL and runs the Unity tests of `gw_core` and
`gw_store`. Expected end of output: `34 Tests 0 Failures 0 Ignored` / `OK`.

| File | What it covers |
|---|---|
| `test_dev_id.c` | ID from MAC (4/6 digits), name joining, buffer limits |
| `test_password.c` | PBKDF2 against published test vectors, verify, password length rule |
| `test_auth.c` | default vs changed password, lockout timing, idle timeout, token checks, LRU eviction, cookies |
| `test_setup.c` | wizard order: no skipping, redo earlier steps, sanitizing stored values |
| `test_wifi_util.c` | scan dedupe/sort/cap, JSON escaping of odd SSIDs, Wi-Fi credential rules, setup password alphabet |
| `test_store.c` | NVS defaults, round trips, factory reset keeps the `fctry` partition |

## 2. Firmware build

```powershell
./tools/build.ps1 esp32 size
```

Expected: `Project build complete`, no warnings, `Total image size` well below 1.5 MB (0x180000).

## 3. On the board (setup mode)

Flash from the **ESP-IDF 6.1 PowerShell** (replace COM5):

```powershell
idf.py -B build/esp32 -p COM5 erase-flash flash monitor
```

| # | Check | Expected |
|---|---|---|
| H1 | Boot log | `gateway 0.1.0 booting`, `id=XXXX host=gatewayXXXX ssid=Gateway-Setup-XXXX` matching the board's MAC |
| H2 | Setup AP | phone/PC sees `Gateway-Setup-XXXX`; joining with `setup1234` works; log shows `setup client connected` |
| H3 | Per-device password | `idf.py menuconfig` → Gateway → Setup Wi-Fi → per device; reflash (no erase): `Setup Wi-Fi password: XXXXXXXXXXXX` printed, same after reboot and after a factory reset |
| H4 | Captive DNS | on a PC joined to the setup AP: `nslookup example.com 192.168.4.1` → `192.168.4.1` |
| H5 | mDNS | `ping gatewayXXXX.local` → `192.168.4.1` (Windows 10+, macOS, Linux with Avahi) |
| H6 | Captive page | a phone joining the setup AP opens the setup page by itself |
| H7 | Setup API | `./tools/smoke/setup_api.ps1 -Ssid "<your wifi>" -WifiPass "<password>"` → `0 check(s) failed` |
| H8 | Persistence | after H7, reset the board: `/api/status` still says `"step":"tls"`, login needs the new password |

H7 covers: login, wrong password, forced password change, old sessions ending, step order (409),
body limits (413/415), scan, test-connect, duplicate test (409), save + step advance.
