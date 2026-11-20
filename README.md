# ESP32 Gateway

ESP-IDF firmware for a "gateway" device that a non-expert sets up from a browser:

1. A fresh device opens its own **setup Wi-Fi** (`Gateway-Setup-1A2B`, WPA2) and serves a setup
   wizard over plain HTTP at `http://gateway1A2B.local` / `http://192.168.4.1`.
2. The wizard forces a **new admin password**, lets you **pick and test your Wi-Fi**, and (plan 2)
   gives the device its **TLS identity**.
3. After a reboot it joins your network and serves the admin pages over **HTTPS only**, optionally
   only to browsers holding a client certificate from your own CA (mutual TLS).

Learning project that is meant to become product-ready: see `docs/specs/2026-11-20-gateway-design.md`
for the full design and `docs/plans/2026-11-20-roadmap.md` for the build order.

**Status:** plans 1–2 done — full setup wizard (password, Wi-Fi, TLS identity via device CSR or
upload, finish) and normal mode (station Wi-Fi, HTTPS admin API with server-auth or mutual TLS, PKI
scripts). Everything is a JSON API for now; the web UI comes in plan 3.

## Toolchain (one-time)

| Where | What | Used for |
|---|---|---|
| WSL Ubuntu-22.04 | ESP-IDF **v6.1.1** via Espressif Installation Manager (EIM) → `~/.espressif/v6.1.1` | building firmware, host tests |
| WSL | `sudo apt-get install -y flex bison gperf ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0 libbsd-dev` | host (`linux` target) builds |
| Windows | ESP-IDF v6.1.1 via EIM, **with Python ≥ 3.10 (64-bit, python.org)** | flashing + serial monitor |
| Windows | Git for Windows (bash + OpenSSL) | certificate scripts in `tools/pki` |

Why builds run in WSL: ESP-IDF's `linux` target (used for unit tests on the PC) needs a POSIX host,
and keeping firmware builds there too means one toolchain for both.

Windows install gotcha: if another Python comes first on `PATH` (e.g. Ruby's MSYS2 Python), EIM
builds a broken virtual environment. Run EIM from a PowerShell where that folder is removed:

```powershell
$env:PATH = ($env:PATH -split ';' | Where-Object { $_ -notlike 'C:\Ruby30\msys32*' }) -join ';'; & "$env:USERPROFILE\.espressif\eim_gui\eim-gui-windows-x64.exe"
```

## Everyday commands (PowerShell, repo root)

If PowerShell refuses to run the scripts ("running scripts is disabled"), prefix them with
`powershell -ExecutionPolicy Bypass -File`.

| Task | Command |
|---|---|
| Unit tests on the PC | `./tools/host_test.ps1` (WSL, all 55 tests; exit code = number of failures) |
| Unit tests without WSL | `./tools/host_test_win.ps1 -Gcc <path\to\gcc.exe>` (native MinGW gcc; 45 tests — everything except NVS storage) |
| Build firmware | `./tools/build.ps1 esp32` (`./tools/build.ps1 esp32 size` adds a size report) |
| Flash + serial log | `./tools/flash.ps1 -Port COM17 -Monitor` (Ctrl+] quits the monitor) |
| Wipe the device first | `./tools/flash.ps1 -Port COM17 -Erase -Monitor` (erases settings, keys and the per-device password) |
| Make your CA (once) | `./tools/pki/make_ca.ps1` → `tools/pki/out/ca.key` + `ca.crt` (see [docs/PKI.md](docs/PKI.md)) |
| Browser certificate (mutual TLS) | `./tools/pki/make_client.ps1 laptop1` → import `tools/pki/out/laptop1.p12` |
| Smoke-test setup mode | join the setup Wi-Fi, then `./tools/smoke/setup_api.ps1 -Ssid "<your wifi>" -WifiPass "<password>" -Pki tools/pki/out` (ends with the device rebooting into normal mode) |
| Smoke-test normal mode | on the same LAN as the gateway: `python tools/smoke/normal_api.py --host gatewayXXXX --ca tools/pki/out/ca.crt --cert tools/pki/out/laptop1.crt --key tools/pki/out/laptop1.key --password <admin password>` (add `--renew --pki tools/pki/out` to test certificate renewal) |

`flash.ps1` uses esptool directly: the build folder is configured in WSL, so Windows `idf.py flash`
would try to reconfigure it.

Build logs: `build/<profile>.log`, `test/host/build-linux.log`.

After changing `sdkconfig.defaults*`, delete `build/<profile>/sdkconfig`: an existing sdkconfig keeps
the values it already has, and defaults only fill in options that are missing.

## Configuration (`idf.py menuconfig` → Gateway)

| Option | Default | Why it exists |
|---|---|---|
| Hostname prefix | `gateway` | mDNS name is `<prefix><id>.local` |
| Device ID length | 4 hex digits | from the MAC; 6 digits make two devices on one LAN sharing an ID very unlikely |
| Setup network name prefix | `Gateway-Setup-` | SSID is `<prefix><id>` |
| Setup network password | fixed `setup1234` | setup traffic is plain HTTP, so only WPA2 protects it; "per device" makes a random 12-char password kept in the `fctry` partition (survives factory reset, printed on serial at boot) |
| Setup network channel | 1 | |
| Admin user / default password | `admin` / `admin` | default only works until the mandatory password change |
| Max sessions / idle timeout | 4 / 15 min | |
| PBKDF2 iterations | 10000 | slower brute-forcing of a leaked hash vs slower login |
| HTTPS client authentication | mutual TLS | mutual: browsers need a certificate from your CA before anything is served; server-auth: normal HTTPS, password only |
| SNTP time server | `pool.ntp.org` | only to display dates (certificate expiry); works without internet |

## Repository map

```
main/                 boot logic (main.c: setup mode vs normal mode) and Kconfig options
components/gw_core    pure logic, unit-tested on the PC: device id, passwords, sessions, setup steps,
                      Wi-Fi helpers, certificate checks, device key + CSR
components/gw_store   NVS storage (also unit-tested on the PC)
components/gw_net     Wi-Fi (setup AP + station with backoff), captive DNS, mDNS, SNTP
components/gw_http    setup server (HTTP), admin server (HTTPS), HTTP→HTTPS redirect, JSON APIs
test/host             Unity test app for the linux target (TLS fixtures generated on each run)
tools/                build/test/flash wrappers, PKI scripts (tools/pki), smoke tests (tools/smoke)
docs/                 design, plans, architecture, PKI, testing
```

More: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) · [docs/PKI.md](docs/PKI.md) · [docs/TESTING.md](docs/TESTING.md)
