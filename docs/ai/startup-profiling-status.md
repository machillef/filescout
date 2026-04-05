# Status: startup-profiling

## Initiative State

- Classification: Medium
- Branch baseline: `master`
- Objective: instrument startup and sidebar icon-loading paths with structured timing so we can measure
  real-world latency after a week of daily use
- Current blocker: none
- Implementation status: not started
- Build status: n/a
- Test status: n/a

## Slice Status

| Slice | Status | Last Updated |
|---|---|---|
| SP1 | Not Started | 2026-04-05 |
| SP2 | Not Started | 2026-04-05 |
| SP3 | Not Started | 2026-04-05 |
| SP4 | Not Started | 2026-04-05 |

## Notes

- `TraceLog` (TraceHelper.h) already writes `HH:MM:SS.mmm [tickcount]` to
  `%TEMP%\FileScout.Native.trace.log`. The infrastructure exists; what's missing is automatic
  duration capture and per-call icon-load instrumentation.
- The new per-folder literal `SHGetFileInfo` calls added in the sidebar-icon-refresh initiative
  are the primary suspected cause of the slowdown felt at startup. SP2 will make this visible.
- No new external dependencies. `ScopedTimer` uses only `<chrono>` and `TraceLog`.
- Log file is append-only — sessions accumulate over time, which is exactly what we want for
  1-week analysis.
