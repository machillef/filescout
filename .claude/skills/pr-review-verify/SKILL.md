---
name: pr-review-verify
description: Use when reviewing local FileScout changes, preparing a commit or pull request, or when the user asks for a code review focused on bugs, regressions, verification gaps, and release risk. Trigger before handoff, before commit, after native changes, or when the user wants findings rather than a broad summary.
---

# PR Review Verify

Use this skill for review-mode work. Findings come first. Summaries are secondary.

## Workflow

1. Read [references/review-checklist.md](references/review-checklist.md).
2. Inspect `git diff --stat` and the actual changed hunks.
3. Prioritize findings in this order:
   - correctness and regressions
   - verification gaps
   - security-sensitive file handling
   - performance and resource lifetime
4. If files under `native\` changed, run the `build-verify-windows` workflow unless the user explicitly asked for review without execution.
5. If there are no findings, say that explicitly and call out remaining risk from missing automated UI coverage.

## FileScout-specific review focus

- Active-pane routing in `MainWindow`
- Shell operation correctness for copy, cut, paste, rename, delete, and drag and drop
- Startup and bootstrap behavior
- Context menu and shell overflow integration
- Changes that silently desync the repo docs from the real build or artifact paths

## Output shape

- Findings first, ordered by severity, with file references.
- Then open questions or assumptions.
- Then a short verification summary.
