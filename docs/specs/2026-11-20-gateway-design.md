# ESP32 Gateway â€” Provisioning + HTTPS Admin Web Server: Design

Status: for approval Â· 2026-11-20 Â· supersedes `2026-11-20-gateway-design-draft.md`

Marks: **(user)** = decided by the user; **(call)** = designer's call, open to change.

## 1. Intent
A gateway device that a non-expert can set up from a browser: join its setup Wi-Fi, set a password,
put it on the home/office network, give it a TLS identity, and from then on manage it only over HTTPS
â€” optionally only from browsers holding a client certificate from the owner's CA.

Why: **learning now** (understand ESP-IDF provisioning, mDNS, TLS/mTLS, PKI, OTA) **and product-ready
later** (user). So the architecture and security model are product-grade from day one; costly or
irreversible hardening is built behind Kconfig switches and documented, but off by default.

## 2. Goals
1. Fresh device â†’ SoftAP + plain-HTTP setup UI with default credentials. (user)
2. mDNS hostname `gatewayXXXX.local`, unique per device. (user)
3. Setup wizard: forced password change â†’ Wi-Fi scan/select/connect â†’ TLS identity. (user)
4. Reboot â†’ station mode â†’ HTTPS-only admin UI; Kconfig choice **server-auth TLS** or **mutual TLS**. (user)
5. PKI guidance + scripts: own CA, device certs, client certs. (user)
6. Status LED + BOOT long-press factory reset. (user)
7. A/B OTA-ready flash layout for firmware **and** web UI. (user: OTA partitions; call: web A/B too)
8. Targets: esp32, esp32 + PSRAM, esp32s3, esp32s31, esp32p4 + esp32c6 co-processor. (user)
9. Full project + docs explaining the why. (user)

## 3. Non-goals (now)
- The gateway's real function and its data API (user: "admin pages only for now, API later") â€” but the
  UI talks only to a JSON API under `/api/`, so a later API reuses endpoints and auth. (call)
- Implementing OTA update download/apply â€” only the layout and version-pairing rules. (call)
- Multiple user accounts / roles: one admin account. (call)
- Enabling flash encryption, secure boot, NVS encryption by default (documented + Kconfig, off). (call)
- Certificate revocation lists, ACME/public CAs, cloud connectivity.

## 4. Anti-goals (would be a failure even if it works)
- Private keys, passwords, PSKs or session tokens in logs or in the repo.
- Any admin function reachable over plain HTTP once provisioned.
- Setup endpoints reachable once provisioned (except via factory reset).
- A wrong Wi-Fi password during setup locking the user out of the setup UI.
- Firmware and web UI from different versions running together.
- Target-specific `#ifdef`s spread through application code.

## 5. Constraints
- ESP-IDF (user). One pinned version for all targets: **v6.1** (call), because esp32s31 support starts
  there (Espressif marks it preview on master/v6.1). Crypto code (key gen, CSR) is written against the
  mbedTLS/PSA API that v6.1 ships â€” not v5.x examples.
- Classic ESP32 without PSRAM: ~1â€“2 concurrent TLS sessions (â‰ˆ35â€“45 KB heap each), while browsers open
  ~6 connections. So (call): small socket limit with LRU purge, keep-alive on, and the UI shipped as
  very few files (one JS + one CSS + index.html, icons inlined) so a page load needs few handshakes.
- No guaranteed internet / clock. (call) mbedTLS date checks stay off (`MBEDTLS_HAVE_TIME_DATE` not
  set), so certificates are accepted regardless of the clock in both TLS modes. SNTP is used when
  reachable only to *display* expiry dates. Enabling date checks is a documented product option once
  a time source is guaranteed.
- Flash: design for **4 MB minimum** (classic ESP32 modules); bigger targets may use larger defaults.
- ESP32-P4 has no radio: Wi-Fi via ESP32-C6 running Espressif's esp_hosted slave firmware, used through
  `esp_wifi_remote`; app code uses the normal `esp_wifi_*` API.
- Web UI build needs Node.js (accepted cost of option 2).

## 6. Approaches considered
| | Approach | Trade-off | Result |
|---|---|---|---|
| 1 | Vanilla HTML/JS embedded in firmware | simplest, one image; clumsy as UI grows | not chosen |
| 2 | **Svelte SPA in its own flash partition** | nicer UI dev, UI updatable separately; needs Node + second image + version pairing | **chosen (user)** |
| â€” | Espressif `wifi_provisioning` component | built for phone app/BLE; no password/cert steps | dropped |

## 7. Decided design

### 7.1 Device identity
- Device ID = last 2 bytes of the Wi-Fi STA MAC as 4 uppercase hex digits â†’ `gateway1A2B.local`,
  SoftAP SSID `Gateway-Setup-1A2B`. (call; user example was `gateway1234`) Collision risk on one LAN
  is ~1/65 536 per pair; Kconfig `GW_DEVICE_ID_LEN` (4 or 6 hex digits) for product use. (call)

### 7.1a Setup Wi-Fi protection (user: Kconfig, both modes)
Kconfig choice `GW_AP_AUTH`:
- `FIXED` (default for now): WPA2-PSK with `GW_AP_PASSWORD` (default `setup1234`), same on every device.
- `PER_DEVICE`: WPA2-PSK with a password unique to the device: 12 random characters (unambiguous
  alphabet) generated on first boot, or pre-written at manufacturing. (call) Stored in a separate
  **`factory` NVS partition** that factory reset does *not* erase, so the label stays valid. Printed on
  the serial console at boot in SETUP mode; a manufacturer pre-writes it with the NVS partition
  generator and prints the label. (call)
- Open (no password) is not offered: the setup traffic is plain HTTP. (call)

### 7.2 Boot modes
```
boot â†’ init NVS â†’ read state
  state != PROVISIONED â†’ SETUP mode
      Wi-Fi APSTA (AP for the user, STA for scan + test connect)
      captive portal: DNS server answers every name with 192.168.4.1, and the OS connectivity-check
        URLs redirect to the setup page, so phones pop the wizard up automatically (call)
      mDNS gatewayXXXX.local, service _http._tcp:80
      HTTP server :80  â†’ setup UI + /api/setup/*
  state == PROVISIONED â†’ NORMAL mode
      Wi-Fi STA, retry forever with backoff 1 s â†’ 60 s max; never reopens SoftAP on its own (user)
      SNTP time sync after IP
      mDNS gatewayXXXX.local, service _https._tcp:443
      HTTPS server :443 â†’ admin UI + /api/*; HTTP :80 only 301-redirects to https
```

### 7.3 Setup wizard (HTTP, SoftAP)
Steps enforced server-side by a persisted `setup_step`; requests for a later step are rejected. (call)
1. **Login** with default credentials (Kconfig `GW_DEFAULT_USER`/`GW_DEFAULT_PASSWORD`, default
   admin/admin). (user) Session cookie `HttpOnly; SameSite=Strict`.
2. **Change password** â€” mandatory; min 8 chars; stored as PBKDF2-HMAC-SHA256 hash + random salt.
   Default credentials stop working. (user/call)
3. **Wi-Fi** â€” scan list (SSID, signal, security; deduplicated, strongest first); user picks one +
   password; device test-connects in STA and saves only on success. Hidden SSID: manual entry.
   (user; test-before-save + hidden SSID are calls)
   One radio: joining a router on another channel moves the SoftAP to that channel and the phone may
   drop off briefly. So the test is **asynchronous** (call): `POST` starts it and returns at once; the
   UI shows "your phone may disconnect â€” rejoin Gateway-Setup-XXXX if needed" and polls a result
   endpoint until success/failure; the result is kept until read.
4. **TLS identity** â€” two paths (user: option C), CSR default:
   - **CSR (default):** device generates ECDSA P-256 key (mbedTLS), offers `gatewayXXXX.csr` download
     (CN and SAN `DNS:gatewayXXXX.local`). User signs it with `tools/pki` and uploads **device cert +
     CA cert**. Key never leaves the device.
   - **Upload:** user uploads **device key + device cert + CA cert** generated on the PC.
   - Device validates before saving: PEM parses; key matches cert; SAN contains its hostname; cert
     chains to the uploaded CA; EKU allows serverAuth. Expiry not checked (no clock in SETUP mode;
     checked and shown in NORMAL mode). (call)
   - The CA cert is stored in both TLS modes (mTLS needs it to verify clients; server-auth mode keeps it
     for display and future use). (call)
   - One root CA signs both device and client certs (call). With the product-step intermediate CA, the
     device cert upload is the **chain** (device + intermediate) and the trust anchor uploaded is the
     **root**; clients are verified against that root, so the same NVS keys work in both setups.
5. **Finish** â€” summary page â†’ mark PROVISIONED â†’ reboot after response.

### 7.4 Normal mode (HTTPS)
- `esp_https_server` with device cert + key; in **mutual** mode also the CA cert â†’ client cert
  required, browsers without one fail the TLS handshake and see nothing. (user)
- Any client cert signed by the CA is accepted; its CN is logged and shown in the UI. (call)
- Then the admin login (same password) â€” certificate identifies the machine, password the person.
- Change Wi-Fi in normal mode (call): the browser loses the device as soon as it switches, so there is
  no live test. New credentials are saved as *pending*, old ones kept as backup, device reboots and
  tries the pending network for 2 minutes; success â†’ pending becomes current; failure â†’ revert to the
  backup network and show "last Wi-Fi change failed" on the dashboard. LED shows the states as usual.
- Admin pages: dashboard (ID, hostname, IP, RSSI, uptime, free heap, firmware + UI versions, cert
  subject/expiry, TLS mode), change password, change Wi-Fi (see above), renew TLS
  identity (CSR or upload, same validation), reboot, factory reset (password re-entry). (call)
- Login rate limit: 5 failures â†’ 30 s lockout; session idle timeout 15 min (Kconfig). (call)

### 7.5 Web UI
- Svelte + Vite single-page app, hash routing, no SvelteKit. (call) Lives in `web/`.
- Same app bundle for both modes; it asks `/api/status` which mode the device is in and shows the
  wizard or the admin pages. (call)
- Build output gzipped, packed into a LittleFS image, flashed to the active `www` partition; served
  with `Content-Encoding: gzip` and long cache headers on hashed asset names. (call)
- Budget: whole gzipped UI â‰¤ 150 KB. (call)

### 7.6 Flash layout (4 MB baseline) (user: 2 OTA partitions; call: web A/B + sizes)
| Name | Type | Size | Purpose |
|---|---|---|---|
| nvs | data/nvs | 24 KB | state, password hash, Wi-Fi creds, device key, certs |
| nvs_keys | data/nvs_keys | 4 KB | reserved for NVS encryption (product) |
| factory | data/nvs | 12 KB | per-device data that survives factory reset (setup AP password) |
| otadata | data/ota | 8 KB | active slot |
| phy_init | data/phy | 4 KB | |
| ota_0 / ota_1 | app | 1.5 MB each | firmware A/B |
| www_0 / www_1 | data/littlefs | 256 KB each | web UI A/B, paired with ota_0 / ota_1 |
| coredump | data/coredump | 64 KB | crash analysis |
Pairing rule: firmware in `ota_N` mounts `www_N`; the firmware embeds the UI version it expects and
shows a "UI version mismatch" warning page if they differ. (call) Larger-flash targets may enlarge slots.

### 7.7 Storage (NVS)
Keys under namespace `gw`: `state`, `setup_step`, `pw_hash`, `pw_salt`, `wifi_ssid`, `wifi_pass`,
`dev_key` (PEM), `dev_cert`, `ca_cert`. Factory reset erases the whole `nvs` partition. (call)
NVS encryption + flash encryption + secure boot: Kconfig/sdkconfig profile `product` documented, off
by default â€” flash encryption is one-way on real chips. (call)

### 7.8 Status LED and BOOT button (user)
| State | Pattern | RGB colour |
|---|---|---|
| SETUP (SoftAP up) | slow blink 1 s on/off | blue |
| Connecting | fast blink 5 Hz | yellow |
| Connected | solid | green |
| Failed, retrying | double blink + pause | red |
| Reset pending (button held) | rapid flicker | white |
- Kconfig: LED type none / GPIO / addressable RGB (`led_strip`), GPIO number, active level. (call)
- BOOT held 5 s â†’ erase NVS â†’ reboot into SETUP; release earlier cancels. Short press: no action. (user/call)

### 7.9 TLS mode (user)
Kconfig choice `GW_TLS_MODE`: `SERVER_AUTH` | `MUTUAL` (default MUTUAL).

### 7.10 PKI (scripts in `tools/pki`, guide in `docs/PKI.md`)
- Own root CA (ECDSA P-256, 10 y), key kept offline on the PC, never on a device. Intermediate CA
  documented as the product step. (call)
- Device certs: from device CSR (default) or PC-generated key; SAN `DNS:gatewayXXXX.local`, EKU
  serverAuth, 2 y. Client certs: EKU clientAuth, CN = person/PC, exported as password-protected `.p12`.
- Scripts in PowerShell and bash, OpenSSL-based: `make_ca`, `sign_csr`, `make_device` (upload path),
  `make_client`. Output to `tools/pki/out/` (git-ignored).
- Docs: how to trust the CA in Windows/macOS/browsers, import the `.p12`, why ECDSA, why SAN.

### 7.11 Targets and build
- One `sdkconfig.defaults` + per-profile files: `esp32`, `esp32_psram`, `esp32s3`, `esp32s31`, `esp32p4`.
- P4 profile pulls `esp_wifi_remote` + `esp_hosted`; docs explain flashing esp_hosted slave on the C6.
  Risk (call): SoftAP+STA, scan and mDNS through esp_hosted depend on the slave firmware version â€” the
  plan's first P4 task is a spike that proves setup mode works there before the rest is built on it.
- `tools/build_all.ps1` builds every profile + the web UI and LittleFS image.

### 7.12 Testing
- Host unit tests (ESP-IDF `linux` target, Unity) for pure logic: password hashing/verify, setup step
  machine, cert/key/CSR validation (fixtures made by `tools/pki`), device-ID/hostname, JSON building.
- Web UI: component/unit tests with Vitest for the wizard flow logic. (call)
- Hardware checklist in `docs/TESTING.md` for Wi-Fi, mDNS, TLS/mTLS via curl/openssl/browser, LED, reset.

## 8. Left to the builder
- Pinned ESP-IDF version; component versions.
- Exact module/component split, task layout, stack sizes, mbedTLS buffer tuning per profile.
- Exact partition offsets and per-target flash sizes within Â§7.6 rules.
- LED/BOOT default GPIOs per dev board.
- UI look and wording (clean, minimal, mobile-friendly).
- How the UI version is stamped and compared.
