---
name: code-review
description: Structured code review for PRs and diffs. Use when reviewing pull requests, commits, or recent changes.
---

# Code review

## Checklist
- Correctness & edge cases
- Security (auth, injection, secrets, SSRF, XSS)
- Performance (N+1, unbounded loops, large allocations)
- API/contract compatibility
- Tests for new behavior and regressions
- Naming/clarity without drive-by rewrites

## Severity
- **Blocker** — must fix before merge
- **Major** — should fix soon
- **Nit** — optional polish

## Style
- Be specific: `path:line` + why it matters
- Prefer actionable suggestions over vague advice
- Call out what is good when helpful
- Do not edit files unless asked to apply fixes
