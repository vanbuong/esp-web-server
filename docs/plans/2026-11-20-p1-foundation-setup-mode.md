# Plan 1 — Foundation + Setup Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A fresh esp32 boots into setup mode — WPA2 SoftAP, captive DNS, mDNS `gatewayXXXX.local` — and a client can log in, change the password and scan/test/save Wi-Fi through a JSON API.

**Architecture:** Pure logic lives in `components/gw_core` (no IDF hardware includes) and is unit-tested on the PC with ESP-IDF's `linux` target in WSL. Thin IDF glue components call it: `gw_store` (NVS), `gw_net` (Wi-Fi, DNS, mDNS), `gw_http` (setup HTTP server + API). `main/main.c` wires them by boot mode. No web UI yet: `/` serves a placeholder page; the API is exercised with curl.

**Tech Stack:** ESP-IDF v6.1 (C17), mbedTLS as shipped in v6.1, cJSON, `espressif/mdns`, Unity, WSL Ubuntu-22.04 for host tests, PowerShell + curl for smoke tests.

**Spec:** `docs/specs/2026-11-20-gateway-design.md` (sections 5, 7.1, 7.1a, 7.2 setup mode, 7.3 steps 1–3, 7.6, 7.7). Roadmap: `docs/plans/2026-11-20-roadmap.md`.

## Global Constraints
- ESP-IDF pinned to **v6.1** for build and host tests. Crypto calls use the API v6.1's mbedTLS provides.
- Target in this plan: **esp32** only (profile `sdkconfig.defaults` + `sdkconfig.defaults.esp32`), 4 MB flash.
- Public symbols prefixed `gw_`; one component = one public header in `include/`; everything else `static`.
- Every fallible call checked (`ESP_RETURN_ON_ERROR` / `ESP_GOTO_ON_ERROR`); no `ESP_ERROR_CHECK` outside `app_main` init.
- Never log admin passwords, Wi-Fi PSKs, session tokens. **Exception from spec 7.1a:** the per-device setup AP password is printed on the serial console at boot in SETUP mode.
- Device ID = last 2 bytes of the STA MAC as 4 uppercase hex digits (Kconfig `GW_DEVICE_ID_LEN` 4 or 6) → hostname `gateway1A2B`, SSID `Gateway-Setup-1A2B`.
- Default login `admin` / `admin` (Kconfig); new password 8–64 bytes; PBKDF2-HMAC-SHA256, 16-byte random salt, 32-byte hash, Kconfig `GW_PBKDF2_ITERATIONS` default 10000.
- Session cookie `gw_session=<64 hex>; Path=/; HttpOnly; SameSite=Strict`; idle timeout 15 min; max 4 sessions; login limiter 5 failures → 30 s lockout.
- Setup AP: WPA2-PSK, never open; `GW_AP_AUTH` FIXED (`setup1234`) | PER_DEVICE (12 chars from `ABCDEFGHJKLMNPQRSTUVWXYZ23456789`, kept in the `factory` NVS partition, survives factory reset).
- Request bodies ≤ 512 bytes (413), `Content-Type: application/json` required on POST with body (415).
- Build/flash/monitor on Windows (EIM-installed IDF). Host tests in WSL (the `linux` target does not build on native Windows).

## Review Focus
1. SSIDs with `"`, `\`, emoji, invalid UTF-8 bytes or exactly 32 bytes → scan JSON stays valid and nothing overflows (test in Task 5).
2. Credential bounds: admin password 7/8/64/65 bytes; Wi-Fi PSK empty (open), 7, 8, 63, 64-hex, 64-non-hex → accept/reject exactly per rule (Tasks 2, 5).
3. Session tokens: expired, unknown, wrong length, empty cookie, cookie among other cookies → rejected or parsed correctly (Task 3).
4. Fresh or half-written NVS (power loss between saving Wi-Fi and advancing the step) → device comes up in a consistent step, never skips one (Task 7).
5. A second `wifi/test` while one runs, oversized body, wrong content type, step skipped via URL → 409/413/415/409, device keeps working (Task 10 smoke script).

---

### Task 0: Toolchains (user steps — agent verifies)

**Files:** none.

- [ ] **Step 1 (user):** Open the Espressif Installation Manager (`C:\Users\<you>\.espressif\eim_gui\eim-gui-windows-x64.exe`), install **ESP-IDF v6.1** with target esp32. Open the "ESP-IDF 6.1 PowerShell" it creates.
- [ ] **Step 2 (user, WSL):** In Ubuntu-22.04:
  ```bash
  sudo apt-get update && sudo apt-get install -y git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0 libbsd-dev
  mkdir -p ~/esp && cd ~/esp && git clone -b v6.1 --recursive https://github.com/espressif/esp-idf.git
  cd esp-idf && ./install.sh esp32
  ```
  (If tag `v6.1` doesn't exist yet, use branch `release/v6.1`.)
- [ ] **Step 3 (agent):** Verify: Windows `idf.py --version` → `ESP-IDF v6.1…`; WSL `wsl -d Ubuntu-22.04 -- bash -lc ". ~/esp/esp-idf/export.sh >/dev/null && idf.py --version"` → same. If either fails, stop and report.

### Task 1: Project skeleton, partitions, host-test app, device ID

**Files:**
- Create: `CMakeLists.txt` (project `gateway`, `PROJECT_VER "0.1.0"`), `partitions.csv`, `sdkconfig.defaults`, `sdkconfig.defaults.esp32`
- Create: `main/CMakeLists.txt`, `main/main.c` (logs `gateway <ver> booting`), `main/Kconfig.projbuild`, `main/idf_component.yml`
- Create: `components/gw_core/CMakeLists.txt`, `components/gw_core/include/gw_dev_id.h`, `components/gw_core/gw_dev_id.c`
- Create: `test/host/CMakeLists.txt`, `test/host/main/CMakeLists.txt`, `test/host/main/test_main.c`, `test/host/main/test_dev_id.c`, `test/host/sdkconfig.defaults`, `test/host/partitions.csv`
- Create: `tools/host_test.ps1`

**Interfaces:**
- Produces: `esp_err_t gw_dev_id_format(const uint8_t mac[6], int digits, char *out, size_t out_len);` (digits 4 or 6, uppercase hex of the last digits/2 MAC bytes; `ESP_ERR_INVALID_ARG` for other digits, `ESP_ERR_INVALID_SIZE` if `out_len` too small)
- Produces: `esp_err_t gw_name_join(const char *prefix, const char *id, char *out, size_t out_len);` (`"gateway"+"1A2B"` → `"gateway1A2B"`; `ESP_ERR_INVALID_SIZE` on truncation)
- Produces: `tools/host_test.ps1` — runs, in WSL: export IDF, `idf.py -C test/host -B test/host/build-linux set-target linux build`, then `test/host/build-linux/host_test.elf`; exits with the test binary's exit code.
- Produces Kconfig symbols: `GW_HOSTNAME_PREFIX` ("gateway"), `GW_DEVICE_ID_LEN` (choice `GW_DEVICE_ID_LEN_4` default / `GW_DEVICE_ID_LEN_6` → hidden int `GW_DEVICE_ID_LEN` 4 or 6), `GW_AP_SSID_PREFIX` ("Gateway-Setup-"), `GW_AP_AUTH_FIXED`/`GW_AP_AUTH_PER_DEVICE` (choice), `GW_AP_PASSWORD` ("setup1234"), `GW_AP_CHANNEL` (1), `GW_DEFAULT_USER` ("admin"), `GW_DEFAULT_PASSWORD` ("admin"), `GW_MAX_SESSIONS` (4), `GW_SESSION_TIMEOUT_MIN` (15), `GW_PBKDF2_ITERATIONS` (10000). Each with a help text explaining *why*.

`partitions.csv` (4 MB) — exact:
```
# Name,    Type, SubType,  Offset,   Size,     Flags
nvs,       data, nvs,      0x9000,   0x6000,
otadata,   data, ota,      0xF000,   0x2000,
phy_init,  data, phy,      0x11000,  0x1000,
nvs_keys,  data, nvs_keys, 0x12000,  0x1000,   encrypted
factory,   data, nvs,      0x13000,  0x3000,
ota_0,     app,  ota_0,    0x20000,  0x180000,
ota_1,     app,  ota_1,    0x1A0000, 0x180000,
www_0,     data, littlefs, 0x320000, 0x40000,
www_1,     data, littlefs, 0x360000, 0x40000,
coredump,  data, coredump, 0x3A0000, 0x10000,
```
`sdkconfig.defaults`: custom partition table `partitions.csv`, flash size 4MB, `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`, HTTP server max URI handlers 24, `CONFIG_MBEDTLS_HAVE_TIME_DATE` not set.

- [ ] **Step 1: Write failing tests** in `test/host/main/test_dev_id.c`:
  - `test_dev_id_4_digits`: mac `{0x24,0x6F,0x28,0xAB,0x1A,0x2B}`, digits 4 → `"1A2B"`, `ESP_OK`.
  - `test_dev_id_6_digits`: same mac, 6 → `"AB1A2B"`.
  - `test_dev_id_rejects_5_digits` → `ESP_ERR_INVALID_ARG`.
  - `test_dev_id_buffer_too_small`: out_len 4, digits 4 → `ESP_ERR_INVALID_SIZE`.
  - `test_name_join`: `("gateway","1A2B")` → `"gateway1A2B"`; `("Gateway-Setup-","1A2B")` with out_len 10 → `ESP_ERR_INVALID_SIZE`.
- [ ] **Step 2: Run** `./tools/host_test.ps1` → FAIL (undefined `gw_dev_id_format`).
- [ ] **Step 3: Implement** `gw_dev_id_format` and `gw_name_join` in `components/gw_core/gw_dev_id.c` (`snprintf`, return codes as in Interfaces).
- [ ] **Step 4: Run** `./tools/host_test.ps1` → `5 Tests 0 Failures 0 Ignored`.
- [ ] **Step 5: Firmware build** `idf.py -B build/esp32 -D SDKCONFIG=build/esp32/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32" set-target esp32 build` → exit 0; `idf.py -B build/esp32 size` shows app < 0x180000.
- [ ] **Step 6: Commit** `feat: project skeleton, partition table, host tests, device id`.

### Task 2: Password hashing and policy (`gw_core/gw_password`)

**Files:** Create `components/gw_core/include/gw_password.h`, `components/gw_core/gw_password.c`, `test/host/main/test_password.c`.

**Interfaces:**
- Produces: `#define GW_PW_SALT_LEN 16`, `#define GW_PW_HASH_LEN 32`, `#define GW_PW_MIN 8`, `#define GW_PW_MAX 64`
- Produces: `esp_err_t gw_pw_hash(const char *pw, const uint8_t *salt, size_t salt_len, uint32_t iterations, uint8_t out[GW_PW_HASH_LEN]);`
- Produces: `bool gw_pw_verify(const char *pw, const uint8_t salt[GW_PW_SALT_LEN], const uint8_t hash[GW_PW_HASH_LEN], uint32_t iterations);` — constant-time compare.
- Produces: `bool gw_pw_policy_ok(const char *pw);` — byte length 8..64, no NUL inside, any bytes otherwise.

- [ ] **Step 1: Failing tests** in `test_password.c`:
  - `test_pbkdf2_vector_c1`: pw `"password"`, salt `"salt"` (4 bytes), iterations 1 → `120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b`.
  - `test_pbkdf2_vector_c2`: iterations 2 → `ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43`.
  - `test_verify_ok_and_wrong`: pw `"admin123"`, salt bytes `0x00..0x0F`, iterations 10 → hash `9244f4657e05d9e3182b7ca6b07ae31c0a51ec75c1326ac25d8ae420929f8888`; verify `"admin123"` true, `"admin124"` false, `""` false.
  - `test_policy_bounds`: 7×`a` false, 8×`a` true, 64×`a` true, 65×`a` false, `NULL` false, `"pässwörd"` (10 bytes) true.
- [ ] **Step 2: Run** host tests → FAIL (undefined symbols).
- [ ] **Step 3: Implement** with the PBKDF2-HMAC-SHA256 API of v6.1's mbedTLS (`mbedtls_pkcs5_pbkdf2_hmac_ext` if present, else PSA `psa_key_derivation_*` with `PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256)`); constant-time compare via mbedTLS's public `mbedtls_ct_memcmp` if exported, else a local XOR-accumulate loop.
- [ ] **Step 4: Run** host tests → all pass.
- [ ] **Step 5: Commit** `feat(core): PBKDF2 password hashing and policy`.

### Task 3: Sessions, login limiter, login decision (`gw_core/gw_auth`)

**Files:** Create `components/gw_core/include/gw_auth.h`, `components/gw_core/gw_auth.c`, `test/host/main/test_auth.c`.

**Interfaces:**
- Consumes: `gw_pw_verify`, `GW_PW_*` (Task 2).
- Produces:
  ```c
  #define GW_TOKEN_HEX_LEN 64
  typedef struct {
      const char *user;              // fixed admin user name
      const char *default_password;  // valid only while !pw_set
      bool pw_set;
      uint8_t salt[GW_PW_SALT_LEN];
      uint8_t hash[GW_PW_HASH_LEN];
      uint32_t iterations;
  } gw_creds_t;
  typedef struct gw_auth gw_auth_t;  // opaque; owns sessions + limiter
  typedef enum { GW_LOGIN_OK, GW_LOGIN_BAD, GW_LOGIN_LOCKED } gw_login_result_t;
  typedef void (*gw_rand_fn)(uint8_t *buf, size_t len);   // esp_fill_random on target, fake in tests

  gw_auth_t *gw_auth_create(size_t max_sessions, uint32_t timeout_s, gw_rand_fn rnd);
  void gw_auth_destroy(gw_auth_t *a);
  gw_login_result_t gw_auth_login(gw_auth_t *a, const gw_creds_t *c, const char *user, const char *pw,
                                  uint32_t now_s, char token_out[GW_TOKEN_HEX_LEN + 1], uint32_t *retry_after_s);
  bool gw_auth_check(gw_auth_t *a, const char *token, uint32_t now_s);   // refreshes idle timer on success
  void gw_auth_logout(gw_auth_t *a, const char *token);
  void gw_auth_logout_all(gw_auth_t *a);                                 // after password change
  bool gw_cookie_get(const char *cookie_header, const char *name, char *out, size_t out_len);
  ```
- Rules: limiter is global; 5 consecutive failures → `GW_LOGIN_LOCKED` for 30 s (`retry_after_s` = seconds left), a success resets the count; while locked even correct creds return LOCKED. When the table is full, a new login evicts the least-recently-used session. Default password accepted only when `pw_set == false`.

- [ ] **Step 1: Failing tests** in `test_auth.c` (fake `rnd` fills an incrementing counter so tokens are deterministic):
  - `test_login_default_before_set`: `admin/admin`, `pw_set=false` → OK, token 64 hex chars, `gw_auth_check` true.
  - `test_default_rejected_after_set`: `pw_set=true` (hash of `"newpass123"`), `admin/admin` → BAD; `admin/newpass123` → OK.
  - `test_wrong_user` → BAD.
  - `test_lockout_after_5`: 5× BAD at t=100 → 6th attempt with correct creds at t=101 → LOCKED, `retry_after_s==29`; at t=130 correct → OK.
  - `test_session_idle_timeout`: timeout 900; check at t+899 true (refreshes), at t+899+900 true, then t+899+1801 false.
  - `test_token_rejects`: `NULL`, `""`, 63-char, 65-char, unknown 64-hex → false.
  - `test_lru_eviction`: max 2; login A, B, check A, login C → A valid, B invalid, C valid.
  - `test_logout_all`: two sessions → both invalid.
  - `test_cookie_parse`: `"a=1; gw_session=abc; b=2"` → `"abc"`; `"gw_session_x=1"` → not found; `NULL` → false; value longer than out → false.
- [ ] **Step 2: Run** host tests → FAIL.
- [ ] **Step 3: Implement** `gw_auth.c` (fixed array of `{char tok[65]; uint32_t last;}` sized at create; token = hex of 32 random bytes; compare tokens in constant time).
- [ ] **Step 4: Run** host tests → all pass.
- [ ] **Step 5: Commit** `feat(core): sessions, login limiter, cookie parsing`.

### Task 4: Setup step machine (`gw_core/gw_setup`)

**Files:** Create `components/gw_core/include/gw_setup.h`, `components/gw_core/gw_setup.c`, `test/host/main/test_setup.c`.

**Interfaces:**
- Produces:
  ```c
  typedef enum { GW_STEP_PASSWORD = 0, GW_STEP_WIFI = 1, GW_STEP_TLS = 2, GW_STEP_FINISH = 3, GW_STEP_DONE = 4 } gw_step_t;
  bool gw_setup_allowed(gw_step_t current, gw_step_t requested);   // requested <= current && current != DONE
  gw_step_t gw_setup_complete(gw_step_t current, gw_step_t completed); // completed == current → current+1, else current
  const char *gw_setup_step_name(gw_step_t s);  // "password","wifi","tls","finish","done"; "unknown" otherwise
  gw_step_t gw_setup_sanitize(uint8_t stored);  // out-of-range NVS value → GW_STEP_PASSWORD
  ```
- [ ] **Step 1: Failing tests:** `test_cannot_skip` (current PASSWORD, request WIFI → false); `test_can_redo_earlier` (current TLS, request WIFI → true); `test_done_allows_nothing`; `test_complete_advances_only_current` (PASSWORD+PASSWORD → WIFI; WIFI+PASSWORD → WIFI); `test_names`; `test_sanitize` (7 → PASSWORD, 2 → TLS).
- [ ] **Step 2: Run** → FAIL. **Step 3: Implement.** **Step 4: Run** → pass.
- [ ] **Step 5: Commit** `feat(core): setup step machine`.

### Task 5: Wi-Fi list, credential checks, AP password (`gw_core/gw_wifi_util`)

**Files:** Create `components/gw_core/include/gw_wifi_util.h`, `components/gw_core/gw_wifi_util.c`, `test/host/main/test_wifi_util.c`. `gw_core` now requires `json` (cJSON; if v6.1 moved it to the registry, add `espressif/cjson` to both `main/idf_component.yml` and `test/host/main/idf_component.yml`).

**Interfaces:**
- Produces:
  ```c
  typedef enum { GW_AUTH_OPEN, GW_AUTH_WEP, GW_AUTH_WPA, GW_AUTH_WPA2, GW_AUTH_WPA3, GW_AUTH_ENTERPRISE, GW_AUTH_OTHER } gw_auth_mode_t;
  typedef struct { char ssid[33]; int8_t rssi; gw_auth_mode_t auth; uint8_t channel; } gw_ap_info_t;
  #define GW_SCAN_MAX 20
  size_t gw_ap_list_normalize(gw_ap_info_t *aps, size_t n);  // drop empty SSID, dedupe by SSID keeping strongest, sort RSSI desc, cap GW_SCAN_MAX; returns new count
  char *gw_ap_list_json(const gw_ap_info_t *aps, size_t n);  // malloc'd: [{"ssid":..,"rssi":..,"auth":"wpa2","channel":..}]; caller frees
  bool gw_wifi_creds_valid(const char *ssid, const char *pass); // ssid 1..32 bytes; pass "" (open) or 8..63 bytes or exactly 64 hex
  #define GW_AP_PW_LEN 12
  void gw_ap_pw_from_random(const uint8_t rnd[GW_AP_PW_LEN], char out[GW_AP_PW_LEN + 1]); // alphabet[rnd[i] & 31]
  ```
  Alphabet (32 chars, exact): `ABCDEFGHJKLMNPQRSTUVWXYZ23456789`. Auth strings: `open, wep, wpa, wpa2, wpa3, enterprise, other`.
- [ ] **Step 1: Failing tests:**
  - `test_normalize`: input `[("a",-70),("b",-40),("a",-50),("",-30)]` → `[("b",-40),("a",-50)]`, count 2.
  - `test_normalize_caps_at_20`: 25 distinct → 20, strongest kept.
  - `test_json_escapes`: SSIDs `he said "hi"`, `back\slash`, a 32-byte SSID, bytes `{0xC3,0x28}` (invalid UTF-8) → output parses with `cJSON_Parse` and the array has 4 items; first SSID round-trips exactly.
  - `test_creds_valid`: `("", "12345678")` false; 33-byte SSID false; `("net","")` true; `("net","1234567")` false; 8 and 63 bytes true; 64 hex true; 64 chars with a `g` false.
  - `test_ap_pw`: rnd `{0,1,2,...,11}` → `"ABCDEFGHJKLM"`; rnd all `0xFF` → `"999999999999"`; length 12, NUL-terminated.
- [ ] **Step 2: Run** → FAIL. **Step 3: Implement.** **Step 4: Run** → pass.
- [ ] **Step 5: Commit** `feat(core): scan list normalization, credential checks, AP password`.

### Task 6: Firmware build check for `gw_core`

**Files:** Modify `main/CMakeLists.txt` (REQUIRES `gw_core`), `main/main.c` (computes and logs device ID / hostname / SSID from `esp_read_mac(..., ESP_MAC_WIFI_STA)`).

- [ ] **Step 1:** Build esp32 (command from Task 1 Step 5) → exit 0, no warnings from `gw_core`.
- [ ] **Step 2 (user, hardware):** `idf.py -B build/esp32 -p COMx flash monitor` → log shows `id=XXXX host=gatewayXXXX ssid=Gateway-Setup-XXXX` matching the board's MAC.
- [ ] **Step 3: Commit** `feat(main): derive device identity at boot`.

### Task 7: NVS storage (`gw_store`)

**Files:** Create `components/gw_store/CMakeLists.txt`, `components/gw_store/include/gw_store.h`, `components/gw_store/gw_store.c`, `test/host/main/test_store.c` (host app's `partitions.csv` gets `nvs` and `factory` entries; the linux target emulates partitions in a file).

**Interfaces:**
- Consumes: `gw_step_t`, `gw_setup_sanitize` (Task 4); `GW_PW_*` (Task 2); `GW_AP_PW_LEN` (Task 5).
- Produces:
  ```c
  typedef enum { GW_STATE_SETUP = 0, GW_STATE_PROVISIONED = 1 } gw_state_t;
  esp_err_t gw_store_init(void);                       // inits "nvs" (erase+retry on NO_FREE_PAGES/NEW_VERSION) and "factory"
  gw_state_t gw_store_get_state(void);                 // missing → SETUP
  esp_err_t gw_store_set_state(gw_state_t s);
  gw_step_t gw_store_get_step(void);                   // missing/invalid → PASSWORD
  esp_err_t gw_store_set_step(gw_step_t s);
  esp_err_t gw_store_get_password(bool *is_set, uint8_t salt[GW_PW_SALT_LEN], uint8_t hash[GW_PW_HASH_LEN]);
  esp_err_t gw_store_set_password(const uint8_t salt[GW_PW_SALT_LEN], const uint8_t hash[GW_PW_HASH_LEN]);
  esp_err_t gw_store_get_wifi(char ssid[33], char pass[65]);  // ESP_ERR_NOT_FOUND if none
  esp_err_t gw_store_set_wifi(const char *ssid, const char *pass);
  esp_err_t gw_store_get_ap_password(char out[GW_AP_PW_LEN + 1]); // "factory" partition, ns "fct"; ESP_ERR_NOT_FOUND if none
  esp_err_t gw_store_set_ap_password(const char *pw);
  esp_err_t gw_store_factory_reset(void);              // erases "nvs" only
  ```
  NVS namespace `gw`, keys exactly: `state`, `setup_step`, `pw_salt`, `pw_hash`, `wifi_ssid`, `wifi_pass`. Each setter commits before returning. Callers save data **before** advancing the step (so power loss can only repeat a step, never skip one).
- [ ] **Step 1: Failing tests:** `test_fresh_defaults` (state SETUP, step PASSWORD, password not set, wifi NOT_FOUND); `test_roundtrip_all`; `test_invalid_step_sanitized` (write raw u8 9 → PASSWORD); `test_factory_reset_keeps_ap_password` (set all + AP pw, reset, re-init → defaults, AP pw still there); `test_wifi_max_lengths` (32-byte SSID, 64-byte pass round-trip). Each test starts from `nvs_flash_erase` of both partitions.
- [ ] **Step 2: Run** → FAIL. **Step 3: Implement.** **Step 4: Run** → pass. (If v6.1's linux target cannot emulate NVS, stop and report — don't fake it.)
- [ ] **Step 5: Build esp32** → exit 0. **Step 6: Commit** `feat(store): NVS storage for state, credentials, wifi`.

### Task 8: Wi-Fi setup mode (`gw_net/gw_wifi`)

**Files:** Create `components/gw_net/CMakeLists.txt`, `components/gw_net/include/gw_wifi.h`, `components/gw_net/gw_wifi.c`. Modify `main/main.c`: in SETUP state get AP password (FIXED: Kconfig; PER_DEVICE: from store, else generate with `esp_fill_random` + `gw_ap_pw_from_random`, save, then `ESP_LOGW` it), check it with `gw_wifi_creds_valid` (abort with a clear error if invalid), start AP.

**Interfaces:**
- Consumes: `gw_ap_info_t`, `gw_ap_list_normalize`, `GW_SCAN_MAX` (Task 5).
- Produces:
  ```c
  esp_err_t gw_wifi_start_setup(const char *ap_ssid, const char *ap_password, uint8_t channel); // APSTA, WPA2-PSK, max 4 clients, AP IP 192.168.4.1
  esp_err_t gw_wifi_scan(gw_ap_info_t *out, size_t max, size_t *count);   // blocking active scan, maps auth, normalizes
  typedef enum { GW_WIFI_TEST_IDLE, GW_WIFI_TEST_RUNNING, GW_WIFI_TEST_OK, GW_WIFI_TEST_FAILED } gw_wifi_test_state_t;
  esp_err_t gw_wifi_test_start(const char *ssid, const char *pass);  // async; ESP_ERR_INVALID_STATE if RUNNING
  gw_wifi_test_state_t gw_wifi_test_get(char *reason, size_t reason_len); // reason: "auth_failed" | "no_ap_found" | "timeout" | "other"
  ```
  Test timeout 20 s; OK = got IP. On FAILED the STA disconnects; on OK it stays connected.
- [ ] **Step 1: Build esp32** → exit 0.
- [ ] **Step 2 (user, hardware):** flash + monitor. Expect `Gateway-Setup-XXXX` visible on a phone; joining with the configured password works; log shows client connected. Re-run with `GW_AP_AUTH_PER_DEVICE` (`idf.py menuconfig`): password printed once, same password after reboot.
- [ ] **Step 3: Commit** `feat(net): setup-mode SoftAP, scan, async test-connect`.

### Task 9: Captive DNS and mDNS (`gw_net/gw_dns`, `gw_net/gw_mdns`)

**Files:** Create `components/gw_net/gw_dns.c`, `components/gw_net/include/gw_dns.h` (adapted from ESP-IDF example `protocols/http_server/captive_portal` `dns_server`, CC0 — note the origin in the file header), `components/gw_net/gw_mdns.c`, `components/gw_net/include/gw_mdns.h`. Modify `main/idf_component.yml` (`espressif/mdns`), `main/main.c`.

**Interfaces:**
- Produces: `esp_err_t gw_dns_start(uint32_t answer_ip_be);` — answers every A query with that IP.
- Produces: `esp_err_t gw_mdns_start(const char *hostname, const char *instance, bool https);` — `_http._tcp` 80 or `_https._tcp` 443; instance `"Gateway XXXX"`.
- [ ] **Step 1: Build esp32** → exit 0.
- [ ] **Step 2 (user, hardware):** laptop joined to the setup AP: `nslookup example.com 192.168.4.1` → `192.168.4.1`; `ping gatewayXXXX.local` resolves to `192.168.4.1` (Windows 10+ / macOS).
- [ ] **Step 3: Commit** `feat(net): captive DNS and mDNS`.

### Task 10: Setup HTTP server and JSON API (`gw_http`)

**Files:** Create `components/gw_http/CMakeLists.txt`, `components/gw_http/include/gw_http_setup.h`, `components/gw_http/gw_http_setup.c`, `components/gw_http/gw_http_util.c` (+ private `gw_http_util.h`), `main/www_placeholder.html` (embedded via `EMBED_TXTFILES`), `tools/smoke/setup_api.ps1`. Modify `main/main.c`.

**Interfaces:**
- Consumes: Tasks 3, 4, 5, 7, 8 interfaces.
- Produces: `typedef struct { const char *device_id, *hostname, *fw_version; const gw_creds_t *defaults; } gw_http_setup_cfg_t;` and `esp_err_t gw_http_setup_start(const gw_http_setup_cfg_t *cfg);`
- API (exact; all JSON; errors are `{"error":"<code>"}`):
  | Method/path | Auth | Step | Success | Errors |
  |---|---|---|---|---|
  | `GET /api/status` | no | any | `{"mode":"setup","step":"<name>","device_id":"1A2B","hostname":"gateway1A2B","fw_version":"0.1.0","logged_in":bool}` | — |
  | `POST /api/login` `{"user","password"}` | no | any | 200 `{"ok":true,"must_change_password":bool}` + Set-Cookie | 401 `bad_credentials`, 429 `locked` + `"retry_after":n` |
  | `POST /api/logout` | yes | any | 200 | 401 |
  | `POST /api/setup/password` `{"new_password"}` | yes | ≤ current (PASSWORD) | 200; saves salt+hash, logs out all sessions except caller's new one: response sets a fresh cookie | 400 `weak_password`, 401, 409 `wrong_step` |
  | `GET /api/setup/wifi/scan` | yes | WIFI allowed | 200 JSON array (Task 5) | 401, 409, 503 `scan_failed` |
  | `POST /api/setup/wifi/test` `{"ssid","password"}` | yes | WIFI allowed | 202 `{"state":"running"}` | 400 `invalid_credentials`, 409 `wrong_step` / `test_running` |
  | `GET /api/setup/wifi/result` | yes | WIFI allowed | `{"state":"idle|running|ok|failed","reason":?}`; first read of `ok` saves Wi-Fi then completes WIFI step | 401, 409 |
  Body > 512 → 413 `too_large`; POST without `application/json` → 415 `bad_content_type`. Unknown paths → `302 Location: http://192.168.4.1/` (captive portal), `/` → placeholder page. Server: `max_open_sockets` 7, `lru_purge_enable` true.
- [ ] **Step 1: Write `tools/smoke/setup_api.ps1`** (params `-Base http://192.168.4.1`, `-Ssid`, `-WifiPass`): runs the sequence and prints PASS/FAIL per check — status (step `password`); scan before login → 401; wrong login ×1 → 401; login admin/admin → `must_change_password:true`; scan → 409 `wrong_step`; password `short` → 400; password `Learning-123` → 200; old cookie → 401 on scan; login admin/admin → 401; login with new pw → 200; scan → array; 600-byte body → 413; `text/plain` → 415; test with given SSID → 202; second test → 409 `test_running`; poll result until `ok`/`failed` (60 s max, tolerate connection errors while the AP changes channel); status step `tls` on `ok`. Exit code = number of failures.
- [ ] **Step 2: Implement** `gw_http_setup.c` handlers and `gw_http_util.c` (body read with limit, JSON parse/send helpers, cookie → `gw_auth_check`, `uptime_s` from `esp_timer_get_time()/1e6`). Wire into `main.c` for SETUP state (creds from store; `gw_auth_create(CONFIG_GW_MAX_SESSIONS, CONFIG_GW_SESSION_TIMEOUT_MIN*60, esp_fill_random)`).
- [ ] **Step 3: Build esp32** → exit 0; run host tests → all pass (no regressions).
- [ ] **Step 4 (user, hardware):** fresh flash (`idf.py -p COMx erase-flash flash monitor`), PC joins setup AP, run `./tools/smoke/setup_api.ps1 -Ssid "<home ssid>" -WifiPass "<pw>"` → exit code 0; phone joining the AP shows the captive page automatically. Reboot board → status step still `tls`, login needs the new password.
- [ ] **Step 5: Commit** `feat(http): setup-mode HTTP server and JSON API`.

### Task 11: Docs for Plan 1

**Files:** Create `README.md` (what it is, toolchain setup = Task 0, build/flash/host-test commands, Kconfig options table with why), `docs/TESTING.md` (host tests + the hardware checks of Tasks 6, 8, 9, 10), `docs/ARCHITECTURE.md` (component map, boot flow, why pure-logic/glue split, partition table explained line by line).

- [ ] **Step 1:** Write the three docs; every command in them is one that was run in this plan.
- [ ] **Step 2:** Final checks: host tests all pass; esp32 build exit 0; `git grep -nE "ESP_LOG.*(pass|token)"` shows only the per-device AP password line.
- [ ] **Step 3: Commit** `docs: README, architecture and testing guide for setup mode`.
