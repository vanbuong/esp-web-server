#!/usr/bin/env bash
# Project SessionStart hook: injects the vendored superpowers bootstrap skill
# (.claude/skills/using-superpowers/SKILL.md) into the session context.
# Adapted from obra/superpowers hooks/session-start (MIT). Plain stdout of a
# SessionStart hook is added to Claude's context.
skill="${CLAUDE_PROJECT_DIR:-.}/.claude/skills/using-superpowers/SKILL.md"
printf '<EXTREMELY_IMPORTANT>\nYou have superpowers (vendored in .claude/skills; "superpowers:<name>" means the project skill "<name>").\n\nBelow is the full content of the using-superpowers skill. For all other skills, use the Skill tool:\n\n'
if [ -r "$skill" ]; then
  while IFS= read -r line || [ -n "$line" ]; do printf '%s\n' "$line"; done < "$skill"
else
  printf 'Error reading %s\n' "$skill"
fi
printf '</EXTREMELY_IMPORTANT>\n'
exit 0
