# Slices: startup-profiling

## SP1: ScopedTimer helper + session markers

Goal:
Extend `TraceHelper.h` with a `ScopedTimer` RAII helper that logs a "done in Xms" line on
destruction, and add a structured session-start marker in `wWinMain` so each app launch is
clearly delimited in the log file.

Touched area:
- `native/FileScout.Native/TraceHelper.h` — add `ScopedTimer`, upgrade log format
- `native/FileScout.Native/main.cpp` — add session-start marker block
- `native/FileScout.Tests/tests/ScopedTimerTests.cpp` — new test file
- `native/FileScout.Tests/FileScout.Tests.vcxproj` — add new test file

TDD required: Yes — `ScopedTimer` is pure logic and is fully testable.

Done criteria:
- `ScopedTimer timer{L"label"}` logs `[TIMER] label: done in Xms` on destruction
- Session-start marker logs: date, time, process ID, Windows version string
- All existing tests still pass

Validation:
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=ScopedTimer*`
- Full suite: `native\bin\x64\Debug\Tests\FileScout.Tests.exe`

---

## SP2: Instrument the icon-loading hot path

Goal:
Wrap every `GetItemIcon` call in `BuildSidebarFavorites`, `UpdateBookmarksSection`, and
`UpdateThisPCSection` with `ScopedTimer` instances so each literal `SHGetFileInfo` call is
logged with its path and duration. This is the primary suspected source of startup slowdown.

Touched area:
- `native/FileScout.Native/Shell/ShellInteropService.cpp` — log cache-hit vs cache-miss +
  duration inside `GetItemIcon`
- `native/FileScout.Native/MainWindow.xaml.cpp` — `ScopedTimer` wrapping in
  `BuildSidebarFavorites`, `UpdateBookmarksSection`, `UpdateThisPCSection`

TDD required: No — shell API integration; verified by reading the log file after a launch.

Done criteria:
- Log contains one `[TIMER]` line per Quick Access icon showing path + ms
- Log contains `[TIMER] BuildSidebarFavorites: done in Xms` total
- Cache hits are logged as `[icon-cache] HIT: <path>` and cache misses as
  `[icon-cache] MISS: <path>`

Validation:
- Launch app, inspect `%TEMP%\FileScout.Native.trace.log` — confirm per-icon entries present
- Full suite must still pass

---

## SP3: Instrument the startup sequence end-to-end

Goal:
Replace the existing ad hoc `TraceLog("... done")` pairs in `MainWindow` ctor and `main.cpp`
with `ScopedTimer` instances so every startup phase reports its wall-clock duration
automatically. Add `ScopedTimer` to `InitPane` and `LoadDirectory`.

Touched area:
- `native/FileScout.Native/main.cpp` — wrap bootstrap, `init_apartment`, `Application::Start`
- `native/FileScout.Native/MainWindow.xaml.cpp` — replace manual TraceLog pairs in ctor with
  `ScopedTimer`; wrap `InitPane`, `LoadDirectory`

TDD required: No — startup sequencing is not unit-testable; verified by log inspection.

Done criteria:
- Log shows: `[TIMER] MddBootstrapInitialize2: done in Xms`
- Log shows: `[TIMER] init_apartment: done in Xms`
- Log shows: `[TIMER] Application::Start: done in Xms`
- Log shows: `[TIMER] InitializeComponent: done in Xms`
- Log shows: `[TIMER] InitPane(0): done in Xms`, `[TIMER] InitPane(1): done in Xms`
- Log shows: `[TIMER] LoadDirectory (<path>): done in Xms (N items)`
- No duplicate or orphaned manual `TraceLog("... done")` pairs remain

Validation:
- Launch app, confirm all above timer lines present in log
- Full suite must still pass

---

## SP4: Analysis script

Goal:
Write a PowerShell script `tools/Get-StartupProfile.ps1` that parses
`%TEMP%\FileScout.Native.trace.log`, groups entries by session (using the session-start markers
from SP1), and outputs a table of per-session timer values plus 7-day rolling averages for the
key metrics.

Touched area:
- `tools/Get-StartupProfile.ps1` — new file

TDD required: No — script is a read-only analysis tool.

Done criteria:
- Running `pwsh tools/Get-StartupProfile.ps1` outputs a table like:
  ```
  Session              BuildSidebarFavorites  InitPane(0)  LoadDirectory  TotalStartup
  -------              ---------------------  -----------  -------------  ------------
  2026-04-05 09:14:02  312 ms                 88 ms        71 ms          1240 ms
  2026-04-06 08:47:11  298 ms                 91 ms        68 ms          1190 ms
  ---
  7-day avg            305 ms                 89 ms        70 ms          1215 ms
  ```
- Script handles missing timer lines gracefully (shows `—` for absent metrics)
- Script handles log files with 0 or 1 sessions without crashing

Validation:
- Run script against a real log file after a few launches and confirm table is correct
