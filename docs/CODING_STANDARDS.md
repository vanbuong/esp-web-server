# Coding standards

Used when writing code and by `/code-review`. A rule here overrides general habit.

## Module design (deep modules, clear seams)
- One ESP-IDF component = one module with a small public header in `include/`. Everything else is `static`.
- A component's header says its contract: what each function does, ownership of returned buffers,
  error codes it can return, whether it is thread-safe, and which task may call it.
- Hardware/IDF side effects sit behind a thin seam so logic can run on the `linux` host target:
  - pure logic (password hashing, cert/key validation, provisioning state machine, JSON building,
    hostname formatting) has **no** `esp_wifi`/`esp_http_server`/GPIO includes;
  - the IDF-facing glue (HTTP handlers, Wi-Fi events) calls into that logic and stays thin.
- A seam is real only when it has two users (firmware + host test). Don't add an adapter with one user.
- Deletion test: if removing a module just moves its code elsewhere unchanged, it was a pass-through — merge it.

## C conventions
- C17, ESP-IDF style: 4-space indent, `snake_case`, component prefix on public symbols (`storage_`, `auth_`, `wifi_mgr_`).
- Return `esp_err_t`; outputs through pointer params. Check every fallible call
  (`ESP_RETURN_ON_ERROR`, `ESP_GOTO_ON_ERROR` + cleanup label). No `ESP_ERROR_CHECK` outside `app_main` init.
- Each file has `static const char *TAG = "<component>";` Logs: `ESP_LOGE` for failures with the error name
  (`esp_err_to_name`), `ESP_LOGI` for state changes, `ESP_LOGD` for detail.
- Bounded strings only: `snprintf`, `strlcpy`; check lengths of everything that comes from HTTP or NVS.
- Every `malloc` has one obvious owner and a free on every path. Prefer stack buffers under ~512 B;
  big buffers (certs, scan results) on heap. Know the task stack size you are running in.
- No magic numbers: lengths and limits are `#define`s or Kconfig values.

## Security (this project handles credentials and TLS keys)
- Passwords: PBKDF2-SHA256 with random salt, stored as hash; compare in constant time.
- Never log or echo: passwords, private keys, session tokens, Wi-Fi PSKs.
- Validate all uploaded PEM with mbedTLS before storing; reject on any error; zero key buffers
  (`mbedtls_platform_zeroize`) after use.
- HTTP handlers: check session + provisioning step server-side; limit request body size; reject unexpected content types.
- Provisioning endpoints must be unreachable once provisioned; HTTPS endpoints unreachable in provisioning mode.

## Review smells to flag (judgement calls, not hard failures)
Mysterious name · duplicated code · long function (> ~60 lines) · data clumps (same 3 params everywhere → struct) ·
primitive obsession (raw `char*` for things with rules, e.g. hostnames) · repeated switches on mode/target ·
shotgun surgery (one change touches many components) · speculative generality (options nobody asked for).
