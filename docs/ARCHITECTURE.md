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
 ├─ state == PROVISIONED → NORMAL mode
 │   ├─ load Wi-Fi + TLS record (missing/invalid → back to setup step WIFI/TLS, reboot)
 │   ├─ Wi-Fi station only; reconnect with backoff 1, 2, 4 … 60 s; setup AP never reopens
 │   ├─ SNTP (display only), mDNS gateway1A2B.local, service _https._tcp:443
 │   ├─ HTTPS :443 → admin JSON API (mutual TLS: client cert from your CA required)
 │   └─ HTTP :80 → 301 to https://gateway1A2B.local/<same path>
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
PASSWORD ──change pw──▶ WIFI ──test ok──▶ TLS ──cert accepted──▶ FINISH ──finish──▶ DONE + PROVISIONED
```

- A request for a step later than the stored one gets `409 wrong_step`; earlier steps can be redone.
- Data is saved **before** the step advances, so a power cut can only make you repeat a step.
- Wi-Fi is saved only after the device actually got an IP address on it.
- Finish writes state PROVISIONED *before* step DONE: the other order plus a power cut would leave
  a setup-mode device at step DONE, where nothing is allowed.

## TLS identity (step TLS, and renewal in the admin API)

```
CSR path (default)                              Upload path
device: new P-256 key → NVS "tls_pend"           PC: make_device.ps1 → key + cert
device → you: CSR (GET /api/setup/tls/csr)
you: sign_csr.ps1 with your CA
you → device: {cert, ca}                         you → device: {key, cert, ca}
           └──────────── gw_tls_check: key parses, cert parses, ca is a CA, key matches cert,
                         SAN = <host>.local, EKU allows serverAuth, chain verifies to ca
                         → stored as ONE record "tls" (atomic), pending key cleared
```
The CSR path never moves the private key. Dates are not checked on the device (no clock).
Renewal makes a *new* pending key while the current identity keeps serving; the new one is used
after the next reboot.

## HTTPS and mutual TLS

`esp_https_server` gets the device key + certificate. In mutual mode it also gets the CA, which
makes it demand a client certificate in the TLS handshake and verify it against that CA. A browser
without one gets a handshake failure — no page, no login form to attack. After the handshake the
usual admin login (password + `Secure; HttpOnly; SameSite=Strict` cookie) still applies:
the certificate says *which machine*, the password says *which person*. The client certificate's CN
is logged and returned in `/api/status` (`client_cn`).

Memory on a classic ESP32 (no PSRAM): a TLS session needs a 16 KB receive buffer (browsers send
full-size records), so `max_open_sockets` is 4 with least-recently-used purging, the send buffer
is 4 KB, and dynamic buffers free both when a connection is idle.

## Security choices in setup mode

| Threat | Mitigation |
|---|---|
| Someone nearby reads setup traffic (plain HTTP) | setup AP is always WPA2; per-device password option |
| Default password left in place | wizard won't move on until it's changed; default stops working afterwards |
| Leaked password hash | PBKDF2-HMAC-SHA256, random 16-byte salt, 10000 iterations |
| Password guessing | 5 failures → 30 s lockout (global) |
| Stolen/forged session | 256-bit random token, HttpOnly + SameSite=Strict cookie, 15 min idle timeout, all sessions end on password change |
| Timing attacks on hashes/tokens | constant-time comparisons |
| Oversized / wrong requests | bodies > 512 bytes (8 KB for certificate uploads) → 413, non-JSON POST → 415 |
| Secrets in logs | passwords, PSKs and tokens are never logged (exception: the per-device setup password, by design) |
| Stolen cookie used for damage (normal mode) | password change and factory reset require re-entering the current password |
| Private key leaking | CSR path keeps it on the device; key buffers are wiped after use; no endpoint returns a key |

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
unused. Current firmware: ~1.0 MB (66 % of a slot) after plan 2.

## ESP-IDF v6.1.1 specifics met so far

- mbedTLS 4 is PSA-only: PBKDF2 uses `psa_key_derivation_*`, keys are made with `psa_generate_key`.
- `mbedtls_pk_check_pair` only works for EC keys in 4.1 (it compares a cached raw key that RSA
  contexts don't fill), so key/cert matching compares the public keys' DER encodings instead.
- cJSON and mDNS come from the component registry (`espressif/cjson`, `espressif/mdns`).
- CSR writing (`MBEDTLS_X509_CREATE_C`, `MBEDTLS_X509_CSR_WRITE_C`) is off by default; enabled here.
- Host-test workarounds (only in `test/host`): NVS compiled as C++17, two `-u` linker flags for
  mbedTLS's allocator, and a `mbedtls_ms_time()` stub. Firmware builds are unaffected.
