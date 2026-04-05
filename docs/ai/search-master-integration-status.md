# Status: search-master-integration

## Initiative State

- Classification: Large
- Branch baseline: `master`
- Objective: forward-port the missing fuzzy-search behavior from `ea49541` into current `master`
- Current blocker: none
- Implementation status: all slices complete
- Build status: canonical solution build passing
- Test status: full suite passing (148/148)

## Slice Status

| Slice | Status | Last Updated |
|---|---|---|
| SMI1 | Complete | 2026-03-29 |
| SMI2 | Complete | 2026-03-29 |
| SMI3 | Complete | 2026-03-29 |
| SMI4 | Complete | 2026-03-29 |
| SMI5 | Complete | 2026-04-05 |

## Notes

- This initiative exists because `feature/r17-fuzzy-search` cannot be merged directly into the
  current `master` UX architecture.
- Bootstrap intentionally leaves the historical top-level `docs/ai/plan.md`, `docs/ai/slices.md`,
  and `docs/ai/status.md` untouched.
- `SMI1` stayed within the planned pure-helper boundary and did not change `MainWindow` or
  `SearchService` behavior.
- `SMI2` hardened the worker contract without forcing the recursive-search UI rewrite early; the
  existing one-flag `MainWindow` callback still compiles through a compatibility overload.

## Slice SMI1: Add pure search helpers on master
Status: Complete
Last updated: 2026-03-29

### What was implemented
- Added `native/FileScout.Native/Search/SearchFilter.h` with pure fuzzy-filter ranking that returns
  snapshot pointers, preserves score-tie order, and caps long result sets.
- Added `native/FileScout.Native/Search/SearchPresentation.h` with deterministic helpers for
  progress text, completion text, search-result subtitles, highlight positions, open targets, and
  `SearchResult` to `ShellItemRef` shaping.
- Added focused `SearchFilter` and `SearchPresentation` tests and wired them into
  `native/FileScout.Tests/FileScout.Tests.vcxproj`.

### What was validated
- `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=SearchFilter*:*SearchPresentation*` — PASS
- Step 8 skipped — debugging not needed.

### What remains unverified
- No live `MainWindow` integration yet; helper behavior is only covered through focused tests in
  this slice.
- Full suite and manual host validation remain deferred to later slices.

### Blockers
None

### Next recommended step
Implement `SMI2` by forward-porting the safer `SearchService` worker behavior and adding
deterministic `SearchServiceTests` coverage for local-root checks, generation ownership, and
cancellation semantics.

## Slice SMI2: Harden SearchService on master
Status: Complete
Last updated: 2026-03-29

### What was implemented
- Updated `native/FileScout.Native/Search/SearchService.h` to support a failure-aware completion
  callback while preserving a one-flag compatibility overload for the existing UI call site.
- Updated `native/FileScout.Native/Search/SearchService.cpp` so detached work no longer reads a raw
  member generation counter, unsupported non-local roots fail fast, periodic result flushes happen
  during large walks, symlink targets are skipped, and depth-12 directories are still traversed.
- Rewrote `native/FileScout.Tests/tests/SearchServiceTests.cpp` around the hardened contract with
  deterministic futures covering cancellation, cap behavior, depth handling, unsupported roots, and
  repeated start/cancel sequences.

### What was validated
- `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=SearchService*` — PASS
- Step 8 skipped — debugging not needed.

### What remains unverified
- `MainWindow` still uses its pre-helper recursive-search presentation and completion text.
- The UI path for reporting unsupported roots or worker failure remains deferred to `SMI4`.

### Blockers
None

### Next recommended step
Implement `SMI3` by replacing the inline filter ranking in `MainWindow::SearchTextBox_TextChanged`
with `BuildFilterResults` while preserving the current pane snapshot/restore path.

## Slice SMI3: Replace inline filter logic in MainWindow
Status: Complete
Last updated: 2026-03-29

### What was implemented
- Replaced the inline `FuzzyScore` ranking block in `native/FileScout.Native/MainWindow.xaml.cpp`
  with `BuildFilterResults`.
- Kept the existing `search_snapshot`, `filtered_items`, and `FileRowViewModel` publication path
  intact so the slice changes ranking behavior without changing the surrounding filter-mode flow.
- Swapped the `MainWindow` search include from `Search/FuzzyMatcher.h` to `Search/SearchFilter.h`.

### What was validated
- `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=SearchFilter*:*SearchPresentation*:*SearchService*` — PASS
- Step 8 skipped — debugging not needed.

### What remains unverified
- The recursive-search UI still formats progress/completion strings inline and still uses the older
  ad hoc recursive row shaping.
- Manual confirmation of one-character query behavior and score-tie ordering in the live list view
  remains deferred to `SMI5`.

### Blockers
None

### Next recommended step
Implement `SMI4` by wiring recursive-mode status text, row shaping, cancellation reset, and
navigate-on-select through `SearchPresentation` plus the hardened `SearchService` contract.

## Slice SMI4: Align recursive search UI with helper contracts
Status: Complete
Last updated: 2026-03-29

### Bug Report (2026-03-29)
Reported by: User report / manual testing
Description: Recursive search could leave the live WinUI host in a `Not Responding` state while
rendering results for query `needle` under `C:\Users\machi`.

### Bug Report (2026-03-29)
Reported by: User report / manual testing
Description: After the first responsiveness fix, recursive search still produced `Not Responding`
while walking `C:\Users\machi`; the window could freeze even after visible matches had already been
rendered.

### What was implemented
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` and
  `native/FileScout.Native/MainWindow.xaml.h` to route recursive search presentation through
  `SearchPresentation` instead of inline strings and ad hoc row shaping.
- Added `ResetSearchState(...)` so cancellation, Escape, empty-query exit, pane switches, and
  directory navigation all tear down recursive-search state consistently.
- Rebuilt recursive result rows from `BuildSearchResultItems(...)`, including parent-relative
  subtitles and live icon hydration, while continuing to publish `FileRowViewModel` rows through
  the existing row pipeline.
- Wired recursive result activation through
  `::FileScoutNative::Search::TryBuildSearchOpenTarget(...)` before `LoadDirectory(...)`.
- Fixed a follow-up regression where Escape restored the list but left stale recursive status text in
  the bottom bar.
- Fixed a live-host responsiveness bug by moving recursive result icon extraction off the UI thread
  into chunked asynchronous backfill guarded by a per-search visible-row generation.
- Fixed a second responsiveness bug in `SearchService` by suppressing redundant timed result
  publishes when the heap has not changed, which prevents `MainWindow` from rebuilding identical
  recursive result rows throughout a long walk.

### What was validated
- `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=SearchService*` — PASS (`9/9`)
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=SearchFilter*:*SearchPresentation*:*SearchService*` — PASS
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — PASS (`148/148`)
- Step 8 required two follow-up bugfixes: stale status text after Escape, then redundant recursive
  result publishes during long walks.

### What remains unverified
- Live-host confirmation that recursive result rendering no longer surfaces a transient
  `Not Responding` state remains deferred to `SMI5`.
- Actual live-host double-click activation of a recursive result remains deferred to `SMI5`.

### Blockers
None

### Fix: recursive search results could freeze the window while rendering (2026-03-29)
Reported by: User report / manual testing
Root cause: the recursive-search results callback synchronously called
`m_shellInterop.GetItemIcon(...)` for every visible result on the UI thread, unlike the existing
directory-load path that chunked icon hydration in the background.
Fix: `native/FileScout.Native/MainWindow.xaml.cpp` now publishes recursive rows first and uses
`BackfillSearchResultIconsAsync(...)` with a `search_view_generation` guard from
`native/FileScout.Native/MainWindow.xaml.h` so stale search batches cannot mutate current rows.
Tests added: none; there is no existing automated WinUI responsiveness harness for this host-level
freeze, so verification relies on existing search tests plus manual retest.
Validated: `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS;
`native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=SearchFilter*:*SearchPresentation*:*SearchService*` — PASS;
`native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — PASS (`147/147`)

### Fix: recursive search kept publishing unchanged result snapshots during long walks (2026-03-29)
Reported by: User report / manual testing
Root cause: `SearchService` flushed the current result heap every `50ms` even when no new matches
had been added, so `MainWindow` repeatedly cleared and rebuilt the same recursive result rows while
the filesystem walk continued.
Fix: `native/FileScout.Native/Search/SearchService.cpp` now tracks whether the heap changed since
the last publish and only posts result batches when it is dirty; `native/FileScout.Tests/tests/SearchServiceTests.cpp`
adds `SearchService.LongWalkWithoutNewMatchesDoesNotEmitRedundantEmptyBatches` to cover the
regression.
Tests added: `native/FileScout.Tests/tests/SearchServiceTests.cpp:SearchService.LongWalkWithoutNewMatchesDoesNotEmitRedundantEmptyBatches`
Validated: `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS;
`native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=SearchService*` — PASS (`9/9`);
`native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=SearchFilter*:*SearchPresentation*:*SearchService*` — PASS;
`native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — PASS (`148/148`)

### Fix: review follow-up preserved streamed recursive selection and cancels stale icon work earlier (2026-03-29)
Reported by: `codex-review-loop` follow-up findings after the second responsiveness fix
Root cause: each streamed recursive batch rebuilt the visible rows from scratch and wiped any live
selection, while stale background icon tasks still resolved shell icons until the final
UI-generation check discarded their writes.
Fix: `native/FileScout.Native/MainWindow.xaml.cpp` now restores recursive selection against
`DisplayedItems(...)` after each streamed batch so matching selections survive incremental updates,
and `BackfillSearchResultIconsAsync(...)` now receives a per-batch cancellation token from
`native/FileScout.Native/MainWindow.xaml.h` so superseded icon backfills stop before doing more
shell work. `native/FileScout.Native/Domain/PaneSelectionState.h` exposes the focused item id so
the restored ListView selection keeps a stable primary item across batches.
Tests added: none; this remains host-level WinUI behavior without an existing automated selection or
message-pump harness.
Validated: `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS;
`native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — PASS (`148/148`)

### Next recommended step
Run `SMI5` closeout in the live WinUI host, confirm filter-mode typing, recursive-result
presentation, cancellation/reset behavior, and perform a final recursive-result activation smoke.

## Slice SMI5: Manual acceptance and initiative closeout
Status: Complete
Last updated: 2026-04-05

### What was implemented
- Built a local acceptance fixture under `C:\Users\machi\codex-search-acceptance` with nested
  `needle` matches and extra noise directories to make recursive search non-trivial.
- Fixed the two follow-up review findings in the host path so streamed recursive batches now retain
  any still-visible selection and stale icon backfills stop once a newer recursive batch replaces
  them.

### What was validated
- `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=SearchFilter*:*SearchPresentation*:*SearchService*` — PASS
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — PASS (`148/148`)
- Live host: filter-mode typing accepted via search box.
- Live host: recursive search from `C:\Users\machi` with query `needle` rendered helper-shaped subtitles and remained responsive — no `Not Responding` state.
- Live host: Recursive completion displayed capped-results helper text.
- Live host: Escape/reset restored home list, editable search box, and correct status bar text.
- Live host: double-click on recursive result opened parent folder and reselected the matching file — navigate-on-select confirmed.

### What remains unverified
- Nothing. All acceptance criteria confirmed.

### Blockers
None
