---
name: firmware-debugging
description: Root-cause-first debugging for the ESP32 gateway firmware. Use for any bug, build failure, crash/panic, boot loop, stack overflow, heap exhaustion, TLS handshake failure, Wi-Fi connect/scan problem, mDNS not resolving, or a test that fails unexpectedly.
---

# Firmware debugging

Adapted from obra/superpowers `systematic-debugging` and mattpocock/skills `diagnosing-bugs` (MIT).

**Iron rule: no fix until the root cause is found and stated.**

## 1. Build a feedback loop first
Pick the fastest loop that can show the bug, in this order:
1. Host Unity test reproducing it (seconds, deterministic) — best.
2. Firmware build error → the exact `idf.py build` command + full first error.
3. Scripted check against the board: `curl -v --cacert ca.crt [--cert client.crt --key client.key] https://gatewayXXXX.local/`,
   `openssl s_client -connect gatewayXXXX.local:443 -CAfile ca.crt [-cert ... -key ...]`, `ping gatewayXXXX.local`.
4. Serial log from the user (`idf.py monitor`), with the steps they did. You cannot see the board:
   ask for the full log from boot, not a snippet.
Redact secrets (passwords, keys, PSKs) from anything you paste or save.

## 2. Read the evidence fully
- Panic: decode the backtrace (`idf.py monitor` does it; else `xtensa-esp32-elf-addr2line -pfiaC -e build/.../app.elf <addrs>`, or riscv32 for C/P4 chips).
  Note the panic reason: `LoadProhibited` (NULL/bad pointer), `Stack canary`/`stack overflow` (task stack), `abort()` (check the line before), watchdog (blocking in a task/ISR).
- TLS: mbedTLS error codes are negative hex (`-0x7780` = fatal alert received, `-0x2700` = cert verify failed, `-0x7200` = bad input / record too big). Map them with `mbedtls_strerror` or the header, don't guess.
- Memory: log `heap_caps_get_free_size(MALLOC_CAP_8BIT)` and `heap_caps_get_minimum_free_size` around the failing step; TLS needs ~35–45 KB per session on plain ESP32.
- Check what changed last (sdkconfig diff, component versions, target switch).

## 3. Hypothesise, then test one variable
- Write 1–3 falsifiable hypotheses, most likely first ("handshake fails because the server cert lacks SAN gateway1234.local — then `openssl s_client` shows a verify error 62"). Share them.
- Change one thing at a time. Temporary logs are tagged `[DBG-xxxx]` so they can be found and removed.

## 4. Fix
- Write a failing test first if the bug is in host-testable logic (`firmware-tdd`).
- One fix at the root cause, not around the symptom (don't just enlarge a stack without knowing the peak — measure with `uxTaskGetStackHighWaterMark`).
- Verify with the original loop, then `verify-before-done`. Remove every `[DBG-` line.

## 5. Three strikes
If three fixes fail, stop. Tell the user what was tried and what that rules out, and question the
design (memory budget, task layout, TLS mode on that chip) together before attempting a fourth.
