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
| SP1 | Complete | 2026-04-05 |
| SP2 | Complete | 2026-04-06 |
| SP3 | Not Started | 2026-04-05 |
| SP4 | Not Started | 2026-04-05 |

## Slice SP1: ScopedTimer helper + session markers
Status: Complete
Last updated: 2026-04-05

### What was implemented
- `TraceHelper.h`: added `ScopedTimer` RAII class — takes a label and optional sink (defaults to
  `TraceLog`); logs `[TIMER] <label>: done in <N>ms` on destruction. Copy and move both explicitly
  deleted. Uses `std::chrono::steady_clock` for monotonic timing.
- `main.cpp`: added `WindowsVersionString()` (via `RtlGetVersion` from ntdll) and `LogSessionStart()`
  that writes `=== SESSION START | YYYY-MM-DD | PID:N | Windows X.Y Build NNNNN ===` immediately
  after startup log reset. Consolidated previously split anonymous namespace blocks into one.
- `ScopedTimerTests.cpp` (new): 5 tests — sink called once, output format, `ms` suffix, numeric
  elapsed value, non-negative elapsed.
- `FileScout.Tests.vcxproj`: added `ScopedTimerTests.cpp`.

### What was validated
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=ScopedTimer*` —
  5/5 PASSED
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — 184/184 PASSED (179 → 184)

### What remains unverified
- Live log inspection: session-start marker format in real log file (requires launching the app).

### Blockers
None

## Slice SP2: Instrument the icon-loading hot path
Status: Complete
Last updated: 2026-04-06

### What was implemented
- `ShellInteropService.cpp`: added `#include "TraceHelper.h"` and cache hit/miss logging in
  `GetItemIcon`. Hit path: copies value under lock, releases lock, then logs
  `[icon-cache] HIT: <path>` (no I/O under mutex — HIGH issue from review, fixed). Miss path:
  logs `[icon-cache] MISS: <path>` before shell fetch.
- `MainWindow.xaml.cpp`:
  - `BuildSidebarFavorites`: `ScopedTimer{ L"BuildSidebarFavorites" }` at entry; per-icon
    `ScopedTimer{ L"GetItemIcon: " + path }` for Quick Access entries and
    `ScopedTimer{ L"GetItemIcon(recent): " + path }` for recent paths.
  - `UpdateBookmarksSection`: `ScopedTimer{ L"UpdateBookmarksSection" }` at entry; per-bookmark
    `ScopedTimer{ L"GetItemIcon(bookmark): " + path }`.
  - `UpdateThisPCSection`: `ScopedTimer{ L"UpdateThisPCSection" }` at entry only (no
    `GetItemIcon` calls — drives use custom XAML shapes).

### What was validated
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — 184/184 PASSED
- Review finding fixed: `TraceLog` no longer called while `m_cacheMutex` is held.

### What remains unverified
- Live log inspection: launch app and confirm per-icon `[TIMER]` lines appear and
  `[icon-cache] MISS:` entries show on cold start, `HIT:` on warm restart.
- MEDIUM (tracked): `ScopedTimer` label construction allocates 2 `wstring`s per call; destructor
  allocs 4 more. Acceptable for diagnostic use but inflates measurement slightly. No `#ifdef`
  guard on trace calls — will emit in release builds. Both deferred to a follow-up cleanup
  outside this initiative.

### Blockers
None

---

## Notes

- `TraceLog` (TraceHelper.h) already writes `HH:MM:SS.mmm [tickcount]` to
  `%TEMP%\FileScout.Native.trace.log`. The infrastructure exists; what's missing is automatic
  duration capture and per-call icon-load instrumentation.
- The new per-folder literal `SHGetFileInfo` calls added in the sidebar-icon-refresh initiative
  are the primary suspected cause of the slowdown felt at startup. SP2 will make this visible.
- No new external dependencies. `ScopedTimer` uses only `<chrono>` and `TraceLog`.
- Log file is append-only — sessions accumulate over time, which is exactly what we want for
  1-week analysis.
