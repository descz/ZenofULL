---
name: debug-systematically
description: Systematic debugging with evidence, hypotheses, and minimal fixes. Use for bugs, crashes, failing tests, or unexpected behavior.
---

# Systematic debugging

## Loop
1. **Observe** — exact error, stack, command, environment
2. **Hypothesize** — one primary cause
3. **Test** — smallest command/read that confirms or rejects
4. **Fix** — minimal change only
5. **Verify** — re-run failing case + nearby regression risk
6. **Prevent** — add/adjust a test when practical

## Anti-patterns
- Broad refactors while debugging
- Changing multiple variables at once
- “It should work” without re-running
- Swallowing errors to hide symptoms

## Output when done
- Root cause
- Fix summary
- How verified
- Residual risks
