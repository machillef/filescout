# Design: phase3-perf-ux

## Part 1 — Requirements

### Problem Statement

FileScout's file list is slow on large directories because rows are built programmatically:
`CreateCardRow()` creates 11 XAML objects per row, `CreateListRow()` creates 8. For System32
(5120 items) this produces ~50k XAML objects on the UI thread with no container recycling. Previous
optimizations (streaming enumeration, FindFirstFileW fast path, icon backfill) addressed enumeration
but not the XAML creation bottleneck.

Separately, the app has 8 critical UX regressions (double-click, Delete key, Ctrl+T conflict, etc.)
and a sidebar that lacks the collapsible Quick Access / This PC / Bookmarks structure users expect
from a modern Windows file manager.

### User Stories

1. As a user, I want large directories (System32, 5000+ files) to display near-instantly so that
   the app doesn't feel slow compared to Windows Explorer.
2. As a user, I want to double-click a folder to navigate into it, matching Windows Explorer behavior.
3. As a user, I want the Delete key to trigger the delete dialog on the selected item(s).
4. As a user, I want Ctrl+T to open a new tab, not toggle the theme.
5. As a user, I want Ctrl+L to focus the address bar so I can type a path quickly.
6. As a user, I want right-clicking a file to not cause the list to scroll to the top.
7. As a user, I want the Ext and Type columns to not overlap so I can read both values.
8. As a user, I want Up/Down arrow keys to move selection in the file list.
9. As a user, I want breadcrumbs to be large enough to read comfortably.
10. As a user, I want a Quick Access section in the sidebar with Desktop, Downloads, and recent paths.
11. As a user, I want a "This PC" section in the sidebar with my drives and their capacity.
12. As a user, I want a Bookmarks section where I can pin and unpin folders I navigate to often.

### Out of Scope

- Non-critical UX issues (autocomplete address bar, app icon, favorites sidebar rename)
- Thumbnail image previews
- Archive browsing improvements
- Sidebar tree-based navigation (drill-down like Windows Explorer's left panel)
- macOS/Linux

---

## Part 2 — Design

### Architecture

Three tracks in dependency order:

```
P1 (ViewModel) → P2 (Templates) → P3 (Wire) → P4 (Backfill)
                                                              ↓
U1–U8 (UX fixes — run after P-track stabilizes)
                                                              ↓
S1–S4 (Sidebar — runs last, depends on stable shell)
```

P-track is the highest-risk and highest-impact. All other tracks build on a stable rendering
pipeline.

### Components

**FileRowViewModel** (new — `Domain/FileRowViewModel.idl + .h + .cpp`)
- WinRT runtime class implementing `Microsoft.UI.Xaml.Data.INotifyPropertyChanged`
- Properties: `Name (String)`, `TypeLabel (String)`, `DisplaySize (String)`, `DisplayDate (String)`,
  `IsFolder (Boolean)`, `IconSource (BitmapSource get/set)`
- `IconSource` setter fires `PropertyChanged` → DataTemplate Image binding auto-refreshes
- Factory helpers: `static FileRowViewModel FromShellItem(ShellItemRef const&)`

**CardRowTemplate / ListRowTemplate** (in `MainWindow.xaml` or `FileRowDataTemplates.xaml`)
- Replace the 11-object / 8-object programmatic trees with XAML declarative equivalents
- Use `x:DataType="local:FileRowViewModel"` + `x:Bind` for compiled, zero-reflection bindings
- ListView `ItemTemplate="{StaticResource CardRowTemplate}"` enables container recycling

**tab_rows vector** (in `PaneContext`)
- Before: `IObservableVector<IInspectable>` containing pre-built XAML `Border` objects
- After: `IObservableVector<FileRowViewModel>` containing data-only ViewModels
- ListView creates XAML containers lazily from DataTemplate — only for visible rows

**Sidebar sections** (XAML `Expander` controls in `MainWindow.xaml`)
- `QuickAccessExpander` — hardcoded Desktop/Downloads + `PaneContext.back_stack` last-5
- `ThisPCExpander` — `GetDiskFreeSpaceExW` per drive, rendered with capacity `ProgressBar`
- `BookmarksExpander` — `FavoritesService`-backed list with pin/unpin button

### Data Flow

**Row load flow (after P-track):**
```
LoadDirectoryAsync (background)
  → builds std::vector<ShellItemRef> with two-bucket icons
  → UI thread: for each item, winrt::make<FileRowViewModel>(item) → rows.Append(vm)
  → ListView creates DataTemplate containers only for visible rows (~30-50 at a time)
  → BackfillIconsAsync (background): resolves real icons
  → UI thread: for each vm, vm.IconSource(bitmapSource) → PropertyChanged fires
  → Image binding updates in-place — no row recreation
```

**Sidebar load flow (S-track):**
```
LoadDirectory completes
  → UpdateSidebar() called:
      QuickAccess: prepend Desktop/Downloads, append back_stack.last(5) deduplicated
      ThisPC: enumerate drives via GetLogicalDriveStrings, call GetDiskFreeSpaceExW
      Bookmarks: FavoritesService.GetAll()
```

### Error Handling

- `IconSource` setter: if `BitmapSource` creation fails, keep previous icon (folder/file fallback)
- Sidebar drive enumeration: skip drives that return errors from `GetDiskFreeSpaceExW`
- `FileRowViewModel::FromShellItem` with missing file: log to TraceLog, return valid VM with empty fields

### Testing Approach

- **FileRowViewModelTests.cpp** — unit tests for INPC, property getters, icon set/get, DisplaySize formatting
- **P3 regression** — existing 82 tests must still pass after row-creation overhaul; run full suite
- **Manual host launch** — required for P2 (DataTemplate visual fidelity), U-track, S-track
- **Performance benchmark** — navigate to C:\Windows\System32, compare TraceLog
  `first_batch_publish_tick - load_start_tick` before and after P-track
