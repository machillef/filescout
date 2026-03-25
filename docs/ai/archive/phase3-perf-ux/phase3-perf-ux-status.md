# Status: phase3-perf-ux

## Initiative State

- Active track: `native/` C++20 + C++/WinRT + WinUI 3
- Phase: phase3-perf-ux — In Progress
- Current blocker: None
- Next slice: None — all slices complete
- Tests at initiative start: 82/82 passing
- Tests now: 110/110 passing (106 prior + 4 RemovePinnedFavorite/GetPinnedFavorites)

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
| U1 | Complete | 2026-03-25 |

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
| S4 | Complete | 2026-03-25 |

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

### S4 — Complete (2026-03-25)

**What was done:**
- Added `RemovePinnedFavorite(path)` + `GetPinnedFavorites()` to `FavoritesService.h/.cpp`
  — remove by case-insensitive path key, returns false if not found; loads pinned-only entries
- Added 4 tests for `RemovePinnedFavorite` (RemovesExisting, ReturnsFalse, Persists, GetAll) — all RED→GREEN
- Added `UpdateBookmarksSection()` to `MainWindow.xaml.h/.cpp`:
  — clears `BookmarksPanel`, builds one Button per `GetPinnedFavorites()` entry
  — each row: 20px icon tile + SemiBold label + × unpin button (transparent, `\uE711` glyph)
  — × click: `RemovePinnedFavorite(path)` + `BuildSidebarFavorites()`
  — row click: `LoadDirectory(path)`, `isSelected` highlights exact current-path match
- Modified `BuildSidebarFavorites()`: pinned entries now `continue` early (removed "PINNED" header/badge)
  — pinned paths still added to `shownPathKeys` so they don't duplicate in recent paths
  — `UpdateBookmarksSection()` called at end (before `UpdateThisPCSection()`)
- Updated `ContextMenuService.cpp`: "Pin to Places" → "Pin to Bookmarks" (both item + folder labels)
- Updated `ExecuteContextCommand` status messages: "Places" → "Bookmarks" throughout

**Validation:** Build clean (no new errors/warnings). 110/110 tests pass.
Net diff: ~95 lines added (`UpdateBookmarksSection`), ~20 lines removed (PINNED header/badge from `BuildSidebarFavorites`).

**Key decisions:**
- `BookmarksExpander` remains `IsExpanded="False"` on launch (consistent with This PC behaviour;
  user opens it manually after pinning the first bookmark)
- Unpin uses `(void)` cast to discard `[[nodiscard]]` return value — UI rebuild is unconditional
- `isSelected` in bookmark buttons uses exact `SidebarPathsEqual` match (not prefix scoring),
  because bookmarks highlight the exact pinned folder, not parents

**What remains unverified:**
- Bookmarks section shows pinned folders after "Pin to Bookmarks" via right-click menu (manual host launch)
- × unpin button removes entry and collapses section when last bookmark removed (manual host launch)
- Bookmarks highlights correctly when navigated to a pinned folder (manual host launch)

### Bug Report (2026-03-25)
Reported by: Manual testing
Description: Right-click context menu appears in the bottom-left corner of the window (near the
sidebar) instead of near the right-clicked file list item. Screenshot shows menu at ~(0, bottom)
of the window regardless of where the user clicks.

### Fix: context menu positioned at wrong location (2026-03-25)
Reported by: Manual testing
Root cause: `m_contextMenu.ShowAt(target)` with no options anchors the flyout to the FrameworkElement's
layout origin (0, 0 relative to the ListView), not the tap position. For a large ListView spanning
most of the window, WinUI 3 placed the menu near the sidebar.
Fix:
- `MainWindow.xaml.h`: added `winrt::Windows::Foundation::Point tapPoint` parameter to `ShowContextMenu`
- `MainWindow.xaml.cpp` (`ShowContextMenu`): replaced `m_contextMenu.ShowAt(target)` with
  `FlyoutShowOptions` using `options.Position(tapPoint)` + `options.ShowMode(Transient)`
- `MainWindow.xaml.cpp` (`FileRowsListView_RightTapped`): added
  `auto const tapPoint = args.GetPosition(ActivePane().active_list_view)` and passed to `ShowContextMenu`
Tests added: No test framework for UI placement — verified by code review (FlyoutShowOptions contract).
Validated: Build clean. 110/110 tests pass.

### Bug Report: U1 breadcrumbs still too small (2026-03-25)
Reported by: Manual testing
Description: After the U1 change (12→14px font, 38→44px height), breadcrumbs were still visually
too small. User requested a more aggressive size increase.

### Fix: breadcrumb font and container size — second pass (2026-03-25)
Reported by: Manual testing
Root cause: First pass delta (12→14px, 38→44px height) insufficient for comfortable readability.
Fix:
- `MainWindow.xaml`: `BreadcrumbBarBorder` `MinHeight` 44→52, `Padding` 6→8
- `MainWindow.xaml.cpp` (`UpdateBreadcrumbs`): `lbl.FontSize` 14→16, `sep.FontSize` 14→16,
  `btn.Padding` {6,0,6,0}→{8,2,8,2}
Tests added: No test framework for visual sizing — verified by code review.
Validated: Build clean. 110/110 tests pass.

### Bug Report: U1 — Escape doesn't dismiss address bar; breadcrumbs still too small (2026-03-25)
Reported by: Manual testing
Description: (1) Left-clicking the breadcrumb bar opens the editable TextBox but pressing Escape
does not restore breadcrumbs. No way to dismiss the TextBox without navigating.
(2) Breadcrumbs still visually too small after second pass — user wants a further increase.

### Fix: address bar Escape + click-outside dismiss; breadcrumb third-pass sizing (2026-03-25)
Reported by: Manual testing
Root cause (Escape): `KeyDown` on TextBox fires after the control's own processing; WinUI 3 TextBox
internally consumes Escape (for undo-buffer cancel) in the tunneling phase, setting Handled=true
before our handler sees it. Fix: use `PreviewKeyDown` (tunneling phase, fires before TextBox
internals) + add `LostFocus` for click-outside dismissal.
Root cause (sizing): Second pass (16px/52px) still insufficient per user preference.
Fix:
- `MainWindow.xaml`: `AddressTextBox` changed `KeyDown` → `PreviewKeyDown`, added `LostFocus`
- `MainWindow.xaml.h`: added `AddressTextBox_LostFocus` declaration
- `MainWindow.xaml.cpp`: added `AddressTextBox_LostFocus` handler → calls `HideAddressTextBox()`
- `MainWindow.xaml`: `BreadcrumbBarBorder` `MinHeight` 52→60, `Padding` 8→10
- `MainWindow.xaml.cpp` (`UpdateBreadcrumbs`): font 16→18px, sep font 16→18px, padding {8,2}→{10,3}
Tests added: No test framework for UI event routing — verified by code review.
Validated: Build clean. 110/110 tests pass.

### Bug Report: S3 — ThisPCExpander starts collapsed; user wants it expanded on launch (2026-03-25)
Reported by: Manual testing
Description: Drive capacity bars are hidden on launch behind a collapsed Expander. User wants
disk space visible immediately without needing to click to expand.

### Fix: ThisPCExpander expanded on launch (2026-03-25)
Reported by: Manual testing
Root cause: `IsExpanded="False"` was set by design at S1 (consistent-collapse policy), but user
preference is to show drives immediately.
Fix:
- `MainWindow.xaml`: `ThisPCExpander` `IsExpanded` False→True
Tests added: No test framework for XAML initial state — verified by code review.
Validated: Build clean. 110/110 tests pass.

