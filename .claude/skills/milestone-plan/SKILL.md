---
name: milestone-plan
description: Plan and then execute a milestone of the ESP32 gateway firmware (a component, a feature, a target bring-up). Use before writing code for anything bigger than a single small change, and when resuming work on a planned milestone.
---

# Milestone plan → execute

Adapted for ESP-IDF from obra/superpowers `writing-plans` + `executing-plans` (MIT).

## 1. Check scope
- Read `CLAUDE.md`, the design doc in `docs/plans/`, and `docs/CODING_STANDARDS.md`.
- If the request has open design questions, ask them first (one at a time, recommend an answer).
  No code until the design question is settled.
- If the milestone spans several independent components, write one plan per component.

## 2. Write the plan
Save to `docs/plans/YYYY-MM-DD-<milestone>.md`:

```
# <Milestone>
Goal: one sentence.     Design: link.     Targets affected: esp32 / esp32 psram / s3 / s31 / p4+c6
Constraints: RAM/stack/flash limits, IDF version, security rules that apply.

## Task N: <name>
Files: create/modify (exact paths), host test file.
Interface: functions added/changed (signature + one-line contract).
Steps:
- [ ] Host test for <behaviour> — expect FAIL (`<command>`)
- [ ] Implement minimal code — expect PASS
- [ ] Build affected profiles — expect exit 0
- [ ] (hardware) What the user should see on serial / in the browser
```

Rules: exact paths, no "TBD" / "handle edge cases" lines, code only where the signature and test
don't already determine it. Order tasks so each one builds and runs on its own (tracer bullet first:
thinnest end-to-end path, then widen).

Show the plan to the user and get a go-ahead before executing.

## 3. Execute
- Work tasks in order. Use `firmware-tdd` for each logic change.
- After each task compare the command output with the plan's expected result. If the *code* is wrong
  → `firmware-debugging`. If the *plan* is wrong → fix the plan, write one line `Ruling: <what> — <why>`
  in it, continue.
- Tick the checkbox only after the check has actually passed in this session.
- Hardware steps: you cannot flash or watch the board. Give the user the exact command and what to
  look for, and wait for their log before ticking.
- Stop and ask only for: destructive actions (erase flash, delete files), security-sensitive choices,
  anything outside the project folder, or a plan so broken every path is a guess.

## 4. Finish
- Run `verify-before-done`.
- Final message: what was built, rulings made, what is untested (especially hardware/target gaps).
