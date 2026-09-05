---
name: git-workflow
description: Safe git commits, branches, and PR workflow on Windows/PowerShell. Use when committing, branching, rebasing, or opening PRs with gh.
---

# Git workflow

## Rules
- Never update git config
- Never force-push to main/master unless explicitly requested
- Never skip hooks (`--no-verify`) unless explicitly requested
- Never commit secrets (`.env`, keys, tokens, credentials)
- Prefer small, focused commits

## Windows PowerShell notes
- Do not use bash HEREDOC for commit messages
- Use: `git commit -m "type: short summary"`
- Multi-line body: `git commit -m "summary" -m "body"`
- Chain dependent commands with `; if ($?) { ... }` not `&&` if shell is Windows PowerShell 5.1

## Standard pre-commit sequence
1. `git status -sb`
2. `git --no-pager diff` and `git --no-pager diff --cached`
3. `git --no-pager log --oneline -10`
4. Stage intentional files only
5. Commit with concise message matching repo style
6. `git status -sb` to verify

## PR sequence
1. Ensure branch is up to date with base when practical
2. Push with `-u` if no upstream
3. `gh pr create` with title + body (summary + test plan)
4. Return PR URL

## Message style
- Prefer conventional style when repo uses it: `feat:`, `fix:`, `docs:`, `refactor:`, `test:`, `chore:`
- Subject ≤ 72 chars, imperative mood
