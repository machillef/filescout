---
name: winui-ui-verifier
description: Use when the user asks to verify FileScout UI behavior manually, reproduce a desktop bug, smoke-test pane or tab routing, validate shell-backed file operations, or confirm WinUI interactions after native changes. Trigger for requests to check the app visually, reproduce a bug, run manual QA, or verify specific keyboard shortcuts, context menus, or dual-pane behavior.
---

# WinUI UI Verifier

Use this skill for manual or guided verification of the running FileScout desktop app. This repo does not have a checked-in automated UI harness, so be explicit about what you verified and what remains manual.

## Quick start

1. If native code changed or the binary may be stale, run the `build-verify-windows` workflow first.
2. Create a disposable test sandbox with `scripts/new-ui-sandbox.ps1`.
3. Launch the app from the Debug artifact.
4. Follow [references/manual-smoke-checklist.md](references/manual-smoke-checklist.md).
5. If anything fails, capture exact steps, expected vs actual behavior, and whether the left or right pane was active.

## Gotchas

- FileScout is shell-backed. Never test rename, delete, copy, cut, paste, or drag and drop against real user files.
- Active-pane routing matters. Always say which pane was active when a command ran.
- Prefer small disposable folders so you can verify recycle bin behavior and drag and drop without collateral damage.
- If launch fails before the window opens, stop and switch to the bug runbook.

## Evidence to collect

- The sandbox path you used.
- The exact shortcut or pointer sequence.
- Any startup or trace logs if the repro is launch-related.
- Screenshots only when they clarify a UI issue; text repro steps are still required.
