---
name: verify-before-done
description: Evidence gate before claiming a task is done, fixed, building, or passing in the ESP32 gateway project, and before any commit. Use whenever about to report success.
---

# Verify before done

Adapted from obra/superpowers `verification-before-completion` (MIT).

No "done / fixed / works / builds" without fresh output from this session that proves it.

| Claim | Evidence required |
|---|---|
| Logic works | Host suite run now: Unity summary shows `0 Failures` |
| Bug fixed | The test/loop that reproduced it now passes; the test was seen failing before the fix |
| Builds | `idf.py build` exit 0 for every profile the change can affect (`tools/build_all.ps1` when shared code or Kconfig/sdkconfig changed). A clean compile of one target does not prove the others |
| Fits | Size report (`idf.py size`) still fits the app partition; note growth if > 5% |
| Works on hardware | The user's serial log / browser result. If you didn't get it, say "not tested on hardware" |
| Requirements met | Line-by-line check against the milestone plan / design doc |
| Subagent says done | Read the diff yourself |

Steps: name the command → run it fully → read the whole output and exit code → only then state the
result, quoting the key line. If a step could not run (no IDF on PATH, no board), say exactly that.

Red flags in your own message: "should work", "probably", "Done!" before any command ran,
reusing an old run's result.

Also before finishing: no `[DBG-` logs left, no secrets in logs or committed files, docs updated if
behaviour or Kconfig options changed.
