---
name: bug-runbook
description: Use when FileScout fails to launch, crashes, shows startup dialogs, misroutes pane actions, or a shell-backed operation behaves incorrectly and you need a structured debugging workflow. Trigger for crash triage, startup failures, manual repro investigation, log collection, or when the user asks for root-cause analysis instead of just a quick fix.
---

# Bug Runbook

Use this skill to turn a vague FileScout problem into a small, evidence-backed report before making changes.

## Quick start

1. Run `scripts/collect-bug-report.ps1` to gather the current repo and temp-log state.
2. Read [references/log-sources.md](references/log-sources.md).
3. Classify the failure before editing code:
   - bootstrap or startup failure before the main window
   - runtime UI bug after launch
   - shell operation bug such as copy, paste, rename, delete, or context menu
4. Form one concrete hypothesis at a time and test it.
5. Report the symptom, repro, evidence, likely subsystem, and next action.

## Symptom routing

- No window or an immediate error dialog:
  inspect `native\FileScout.Native\main.cpp`, `native\FileScout.Native\App.xaml.cpp`, and the temp logs.
- Window launches but pane or tab behavior is wrong:
  inspect `native\FileScout.Native\MainWindow.xaml.cpp` and the active-pane state flow.
- Copy, paste, rename, delete, or context menu issues:
  inspect `native\FileScout.Native\Shell\`.

## Gotchas

- `TraceLog` writes an append-only temp log. Read it before reproducing too many times.
- `main.cpp` also writes a separate startup log for failures that happen before the main window appears.
- Shell services throw `std::runtime_error`; do not lose the first thrown message while adding extra logging.
- Do not revert unrelated user changes while debugging.
