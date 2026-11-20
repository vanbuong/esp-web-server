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

**Status:** plan 1 done — setup mode (setup Wi-Fi, captive portal, mDNS, login, password change,
Wi-Fi scan/test/save) through a JSON API. No web UI yet (plan 3); HTTPS comes in plan 2.

## Toolchain (one-time)

| Where | What | Used for |
|---|---|---|
| WSL Ubuntu-22.04 | ESP-IDF **v6.1.1** via Espressif Installation Manager (EIM) → `~/.espressif/v6.1.1` | building firmware, host tests |
| WSL | `sudo apt-get install -y flex bison gperf ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0 libbsd-dev` | host (`linux` target) builds |
| Windows | ESP-IDF v6.1.1 via EIM, **with Python ≥ 3.10 (64-bit, python.org)** | flashing + serial monitor |

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
| Unit tests on the PC | `./tools/host_test.ps1` (exit code = number of failures) |
| Build firmware | `./tools/build.ps1 esp32` (`./tools/build.ps1 esp32 size` adds a size report) |
| Flash + serial log | in the **ESP-IDF 6.1 PowerShell**: `idf.py -B build/esp32 -p COM5 flash monitor` (Ctrl+] quits) |
| Wipe the device first | `idf.py -B build/esp32 -p COM5 erase-flash flash monitor` |
| Smoke-test the setup API | join the setup Wi-Fi, then `./tools/smoke/setup_api.ps1 -Ssid "<your wifi>" -WifiPass "<password>"` |

Build logs: `build/<profile>.log`, `test/host/build-linux.log`.

## Configuration (`idf.py menuconfig` → Gateway)

| Option | Default | Why it exists |
|---|---|---|
| Hostname prefix | `gateway` | mDNS name is `<prefix><id>.local` |
| Device ID length | 4 hex digits | from the MAC; 6 digits make two devices on one LAN sharing an ID very unlikely |
| Setup network name prefix | `Gateway-Setup-` | SSID is `<prefix><id>` |
| Setup network password | fixed `setup1234` | setup traffic is plain HTTP, so only WPA2 protects it; "per device" makes a random 12-char password kept in the `factory` partition (survives factory reset, printed on serial at boot) |
| Setup network channel | 1 | |
| Admin user / default password | `admin` / `admin` | default only works until the mandatory password change |
| Max sessions / idle timeout | 4 / 15 min | |
| PBKDF2 iterations | 10000 | slower brute-forcing of a leaked hash vs slower login |

## Repository map

```
main/                 boot logic (main.c) and Kconfig options
components/gw_core    pure logic, unit-tested on the PC: device id, passwords, sessions, setup steps, Wi-Fi helpers
components/gw_store   NVS storage (also unit-tested on the PC)
components/gw_net     Wi-Fi (SoftAP + station), captive DNS, mDNS
components/gw_http    setup-mode HTTP server and JSON API
test/host             Unity test app for the linux target
tools/                build/test wrappers (PowerShell → WSL), smoke test
docs/                 design, plans, architecture, testing
```

More: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) · [docs/TESTING.md](docs/TESTING.md)
