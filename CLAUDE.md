# ESP32 Gateway Web Server — agent rules

Learning project: ESP-IDF firmware for a gateway that provisions over a SoftAP (HTTP), then runs an
HTTPS server (server-auth or mutual TLS, chosen in Kconfig) with mDNS hostname `gatewayXXXX.local`.
Approved design: `docs/plans/2026-11-20-gateway-design.md`. The user is learning — explain the *why*
of ESP-IDF and TLS choices in docs and comments, not just the *what*.

## 1. Think before coding
- State assumptions explicitly. If a requirement has more than one reading, list them and ask.
- If a simpler approach exists, say so and push back.
- If something is unclear (an ESP-IDF API, a chip capability, a TLS behaviour), stop and check the
  ESP-IDF docs/headers instead of guessing. Name the IDF version an API comes from.

## 2. Simplicity first
- Write the minimum code that meets the design. No features, abstractions or Kconfig options that
  the design does not ask for.
- No error handling for impossible cases — but every `esp_err_t` that *can* fail is checked
  (`ESP_RETURN_ON_ERROR` / `ESP_GOTO_ON_ERROR`, never a silent ignore).
- If 200 lines could be 50, rewrite it.

## 3. Surgical changes
- Touch only what the task needs. Match the existing style. Don't refactor what isn't broken.
- Remove what your own change made unused; mention (don't delete) other dead code you notice.
- Every changed line should trace back to the request.

## 4. Goal-driven execution
- Turn each task into a verifiable goal ("write a host test for a bad cert/key pair, make it pass").
- Multi-step work: brief plan, each step with its check. Use the `milestone-plan` skill for anything
  bigger than one component.
- Never claim "done", "fixed" or "builds" without fresh output from the command that proves it
  (see the `verify-before-done` skill).

## Project skills (in `.claude/skills/`)
| Skill | Use when |
|---|---|
| `milestone-plan` | Starting a milestone or feature: plan tasks with files, seams and checks, then execute in order |
| `firmware-tdd` | Writing or changing logic in a component (auth, storage, cert validation, prov state machine, JSON) |
| `firmware-debugging` | Any bug, crash, boot loop, TLS handshake failure, Wi-Fi/mDNS problem |
| `verify-before-done` | Before saying a task is finished, before a commit |

Coding standards for writing and reviewing: `docs/CODING_STANDARDS.md` (the built-in `/code-review`
should check against it).

## Commands
Run from an ESP-IDF terminal (export.ps1 sourced). If `idf.py` is not on PATH, say so — do not guess results.
- Build one profile: `idf.py -B build/esp32 -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32" set-target esp32 build`
- Build all profiles: `./tools/build_all.ps1`
- Host unit tests (no hardware): `cd test/host; idf.py --preview set-target linux; idf.py build; ./build/host_test.elf`
- Flash + monitor: `idf.py -B build/esp32 -p COMx flash monitor` (hardware steps are the user's to run; ask them for the log)

## Hard rules
- Never commit private keys, `.p12` files or `tools/pki/out/`. The CA private key never goes on a device.
- Never log passwords, private keys, session tokens or Wi-Fi passwords (log lengths/fingerprints only).
- Target-specific code lives behind `CONFIG_IDF_TARGET_*` / `SOC_*` caps, in as few places as possible.
  ESP32-P4 has no radio: Wi-Fi comes from the ESP32-C6 through `esp_wifi_remote`; app code stays the same.
