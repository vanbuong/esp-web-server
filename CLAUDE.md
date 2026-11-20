# ESP32 Gateway Web Server — agent rules

Learning project: ESP-IDF firmware for a gateway that provisions over a SoftAP (HTTP), then runs an
HTTPS server (server-auth or mutual TLS, chosen in Kconfig) with mDNS hostname `gatewayXXXX.local`.
The user is learning — explain the *why* of ESP-IDF and TLS choices in docs and comments.

These rules take precedence over any skill. Skills take precedence over default behaviour.

## 1. Think before coding
- State assumptions explicitly. If a requirement has more than one reading, list them and ask.
- If a simpler approach exists, say so and push back.
- If something is unclear (an ESP-IDF API, a chip capability, a TLS behaviour), check the ESP-IDF
  docs/headers instead of guessing. Name the IDF version an API comes from.

## 2. Simplicity first
- Minimum code that meets the design. No features, abstractions or Kconfig options it doesn't ask for.
- No error handling for impossible cases — but every `esp_err_t` that *can* fail is checked.
- If 200 lines could be 50, rewrite it.

## 3. Surgical changes
- Touch only what the task needs. Match the existing style. Don't refactor what isn't broken.
- Remove what your own change made unused; mention (don't delete) other dead code you notice.

## 4. Goal-driven execution
- Turn each task into a verifiable goal. Never claim "done/fixed/builds" without fresh command output.

## Workflow (skills in `.claude/skills/`)
`superpowers:<name>` in a skill means the project skill `<name>`.

| Stage | Skill |
|---|---|
| New feature / design | `brainstorming` → spec in `docs/specs/YYYY-MM-DD-<topic>-design.md` |
| Stress-test a design | `grilling`, `domain-modeling` (GLOSSARY.md + `docs/adr/`) |
| Plan | `writing-plans` → `docs/plans/YYYY-MM-DD-<feature>.md`; optionally `to-tickets` to split into tickets |
| Isolate work | `using-git-worktrees` (worktrees in `.worktrees/`) |
| Build | `executing-plans` (inline, default here) or `subagent-driven-development` (only when the user asks — it is costly) |
| Code | `test-driven-development` **+ `firmware-tdd`** (host tests, ESP-IDF specifics) |
| Bugs | `systematic-debugging` **+ `firmware-debugging`** (panics, TLS errors, heap, serial logs) |
| Done? | `verification-before-completion` + the firmware evidence table below |
| Review | `requesting-code-review` / `receiving-code-review`, standards in `docs/CODING_STANDARDS.md` |
| Integrate | `finishing-a-development-branch`, `pr` for PR bodies |
| Issues | `triage`, `to-spec`, `to-tickets` (tracker config in `docs/agents/`) |

## Firmware additions to planning and execution
- Every plan header lists **targets affected**: esp32, esp32+PSRAM, esp32s3, esp32s31, esp32p4+c6.
- Plan tasks in tracer-bullet order: thinnest end-to-end path first, each task builds and runs alone.
- Hardware steps: you cannot flash or watch the board. Give the user the exact command and what to
  look for, and wait for their serial log before ticking a hardware check.

### Firmware evidence (for `verification-before-completion`)
| Claim | Evidence |
|---|---|
| Logic works | Host Unity suite run now: `0 Failures` |
| Builds | `idf.py build` exit 0 for every affected profile (`tools/build_all.ps1` when shared code/Kconfig/sdkconfig changed) |
| Fits | `idf.py size` fits the app partition; note growth > 5% |
| Works on hardware | The user's serial log / browser result — otherwise say "not tested on hardware" |
Also: no `[DBG-` logs left, no secrets in logs or committed files.

## Commands
Run from an ESP-IDF terminal (export.ps1 sourced). If `idf.py` is not on PATH, say so — do not guess results.
- Build one profile: `idf.py -B build/esp32 -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32" set-target esp32 build`
- Build all profiles: `./tools/build_all.ps1`
- Host unit tests: `cd test/host; idf.py --preview set-target linux; idf.py build; ./build/host_test.elf`
- Flash + monitor (user runs it): `idf.py -B build/esp32 -p COMx flash monitor`

## Hard rules
- Never commit private keys, `.p12` files or `tools/pki/out/`. The CA private key never goes on a device.
- Never log passwords, private keys, session tokens or Wi-Fi passwords (log lengths/fingerprints only).
- Target-specific code lives behind `CONFIG_IDF_TARGET_*` / `SOC_*` caps, in as few places as possible.
  ESP32-P4 has no radio: Wi-Fi comes from the ESP32-C6 through `esp_wifi_remote`; app code stays the same.
