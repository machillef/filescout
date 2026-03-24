# Status: phase3-perf-ux

## Initiative State

- Active track: `native/` C++20 + C++/WinRT + WinUI 3
- Phase: phase3-perf-ux — In Progress
- Current blocker: None
- Next slice: P4 — Async icon backfill via ViewModel INPC
- Tests at initiative start: 82/82 passing
- Tests now: 93/93 passing (82 existing + 8 RowFormatting + 3 from other tracks)

## Slice Status

| Slice | Status | Last Updated |
|---|---|---|
| P1 | Complete | 2026-03-24 |
| P2 | Complete | 2026-03-24 |

### P1 — Complete (2026-03-24)

**What was done:**
- Created `Domain/RowFormatting.h` + `.cpp` — pure C++ helpers (`FormatFileSize`, `BuildDetailText`)
- Created `Domain/FileRowViewModel.idl` — WinRT IDL for `FileRowViewModel : INotifyPropertyChanged`
- Created `Domain/FileRowViewModel.h` + `.cpp` — C++/WinRT implementation with INPC, internal setters, `MakeFileRowViewModel` factory
- Added all new files to `FileScout.Native.vcxproj`
- Added `RowFormatting.cpp` + `FileRowViewModelTests.cpp` to `FileScout.Tests.vcxproj`
- Wrote 8 tests for `RowFormatting` (FormatFileSize × 6 + BuildDetailText × 2)

**Validation:** 90/90 tests pass (8 new RowFormatting tests all GREEN)

**Key learnings:**
- `FileRowViewModel.g.cpp` (IDL-generated factory shim) must be `#include`d from `FileRowViewModel.cpp` via `#if __has_include(...)` to resolve `winrt_make_*` symbol
- Build command: use `powershell -Command "cmd /c '...'"` not `cmd /c "..."` from bash (see CLAUDE.md)

**What remains unverified:**
- `FileRowViewModel` WinRT class not directly tested (test project can't instantiate WinRT classes without full WinRT runtime); verified by clean compilation + factory registration in module.g.cpp

### P2 — Complete (2026-03-24)

**What was done:**
- Added `CardRowTemplate` and `ListRowTemplate` `DataTemplate` resources to `Grid.Resources` in `MainWindow.xaml`
- Both templates use `x:DataType="local:FileRowViewModel"` + `x:Bind` for compile-time binding
- `CardRowTemplate`: icon tile (36×36, neutral background), name (SemiBold/14px), DetailText (opacity 0.72), DateText (opacity 0.55 right-aligned)
- `ListRowTemplate`: 20px icon + 5 columns (3*/1.8*/1.2*/2*) for Name/TypeLabel/SizeText/DateText
- `IconSource` bound with `Mode=OneWay` so INPC update triggers Image refresh in P4
- No C++ changes

**Validation:** Build clean (no new errors/warnings). Tests: 93/93 pass.

**Key decisions:**
- `IsFolder`-conditional icon tile background (AccentFillColorSecondaryBrush) deferred to P3: requires an IValueConverter C++ class — not worth adding in this no-C++ slice
- ItemTemplate assignment on ListView deferred to P3 (ListView is programmatic; needs C++ `CreateTabListView` change)

**What remains unverified:**
- Visual rendering at runtime (requires P3 to change ItemsSource to FileRowViewModel vector)

### P3 — Complete (2026-03-24)

**What was done:**
- Deleted `CreateCardRow`, `CreateListRow`, `CreateRowView` from anonymous namespace (~170 lines removed)
- Changed `tab_rows` from `IObservableVector<IInspectable>` to `IObservableVector<FileRowViewModel>` in `PaneContext`
- Changed `single_threaded_observable_vector` instantiation to `FileRowViewModel` type in both `AddTab` and `InitPane`
- Rewrote `BackfillIconsAsync`: drops `listMode` param, calls `rows.GetAt(i).IconSource(source)` via INPC (no SetAt/CreateRowView)
- Simplified `UpdateRows` and `RebuildAllTabRowsForPane` to use `MakeFileRowViewModel` directly
- Rewrote `CreateSearchRowView`: returns `FileRowViewModel` (Name=filename, TypeLabel/DetailText=relative path subtitle)
- `CreateTabListView`: looks up `CardRowTemplate`/`ListRowTemplate` from XAML resources and assigns `ItemTemplate`
- `SetViewMode`: updates `active_list_view.ItemTemplate()` on view mode change
- Group headers disabled (flat list) — `group_by_sort_column` bool + `BuildGroupLabel` retained for toggle state; visual grouping deferred

**Validation:** Build clean. 93/93 tests pass. Net diff: 62 insertions / 321 deletions.

**Key decisions:**
- Group header rows (WrapRowWithGroupHeader StackPanel wrappers) removed because mixed-type vector breaks x:Bind; visual grouping deferred to future slice
- `CreateSearchRowView` stores relative path in both `TypeLabel` and `DetailText` for consistency with list/card templates

**What remains unverified:**
- Visual rendering at runtime (requires app launch with real directory)
- Group-by visual behaviour disabled; restore path not yet planned

| P3 | Complete | 2026-03-24 |
| P4 | Not Started | — |
| U1 | Not Started | — |
| U2 | Not Started | — |
| U3 | Not Started | — |
| U4 | Not Started | — |
| U5 | Not Started | — |
| U6 | Not Started | — |
| U7 | Not Started | — |
| U8 | Not Started | — |
| S1 | Not Started | — |
| S2 | Not Started | — |
| S3 | Not Started | — |
| S4 | Not Started | — |
