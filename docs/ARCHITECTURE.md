# Architecture (after plan 1)

## Components and why they are split this way

```
            main/main.c  (boot: identity → storage → mode)
                 │
     ┌───────────┼──────────────┬───────────────┐
     ▼           ▼              ▼               ▼
 gw_http     gw_net         gw_store         gw_core
 HTTP API    Wi-Fi, DNS,    NVS keys         pure logic
 handlers    mDNS           (state, creds)   (no hardware)
     │           │              │               ▲
     └───────────┴──────────────┴───────────────┘  all use gw_core
```

- **gw_core** has no Wi-Fi, HTTP or GPIO includes. It decides things: is this password allowed,
  does this login succeed, may the wizard go to this step, which scan results to show. Because it
  only needs C, mbedTLS and cJSON, it also builds for ESP-IDF's `linux` target and is unit-tested
  on the PC in seconds — no board, no flashing.
- **gw_store** wraps NVS (ESP-IDF's key-value store in flash). NVS is emulated on the `linux` target
  too, so storage rules (defaults, factory reset, max lengths) are unit-tested as well.
- **gw_net** and **gw_http** are thin glue: they translate radio events and HTTP requests into calls
  to gw_core/gw_store. They are checked on hardware (smoke test) rather than with fakes.
- The rule that makes this work: logic goes into gw_core; glue stays small enough to read at once.

## Boot flow

```
app_main
 ├─ device id from Wi-Fi MAC → "1A2B", hostname "gateway1A2B", SSID "Gateway-Setup-1A2B"
 ├─ gw_store_init  (NVS "nvs" + "fctry" partitions)
 ├─ state == PROVISIONED → (plan 2: station mode + HTTPS)   — today: log and stop
 └─ SETUP mode
     ├─ setup password: Kconfig, or per-device from "fctry" (generated on first boot)
     ├─ Wi-Fi APSTA: WPA2 access point 192.168.4.1 + idle station for scan/test
     ├─ captive DNS: every name → 192.168.4.1 (phones pop up the setup page)
     ├─ mDNS: gateway1A2B.local, service _http._tcp:80
     └─ HTTP server :80 → JSON API + placeholder page; unknown URLs → 302 to http://192.168.4.1/
```

## Setup wizard state machine

`gw_setup` (pure logic) + the `setup_step` NVS key:

```
PASSWORD ──change pw──▶ WIFI ──test ok──▶ TLS ──(plan 2)──▶ FINISH ──▶ DONE
```

- A request for a step later than the stored one gets `409 wrong_step`; earlier steps can be redone.
- Data is saved **before** the step advances, so a power cut can only make you repeat a step.
- Wi-Fi is saved only after the device actually got an IP address on it.

## Security choices in setup mode

| Threat | Mitigation |
|---|---|
| Someone nearby reads setup traffic (plain HTTP) | setup AP is always WPA2; per-device password option |
| Default password left in place | wizard won't move on until it's changed; default stops working afterwards |
| Leaked password hash | PBKDF2-HMAC-SHA256, random 16-byte salt, 10000 iterations |
| Password guessing | 5 failures → 30 s lockout (global) |
| Stolen/forged session | 256-bit random token, HttpOnly + SameSite=Strict cookie, 15 min idle timeout, all sessions end on password change |
| Timing attacks on hashes/tokens | constant-time comparisons |
| Oversized / wrong requests | bodies > 512 bytes → 413, non-JSON POST → 415 |
| Secrets in logs | passwords, PSKs and tokens are never logged (exception: the per-device setup password, by design) |

## Flash layout (4 MB, `partitions.csv`)

| Partition | Size | What and why |
|---|---|---|
| `nvs` | 48 KB | state, setup step, password hash, Wi-Fi credentials, TLS key + certificates (up to ~6 KB, and NVS needs a spare page to rewrite records). Erased by factory reset. |
| `otadata` | 8 KB | which app slot boots; written by OTA updates |
| `phy_init` | 4 KB | radio calibration data |
| `nvs_keys` | 4 KB | reserved for NVS encryption keys (product hardening, off for now) |
| `fctry` | 12 KB | per-device data that survives factory reset: the setup Wi-Fi password |
| `ota_0` / `ota_1` | 1.5 MB each | two firmware slots: an update is written to the idle slot, so a failed update never bricks the device |
| `www_0` / `www_1` | 256 KB each | web UI (plan 3), paired with `ota_0` / `ota_1` so firmware and UI versions always match |
| `coredump` | 64 KB | crash dump for post-mortem analysis (`idf.py coredump-info`) |

The app partitions start at 0x20000 because app images must be 64 KB aligned; the gap before it is
unused. Current firmware: ~0.9 MB, so each slot has room to grow.

## ESP-IDF v6.1.1 specifics met in plan 1

- mbedTLS 4 is PSA-only: PBKDF2 uses `psa_key_derivation_*`.
- cJSON and mDNS come from the component registry (`espressif/cjson`, `espressif/mdns`).
- Host-test workarounds (only in `test/host`): NVS compiled as C++17, and two `-u` linker flags for
  mbedTLS's allocator. Firmware builds are unaffected.
