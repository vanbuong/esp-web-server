# Third-party notices

## Vendored skills (copied unchanged, except `agents/openai.yaml` files removed)
- `.claude/skills/{brainstorming, diagnosing-superpowers, dispatching-parallel-agents, executing-plans,
  finishing-a-development-branch, receiving-code-review, requesting-code-review, subagent-driven-development,
  systematic-debugging, test-driven-development, using-git-worktrees, using-superpowers,
  verification-before-completion, writing-plans, writing-skills}` — from obra/superpowers @ `22f0b463`
  (2026-11-03). License: `.claude/LICENSE.superpowers`.
- `.claude/skills/{setup-matt-pocock-skills, triage, to-tickets, to-spec, pr, domain-modeling, grilling}`
  — from mattpocock/skills @ `abeeaf95` (2026-11-19). License: `.claude/LICENSE.mattpocock-skills`.
- `.claude/hooks/session-start.sh` — adapted from obra/superpowers `hooks/session-start`.

To update: re-clone the repo and copy the same folders over, then re-check the diff.

## Adapted (rewritten for ESP-IDF)
`CLAUDE.md`, `.claude/skills/firmware-tdd`, `.claude/skills/firmware-debugging` and
`docs/CODING_STANDARDS.md` are adapted from ideas and rules in these MIT-licensed projects:

- **obra/superpowers** — Copyright (c) 2025 Jesse Vincent — https://github.com/obra/superpowers
  (writing-plans, executing-plans, test-driven-development, systematic-debugging, verification-before-completion)
- **mattpocock/skills** — Copyright (c) 2026 Matt Pocock — https://github.com/mattpocock/skills
  (tdd, diagnosing-bugs, codebase-design, code-review smell baseline)
- **andrej-karpathy-skills** (forrestchang / multica-ai) — https://github.com/multica-ai/andrej-karpathy-skills
  (four coding principles: think before coding, simplicity first, surgical changes, goal-driven execution)

MIT License: Permission is hereby granted, free of charge, to any person obtaining a copy of this
software and associated documentation files, to deal in the Software without restriction, including
the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies, subject
to the condition that the above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
