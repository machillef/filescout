# Scope Map: large-dir-perf

## Problem Statement

After phase3-perf-ux (P1–P4, archived), the DataTemplate + FileRowViewModel INPC architecture
replaced pre-built XAML objects and enabled UI virtualization. However, System32 (~5120 items)
still exhibits visible stutter on navigate because two hot-path operations run entirely on the
UI thread in single large dispatches:

1. **Final batch publish** — `LoadDirectoryAsync` posts all remaining rows (4970+) as a single
   `TryEnqueue` lambda. This lambda calls `rows.Append(MakeFileRowViewModel(item))` 4970 times
   before returning, blocking the UI thread for the duration.

2. **BackfillIconsAsync** — After both row batches appear, `BackfillIconsAsync` posts one
   `TryEnqueue` lambda that calls `BuildIconSource(item.icon)` (creates a `WriteableBitmap`) and
   then `IconSource(source)` (fires INPC) for every single row — potentially 5120 times.
   `BuildIconSource` is expensive: it allocates a `WriteableBitmap` and writes BGRA pixels.
   Since most rows share extensions (≤30 unique in System32), ~5090 of these WriteableBitmap
   allocations are redundant — the same pixel data recreated for every row with the same extension.

3. **Sort on UI thread** — `SortItems` is called inside the dispatch lambda (on the UI thread)
   rather than on the background thread where enumeration already runs.

## In-Scope

| File | Changes |
|---|---|
| `native/FileScout.Native/MainWindow.xaml.cpp` | `LoadDirectoryAsync` (chunked publish), `BackfillIconsAsync` (ImageSource cache), `SortItems` call site |
| `native/FileScout.Native/MainWindow.xaml.h` | `PaneContext` (add ImageSource cache if local) |
| `native/FileScout.Tests/tests/` | New benchmark/regression tests for large-dir enumeration |

## Adjacent (Understand, Don't Touch)

| Area | Contract |
|---|---|
| `Domain/FileRowViewModel.{h,cpp}` | `MakeFileRowViewModel` factory — fast, no changes needed |
| `Shell/ShellInteropService.{h,cpp}` | `GetIconForItem` thread-safe via `m_cacheMutex` — interface unchanged |
| `PaneLoadTiming` struct | Timing fields used for measurement — structure unchanged |
| `BackfillIconsAsync` → `BackfillIconsAsync` | Generation guard + tab-index guard — both must be preserved |

## Out of Scope

- Archive enumeration (`EnumerateArchiveDirectory`) — separate code path, not a large-dir scenario
- Search pipeline (`SearchService`, filter mode) — separate from `LoadDirectoryAsync`
- Sidebar, favorites, clipboard, context menu
- Any UX layout changes
- macOS / Linux

## Dependents on Target Area

| Dependent | Risk |
|---|---|
| All navigation (Back, Forward, Refresh, tab switch) | Calls `LoadDirectory` → `LoadDirectoryAsync` — must all still work |
| `RebuildAllTabRowsForPane` | Calls `UpdateRows` directly (sort/view-mode change) — `UpdateRows` unchanged |
| Group-by mode | `BackfillIconsAsync` already guards `group_by_sort_column` — preserve this guard |
| `user_interacted_after_first_batch` flag | Prevents final-batch from overwriting live selection — must survive restructure |

## Definition of Done

1. System32 (~5120 items): `final_publish_tick − load_start_tick < 1000 ms` on a warm navigate
   (current baseline: ~2000 ms from phase3 retro log)
2. `BackfillIconsAsync` completes without visible frame drops after the final batch appears —
   no more than 30 unique `WriteableBitmap` objects created per navigate (one per unique extension)
3. All existing tests pass — zero regressions against current 90+ test count
4. Sort, group-by, view-mode toggle, tab switching, Back/Forward still work correctly

## Verification Commands

```powershell
# Build (Debug x64)
powershell -Command "cmd /c 'call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal'"

# Run tests
native/bin/x64/Debug/Tests/FileScout.Tests.exe
```
