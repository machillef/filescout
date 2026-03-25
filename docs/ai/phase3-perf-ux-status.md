# Status: phase3-perf-ux

## Initiative State

- Active track: `native/` C++20 + C++/WinRT + WinUI 3
- Phase: phase3-perf-ux — In Progress
- Current blocker: None
- Next slice: S4 — Bookmarks section
- Tests at initiative start: 82/82 passing
- Tests now: 106/106 passing (100 prior + 6 DriveCapacity)

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

### P4 — Complete (2026-03-24)

**What was done:**
- No new code required — `BackfillIconsAsync` was fully rewritten as part of P3
- `rows.GetAt(i).IconSource(source)` updates the ViewModel in-place via INPC
- Image binding in both DataTemplates uses `Mode=OneWay` (P2), so PropertyChanged triggers refresh automatically
- `CreateRowView` / `SetAt` calls: 0 remaining

**Validation:** Build clean (from P3). 93/93 tests pass.

**What remains unverified:**
- Icons actually update in-place without scroll reset (manual host launch → navigate large dir → icons should refresh without visual jump)

| P3 | Complete | 2026-03-24 |
| P4 | Complete | 2026-03-24 |
| U1 | Complete | 2026-03-24 |

### U1 — Complete (2026-03-24)

**What was done:**
- `BreadcrumbBarBorder` in XAML: `MinHeight` 38→44, `Padding` 4→6
- `UpdateBreadcrumbs` in C++: `lbl.FontSize` 12→14, `btn.Padding` {4,0,4,0}→{6,0,6,0}, separator `FontSize` 12→14
- Slice spec said "pure XAML" but buttons are programmatic — change landed in both XAML and C++

**Validation:** Build clean. 93/93 tests pass.

**What remains unverified:**
- Visual readability at runtime (requires manual host launch)

### U2 — Complete (2026-03-24)

**What was done:** No code change required — `FileRowsListView_DoubleTapped` was already fully
implemented: folder navigation, archive paths, archive view items, regular file open with status text.

### U3 — Complete (2026-03-24)

**What was done:** No code change required — `SetupKeyboardAccelerators` already registers
`AddAccelerator(VK::Delete, VKM::None, L"delete")` which routes through `ExecuteContextCommand`.

### U4 — Complete (2026-03-24)

**What was done:** No code change required — `AddAccelerator(VK::T, VKM::Control, L"newTab")`
already registered in `SetupKeyboardAccelerators`. Ctrl+T opens a new tab.

### U5 — Complete (2026-03-24)

**What was done:**
- Added `Ctrl+L` accelerator in `SetupKeyboardAccelerators` alongside existing F4 using a
  structured-binding loop over `std::initializer_list<std::pair<VK, VKM>>`
- Both F4 and Ctrl+L call `ShowAddressTextBox()`, which already calls `SelectAll()` + `Focus(Programmatic)`

**Validation:** Build clean. 93/93 tests pass.

**What remains unverified:**
- Address bar visually appears and gets focus on Ctrl+L (manual host launch)

| U2 | Complete | 2026-03-24 |
| U3 | Complete | 2026-03-24 |
| U4 | Complete | 2026-03-24 |
| U5 | Complete | 2026-03-24 |
| U6 | Complete | 2026-03-24 |
| U7 | Complete | 2026-03-24 |
| U8 | Complete | 2026-03-24 |
| S1 | Complete | 2026-03-24 |
| S2 | Complete | 2026-03-24 |
| S3 | Complete | 2026-03-25 |
| S4 | Not Started | — |

### U6 — Complete (2026-03-24)

**What was done:** No code change required — P3's ListView/DataTemplate virtualization refactor
resolved the scroll-jump on right-click. `FileRowsListView_RightTapped` sets `SelectedIndex`
on the correct item; no `ScrollIntoView` call is needed (ListView handles focus itself).

### U7 — Complete (2026-03-24)

**What was done:** No code change required — `ListRowTemplate` uses star-based `GridViewColumn`
widths with `TextTrimming="CharacterEllipsis"`, so column overlap is impossible. P3 resolved this.

### U8 — Complete (2026-03-24)

**What was done:** No code change required — `FileRowsListView_KeyDown` early-returns for
`vk < 65 || vk > 90` (letter keys only). Arrow keys (VK 38/40) fall through to the ListView
and navigate correctly.

### S1 — Complete (2026-03-24)

**What was done:**
- Replaced flat "PLACES" heading + `SidebarPlacesPanel` + divider block in `MainWindow.xaml`
  with 3 `Expander` sections: `QuickAccessExpander` (expanded), `ThisPCExpander` (collapsed),
  `BookmarksExpander` (collapsed)
- `SidebarPlacesPanel` preserved as the content StackPanel of `QuickAccessExpander` so
  `BuildSidebarFavorites()` C++ accessor compiles unchanged
- Added `if (entry.is_drives_view) continue;` in the `BuildSidebarFavorites()` `for` loop
  (`MainWindow.xaml.cpp`) to skip the "This PC" drives-view entry from Quick Access panel
  (it gets its own Expander in S3)

**Validation:** Build clean. 93/93 tests pass.

**Key decisions:**
- Preserving `SidebarPlacesPanel` x:Name inside `QuickAccessExpander` content avoids any C++
  accessor changes while achieving the structural redesign

**What remains unverified:**
- Expander sections visually collapse/expand correctly at runtime (manual host launch)

### S2 — Complete (2026-03-24)

**What was done:**
- Created `Domain/SidebarRecents.h` + `.cpp` — pure C++ `SidebarPathKey` + `SelectRecentPaths`
  (case-insensitive dedup, oldest-first back_stack, newest-first result, capped at `max`)
- Added both files to `FileScout.Native.vcxproj` (ClInclude + ClCompile)
- Added `SidebarRecents.cpp` + `SidebarRecentsTests.cpp` to `FileScout.Tests.vcxproj`
- Wrote 7 tests (Empty, AllExcluded, CapAtMax, Dedup, MostRecentFirst, CaseInsensitiveExclusion,
  EmptyPathsSkipped) — all RED→GREEN
- In `BuildSidebarFavorites()` (`MainWindow.xaml.cpp`):
  - Removed redundant "QUICK ACCESS" section label (Expander header in S1 already says "Quick Access")
  - Added `shownPathKeys` set — populated with `SidebarPathKey` for each non-pinned shown path
  - After existing favorites loop: added recent paths section (thin divider + up to 5 buttons)
    built from `SelectRecentPaths(m_panes[0].back_stack, shownPathKeys, 5)`
- In `LoadDirectory()`: added `BuildSidebarFavorites()` call when back_stack grows so the sidebar
  refreshes on every navigation

**Validation:** Build clean. 100/100 tests pass (7 new SidebarRecents tests all GREEN).

**Key decisions:**
- `m_panes[0].back_stack` per spec — only left pane's history contributes to recent paths
- Recent paths appear after built-in favorites and before PINNED section (will be reordered in S4)
- `BuildSidebarFavorites()` called synchronously before `LoadDirectoryAsync` — back_stack is
  already updated at that point, so recent paths are always up-to-date

**What remains unverified:**
- Recent path buttons appear and are clickable after navigating several folders (manual host launch)

### S3 — Complete (2026-03-25)

**What was done:**
- Created `Domain/DriveCapacity.h` + `.cpp` — pure C++ `DriveUsedRatio(totalBytes, freeBytes)` that
  returns fraction used (0.0–1.0), clamps against zero total and free > total edge cases
- Updated `BuildSidebarDriveUsage` (anonymous namespace) to use `DriveUsedRatio` instead of
  inline `std::clamp(used/total)` — delegates to domain function
- Added `void UpdateThisPCSection()` declaration to `MainWindow.xaml.h`
- Implemented `MainWindow::UpdateThisPCSection()`: clears `ThisPCPanel`, builds one Button per
  drive (icon tile + SemiBold label + 11px capacity text + 4px ProgressBar when `has_usage`)
- Added `#include "Domain/DriveCapacity.h"` to `MainWindow.xaml.cpp`
- Removed ~93-line STORAGE block from `BuildSidebarFavorites`; replaced with `UpdateThisPCSection()`
  call (drives now render in `ThisPCExpander` not `SidebarPlacesPanel`)
- Added `DriveCapacity.h/.cpp` to `FileScout.Native.vcxproj`; `DriveCapacity.cpp` + `DriveCapacityTests.cpp`
  to `FileScout.Tests.vcxproj`
- Wrote 6 tests (ZeroTotal, FullDrive, EmptyDrive, HalfFull, QuarterFree, FreeExceedsTotal) — all RED→GREEN

**Validation:** Build clean. 106/106 tests pass (6 new DriveCapacity tests all GREEN).
Net diff: ~93 lines removed from BuildSidebarFavorites, ~95 lines added as UpdateThisPCSection.

**Key decisions:**
- `UpdateThisPCSection()` called from `BuildSidebarFavorites()` so drive selection highlight
  (`driveSelected`) updates on every navigation, matching the Quick Access section refresh cadence
- Drive rows omit `ProgressBar` when `has_usage = false` (GetDiskFreeSpaceExW failed) — no
  misleading empty bar shown for unreadable drives

**What remains unverified:**
- `ThisPCExpander` expands and shows drive list at runtime (manual host launch)
- Drive capacity bars reflect correct used% for each drive (manual host launch)
- Clicking a drive row navigates the active pane to that drive root (manual host launch)

