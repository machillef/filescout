# Slices: phase3-perf-ux

## P1: FileRowViewModel — IDL + Implementation

**Goal:** Create `FileRowViewModel` C++/WinRT runtime class with `INotifyPropertyChanged`.

**Touched area:**
- `native/FileScout.Native/Domain/FileRowViewModel.idl` (new)
- `native/FileScout.Native/Domain/FileRowViewModel.h` (new)
- `native/FileScout.Native/Domain/FileRowViewModel.cpp` (new)
- `native/FileScout.Native/FileScout.Native.vcxproj` (add IDL + sources)
- `native/FileScout.Tests/FileRowViewModelTests.cpp` (new)

**TDD required:** Yes — tests before implementation.

**Done criteria:**
- `FileRowViewModel` compiles as WinRT runtime class
- Properties: `Name`, `TypeLabel`, `DisplaySize`, `DisplayDate`, `IsFolder`, `IconSource`
- Setting `IconSource` fires `PropertyChanged` with `L"IconSource"`
- `DisplaySize` formats bytes as human-readable (KB/MB/GB)
- 5+ unit tests RED → GREEN; full test suite passes

**What remains unverified:** Runtime XAML binding (verified in P2).

**Validation:** `native\bin\x64\Debug\Tests\FileScout.Tests.exe`

---

## P2: XAML DataTemplates — CardRow + ListRow

**Goal:** Define `CardRowTemplate` and `ListRowTemplate` DataTemplates in a ResourceDictionary
using `x:DataType="local:FileRowViewModel"` + `x:Bind`. ListView `ItemTemplate` set to these.

**Touched area:**
- `native/FileScout.Native/MainWindow.xaml` — add ResourceDictionary + templates, set `ItemTemplate`
  on both ListView instances
- No C++ changes in this slice

**TDD required:** No — pure XAML, no testable logic.

**Done criteria:**
- Both binaries build clean
- DataTemplates compile without `x:Bind` code-gen errors
- XAML designer shows template structure

**What remains unverified:** Visual fidelity (requires manual host launch with real ViewModel data
piped in — verified as part of P3 manual validation).

**Validation:** Build only (`FileScoutNative.exe` and `FileScout.Tests.exe` build clean).

---

## P3: Wire ListView to ViewModels (HIGH RISK)

**Goal:** Replace `ObservableVector<IInspectable>` (containing pre-built XAML objects) with
`ObservableVector<FileRowViewModel>`. Remove `CreateCardRow`, `CreateListRow`, `CreateRowView`.
Update all callers: `UpdateRows`, `RebuildAllTabRowsForPane`, `BackfillIconsAsync`,
`CreateSearchRowView` path, group header path.

**Touched area:**
- `native/FileScout.Native/MainWindow.xaml.h` — `tab_rows` type change
- `native/FileScout.Native/MainWindow.xaml.cpp` — ~500 lines of row-creation code removed/replaced
- May need to split into P3a + P3b if scope is too large during execution

**TDD required:** No new test logic — regression test (existing 82 tests must still pass).

**Done criteria:**
- `CreateCardRow`, `CreateListRow`, `CreateRowView` deleted
- `tab_rows` holds `FileRowViewModel` instances
- Full 82+ test suite passes
- Both binaries build clean

**What remains unverified:** Visual correctness at runtime (requires manual host launch — navigate
to a directory, confirm rows display correctly in both card and list modes).

**Validation:** Full build + `FileScout.Tests.exe`

**Rollback note:** P3 is the highest-risk slice. If the change causes build failures > 3 attempts,
split into P3a (type change + UpdateRows) and P3b (remove old helpers + fix search/group paths).

---

## P4: Icon Backfill via ViewModel PropertyChanged

**Goal:** Update `BackfillIconsAsync` to retrieve `FileRowViewModel` from `tab_rows[i]` and call
`vm.IconSource(bitmapSource)` instead of `rows.SetAt(i, CreateRowView(...))`.

**Touched area:**
- `native/FileScout.Native/MainWindow.xaml.cpp` — `BackfillIconsAsync` rewrite

**TDD required:** No — behavioral change is UI-only (icon updates in-place).

**Done criteria:**
- `BackfillIconsAsync` no longer calls `CreateRowView`
- Setting `IconSource` on ViewModel triggers Image binding refresh (no row recreation)
- Both binaries build clean; test suite passes

**What remains unverified:** Icons actually update in-place without scroll reset (manual host launch
→ navigate large dir → icons should refresh without visual jump).

**Validation:** Build + `FileScout.Tests.exe`

---

## U1: Breadcrumb Sizing

**Goal:** Increase breadcrumb button font and height for readability.

**Touched area:**
- `native/FileScout.Native/MainWindow.xaml` — `HyperlinkButton` `FontSize`, `MinHeight`, `Padding`

**Changes:** `FontSize` 12→14, `MinHeight` 38→44, `Padding="{4,0,4,0}"` → `"{6,0,6,0}"`

**TDD required:** No — pure XAML visual change.

**Done criteria:** Build clean. Manual validation: breadcrumbs visually larger and readable.

**Validation:** Build + manual host launch.

---

## U2: Double-Click to Open Folders/Files

**Goal:** `FileRowsListView_DoubleTapped` navigates into selected folder or activates selected file.

**Touched area:**
- `native/FileScout.Native/MainWindow.xaml.cpp` — `FileRowsListView_DoubleTapped` implementation

**TDD required:** No — UI event handler, no unit-testable logic.

**Done criteria:** Build clean. Manual: double-clicking a folder navigates into it.

**Validation:** Build + manual host launch.

---

## U3: Delete Key Shortcut

**Goal:** Pressing Delete in the file list triggers the delete confirmation dialog.

**Touched area:**
- `native/FileScout.Native/MainWindow.xaml.cpp` — `FileRowsListView_KeyDown`: add
  `VirtualKey::Delete` case → `ExecuteContextCommand(L"delete", ...)`

**TDD required:** No — keyboard event handler.

**Done criteria:** Build clean. Manual: press Delete on selected file → dialog appears.

**Validation:** Build + manual host launch.

---

## U4: Ctrl+T = New Tab

**Goal:** Reassign Ctrl+T from theme-toggle to `addTab` command.

**Touched area:**
- `native/FileScout.Native/MainWindow.xaml.cpp` — `SetupKeyboardAccelerators()` or
  the Ctrl+T keyboard accelerator binding

**TDD required:** No.

**Done criteria:** Ctrl+T opens a new tab; theme toggle moved to another shortcut or command palette only.

**Validation:** Build + manual host launch.

---

## U5: Ctrl+L — Focus Address Bar

**Goal:** Ctrl+L focuses the address text box (same as Windows Explorer).

**Touched area:**
- `native/FileScout.Native/MainWindow.xaml.cpp` — add Ctrl+L accelerator → `ShowAddressTextBox()`
  + focus the `AddressTextBox`

**TDD required:** No.

**Done criteria:** Build clean. Manual: Ctrl+L shows address bar and keyboard focus is in it.

**Validation:** Build + manual host launch.

---

## U6: Context Menu Right-Click Scroll Fix

**Goal:** Right-clicking a file should not cause the list to scroll to the top.

**Investigation required:** Determine if this is still reproducible after S2's `tab_switching` fix.
If already fixed, close this slice as no-op with a note. If still present, add `tab_switching` guard
around the `SelectedIndex` change triggered by right-click selection.

**Touched area:**
- `native/FileScout.Native/MainWindow.xaml.cpp` — `FileRowsListView_RightTapped` or
  `FileRowsListView_SelectionChanged`

**TDD required:** No.

**Done criteria:** Right-click on a file mid-list does not scroll the list to the top.

**Validation:** Build + manual host launch.

---

## U7: Column Width Fix (Ext + Type Overlap)

**Goal:** Ext and Type columns in list-mode do not overlap.

**Touched area:**
- `native/FileScout.Native/MainWindow.xaml` — list-mode `ColumnDefinition` explicit widths
- or `CreateListRow` → after P3, update `ListRowTemplate`

**TDD required:** No — XAML layout change.

**Done criteria:** Build clean. Manual: list mode shows Ext and Type in separate readable columns.

**Validation:** Build + manual host launch in list mode.

---

## U8: Arrow Key Navigation

**Goal:** Up/Down arrow keys move selection in the file list.

**Investigation required:** WinUI 3 `ListView` handles Up/Down natively. If `FileRowsListView_KeyDown`
(added in R18c for type-ahead) is consuming arrow key events before ListView handles them, add a
passthrough: only handle `Key >= A && Key <= Z` in our handler, let arrows fall through.

**Touched area:**
- `native/FileScout.Native/MainWindow.xaml.cpp` — `FileRowsListView_KeyDown` — add early return
  for arrow keys

**TDD required:** No.

**Done criteria:** Arrow keys move selection without triggering type-ahead.

**Validation:** Build + manual host launch.

---

## S1: Sidebar Structural Redesign

**Goal:** Replace flat sidebar groups with `Expander`-based collapsible sections (Quick Access,
This PC, Bookmarks). No data wired yet — just the structural XAML.

**Touched area:**
- `native/FileScout.Native/MainWindow.xaml` — sidebar `ScrollViewer` contents
- `native/FileScout.Native/MainWindow.xaml.cpp` — remove `LoadDrivesView()` call from sidebar init
  (to be replaced in S3)

**TDD required:** No — pure XAML structure.

**Done criteria:** Build clean; three collapsed `Expander` sections visible in sidebar on launch.

**Validation:** Build + manual host launch.

---

## S2: Quick Access Section

**Goal:** Quick Access section shows Desktop, Downloads, and last 5 navigated paths.

**Touched area:**
- `native/FileScout.Native/MainWindow.xaml.cpp` — `BuildSidebarFavorites()` or new
  `UpdateQuickAccessSection()` function
- Source for "recent paths": `m_panes[0].back_stack` (last 5 unique paths)

**TDD required:** Yes — `UpdateQuickAccess` logic for deduplication and limiting to 5.

**Done criteria:** Build clean; tests pass; Quick Access shows Desktop, Downloads, plus up to 5
recently navigated paths. Clicking a path navigates the active pane.

**Validation:** Build + `FileScout.Tests.exe` + manual host launch.

---

## S3: This PC — Drives + Capacity Bars

**Goal:** This PC section shows all local drives with their free/total capacity as a `ProgressBar`.

**Touched area:**
- `native/FileScout.Native/MainWindow.xaml.cpp` — `LoadDrivesView()` repurposed or new
  `UpdateThisPCSection()` using `GetLogicalDriveStringsW` + `GetDiskFreeSpaceExW`
- `native/FileScout.Native/MainWindow.xaml` — drive row template with `ProgressBar`

**TDD required:** Yes — capacity percentage calculation.

**Done criteria:** Build clean; drives listed with capacity bars; clicking drive navigates to it.

**Validation:** Build + `FileScout.Tests.exe` + manual host launch.

---

## S4: Bookmarks Section

**Goal:** Bookmarks section shows user-pinned folders. User can pin current folder and unpin entries.

**Touched area:**
- `native/FileScout.Native/Shell/FavoritesService.h/.cpp` — add `Pin(path)` / `Unpin(path)` / `GetAll()`
- `native/FileScout.Native/MainWindow.xaml` — Bookmarks row template with unpin button
- `native/FileScout.Native/MainWindow.xaml.cpp` — pin command wired in command palette or context menu

**TDD required:** Yes — `FavoritesService` pin/unpin/getall.

**Done criteria:** User can pin current folder via command palette "Pin to bookmarks", see it in
sidebar Bookmarks section, click to navigate, and unpin via × button.

**Validation:** Build + `FileScout.Tests.exe` + manual host launch.
