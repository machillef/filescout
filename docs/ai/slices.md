# FileScout Native Rewrite Slices

## R1: Native Workspace And Shell Scaffold
**Goal:** Create a same-repo WinUI 3 desktop app scaffold with real filesystem
enumeration and initial domain contracts.
**Status:** Complete
**Acceptance:** The native app hosts an unpackaged WinUI 3 shell, loads the
current directory, and renders a placeholder Explorer-style surface.

## R2: Shell-Backed File Pane With Native Icon Extraction And Basic Selection
**Goal:** Turn the placeholder pane into a usable shell-backed file list with
selection, metadata, and native icons.
**Status:** Complete
**Completed sub-slices:** `R2a` selectable shell pane foundation; `R2b` native
icon extraction and richer row visuals
**Follow-up note:** manual validation of the native selection flow is still
recommended before widening behavior-heavy slices
**Depends on:** `R1`

## R3: Windows 11 Style Context Menu Model With Native Overflow
**Goal:** FileScout-owned Windows 11 menu with shell-backed overflow for
unsupported extension cases.
**Status:** Complete
**Completed sub-slices:** `R3a` FileScout-owned context menu foundation; `R3b`
shell-backed overflow / `More Windows actions...` invocation
**Follow-up note:** manual validation of the right-click and overflow flow is
still recommended before widening into shell file operations
**Depends on:** `R2`

## R4: Shell-Backed Open/Open With/Properties/Delete/Rename/Copy/Cut/Paste
**Goal:** File operations use Windows shell semantics first.
**Status:** Complete
**Completed sub-slices:** `R4a` shell-backed open, open with, properties, and
file activation; `R4b` native new-folder, rename, and delete mutations; `R4c`
clipboard-backed `Copy`, `Cut`, and `Paste`
**Follow-up note:** manual validation of file activation, destructive actions,
and clipboard-backed paste flows is still recommended before widening into drag
and drop; the native debug host is currently blocked by a startup abort that
must be resolved before further feature slices
**Depends on:** `R2`, `R3`

## R4d: Native Runtime Stability Fix
**Goal:** Restore a reliably launching unpackaged WinUI 3 host before more
explorer features are added.
**Status:** Complete
**Root cause fixed:** `WindowsAppSDKSelfContained=true` caused a mixed-mode
initialization where `MddBootstrapInitialize2` was a no-op and the MRT resource
namespace was never established; switching to framework-package mode
(`WindowsAppSDKSelfContained=false`) fixed both the XAML resource abort and a
clean-build PCH memory failure (`C3859`/`C1076`)
**Depends on:** `R4c`

## R5: Native Drag/Drop And Clipboard Interop
**Goal:** Native drag/drop and clipboard parity for filesystem items.
**Status:** Complete
**Completed sub-slices:** `R5a` keyboard shortcuts (Ctrl+C/X/V, Del, F2, F5);
`R5b` drop target (DragOver + Drop with GetDeferral + CopyItemsFromPaths);
`R5c` drag source (DragItemsStarting with StorageFile/Folder packaging)
**Follow-up note:** manual validation of in-app drag/drop and cross-app
Explorer interop is recommended before widening into R6
**Depends on:** `R4d`

## R6: Tabs
**Goal:** Multiple tabs per pane in the native shell.
**Status:** Complete
**Completed sub-slices:** `R6` TabView with per-tab ListViews, state persistence,
Ctrl+T / Ctrl+W shortcuts, and last-tab close protection
**Follow-up note:** manual validation of tab open/close/switch and keyboard
shortcuts is recommended before widening into R7
**Depends on:** `R5`

## R7: Dual-Pane Layout
**Goal:** Two independent panes with active-pane routing.
**Status:** Complete
**Completed sub-slices:** `R7` two `TabView` columns, `PaneContext` per-pane state,
active-pane indicator border, `SetActivePaneIndex`/`Ctrl+Tab` routing
**Follow-up note:** manual validation of two-pane browsing, pane focus switching,
and cross-pane drag-and-drop is recommended before widening into R8
**Depends on:** `R6`

## R8: Favorites, Sidebar Tree, And Pinned Locations
**Goal:** Real navigation surfaces for places, favorites, and shell locations.
**Status:** Complete
**Completed sub-slices:** `R8` FavoritesService with `SHGetKnownFolderPath`; clickable sidebar
buttons for Home/Desktop/Documents/Downloads/Pictures/Music/Videos/This PC/Recycle Bin;
`EnumerateDrives()` for the "This PC" virtual view; startup path changed to user home folder;
`R8b` sidebar polish with per-place icon tiles, persisted `Pin to Places` from the file-list
context menu, and compact clickable drive-usage cards in the sidebar
**Follow-up note:** manual validation of each sidebar entry plus the new pinning flow and drive
cards is recommended before widening into more sidebar-specific polish
**Depends on:** `R2`, `R7`

## R9: Sort/Group/View Mode Parity
**Goal:** Explorer-class list behavior for sort, grouping, and presentation.
**Status:** Complete
**Completed sub-slices:**
- `R9a` sort column/direction with clickable column headers, folders-first on all columns,
  date-modified field, per-pane sort state
- `R9b` view mode toggle (Cards/List) with per-pane state, compact list rows,
  `RebuildAllTabRowsForPane` with live-item correctness for active tab
- `R9c` group toggle tied to the active sort column, lightweight in-row group headers that
  preserve ListView selection/indexing, and background-tab sort refresh for shared pane state
**Follow-up note:** manual validation of grouped rendering, per-pane group toggles, and
group-label quality on filesystem and "This PC" views is still recommended before widening into R11
**Depends on:** `R2`

## R10: Preview And Inspect Surface
**Goal:** Native preview and metadata inspection.
**Status:** Complete
**Completed sub-slices:** `R10` sidebar image preview via `StorageFile`+`SetSourceAsync`
(generation-counter cancellation), created date, file attributes, is_image detection
**Depends on:** `R2`

## R11: Archive Mode
**Goal:** Browse archives as FileScout-owned virtual views in the native app.
**Status:** Complete
**Completed sub-slices:** `R11a` ZIP virtual-view foundation via shell namespace
enumeration, per-tab archive-view state, and read-only archive command guards;
`R11b` archived-file temp extraction and shell open via shell copy-out to a
per-open temp workspace; `R11c` explicit extract/reveal affordances plus
conflict-safe unique naming when extracting into the archive's parent folder;
`R11d` shell-capability-based archive detection so any regular file exposed by
the host shell as an enumerable archive namespace now routes through the same
archive browse/open/extract flows (validated for `.tar` on this machine);
`R11e` explicit unsupported-archive feedback plus archive labeling and browse
affordances in normal directory listings
**Follow-up note:** manual validation of ZIP/TAR browsing plus unsupported
archive messaging on machines without shell archive extensions is still
recommended before widening into non-shell-backed archive formats
**Depends on:** `R7`

## R12: Batch Rename
**Goal:** Bring FileScout power-user rename flows into the native shell.
**Status:** Complete
**Completed sub-slices:** `R12a` multi-selection batch rename foundation with
sequential numbering, selection-aware rename/delete/copy/cut/drag behavior,
and bulk shell rename/delete execution; `R12b` custom sequential numbering
controls plus live rename preview and validation polish for the batch rename
dialog; `R12c` preset-based `Add prefix` / `Add suffix` modes that preserve
file extensions and reuse the same preview-driven shell rename flow; `R12d`
literal `Find and replace` for folder names and file stems, with no-op
rejection and duplicate detection against unchanged selected items; `R12e`
`Change case` with lowercase/uppercase/title-case modes for folder names and
file stems, still preserving file extensions and reusing the same
preview-driven shell rename flow; `R12f` shared Windows file-name validation
across batch rename and single-item rename/create flows so dot names, trailing
space/periods, control characters, and reserved device names are rejected
consistently; `R12g` a more structured WinUI batch rename dialog with
selection summary, two-column setup/preview layout, clearer invalid-state
messaging, and preview cards that surface changed vs unchanged items more
cleanly
**Follow-up note:** manual validation of Ctrl/Shift multi-selection, multi-item
drag/delete behavior, and the polished batch-rename dialog in the running WinUI
host is still recommended
**Depends on:** `R4`, `R7`

## R16: Visual Polish And Explorer Chrome
**Goal:** Replace the detached hero-card layout with integrated title-bar chrome,
apply Explorer-inspired sidebar and pane-indicator surfaces, and switch sidebar
icons to shell-backed priority with Fluent glyph fallback.
**Status:** Complete
**Completed sub-slices:** `R16a` shell chrome restructure — custom title bar
integration (`TitleBarDragHost`, `ConfigureTitleBar`, `SetTitleBar`), hero card
removed, toolbar collapsed into title-bar row with FileScout/ALPHA identity,
Fluent icon glyphs for Back/Forward/Refresh, pane border indicators (2px accent
active / 1px neutral inactive) with `UpdateActivePaneIndicator`; sidebar Fluent
glyph helpers (`BuildSidebarFontIcon`, `KnownFolderGlyph`) and drive glyph added;
`R16b` sidebar background changed to `CardBackgroundFillColorDefaultBrush` for
stronger contrast against Mica backdrop in both dark and light themes
**Follow-up note:** manual validation of sidebar contrast (dark + light theme),
drag-region behaviour inside title-bar host, and full R16 spec checklist is still
recommended before starting R13
**Depends on:** `R9`, `R8b`

## Perf: Staged Row Publication
**Goal:** Show the first visible batch of rows (min 150, total) sooner for large
folders (1k–5k items) by splitting row publication into two dispatcher callbacks.
**Status:** Superseded
**Note:** Superseded by Perf-2 streaming enumeration (2026-03-22). The two-batch
approach remains in place but the benchmark acceptance task is moot — first_batch_ms
is now < 200ms by design from streaming enumeration.
**Depends on:** perf: background enumeration (complete)

## R17: Fuzzy Search
**Goal:** Bring fast fuzzy search to both panes with Filter (in-place) and Recursive
(background walk) modes, driven by a dedicated SearchTextBox.
**Status:** Complete
**Completed sub-slices:**
- `R17a` FuzzyMatcher — SIMD-accelerated (SSE4.2 `_mm_cmpistrm`) character-presence
  bitmask filter + Smith-Waterman alignment scoring; 64 unit tests
- `R17b` SearchService — background directory walk, min-heap top-200, PostFn dispatcher
  abstraction, cancellation via `shared_ptr<atomic<bool>>`
- `R17c` SearchFilter mode — SearchTextBox auto-enters Filter on first keystroke;
  scores snapshot items; Escape restores; Ctrl+F focuses SearchTextBox
- `R17d` SearchRecursive mode — Enter escalation from Filter; streaming ProgressBar;
  `on_results` replaces ListView rows; `on_complete` updates status bar
- `R17e` Result row polish — `CreateSearchRowView` with relative-path subtitle;
  `search_results` parallel vector for navigate-on-select via double-click
**Depends on:** `R16`, perf staged rows

## R18: Chrome UX Redesign
**Goal:** Fix drive icon bug; separate navigation (breadcrumb) from search (dedicated box);
add keyboard type-ahead in file list.
**Status:** Complete
**Completed sub-slices:**
- `R18a` Drive icon fix — `GetIconForItem` detects drive roots and calls `SHGetFileInfoW`
  without `SHGFI_USEFILEATTRIBUTES` so `C:\` gets its real drive icon
- `R18b` Breadcrumb nav + dedicated SearchTextBox — 3-column toolbar (buttons / breadcrumb+
  address overlay / SearchTextBox); `UpdateBreadcrumbs` builds HyperlinkButton segments;
  F4 shows AddressTextBox overlay; placeholder updates on navigation; all search logic
  moved from AddressTextBox to SearchTextBox
- `R18c` Keyboard type-ahead — `FileRowsListView_KeyDown` handles A–Z VK codes;
  wrapping scan through `pane.items`; `ScrollIntoView` on match
**Spec:** `docs/superpowers/plans/2026-03-22-chrome-ux-redesign.md`
**Depends on:** `R17`

## R13: Command Palette
**Goal:** Global fuzzy command surface in the native host — Ctrl+P overlay with fuzzy-filtered command list dispatching through existing services.
**Status:** Complete (R13a–R13e all done 2026-03-22 to 2026-03-23)
**Depends on:** `R6`, `R17` (reuses FuzzyScore)

### Architecture
- `Domain/CommandEntry.h` — pure struct: `id` (wstring), `display_name`, `shortcut_text`, `category`; no WinRT dependencies
- `Services/CommandRegistry.h/.cpp` — builds static command table at startup; `Filter(query)` returns scored+sorted `std::vector<CommandEntry>` using `FuzzyScore` from `Search/FuzzyMatcher.h`
- Palette overlay: XAML `Grid` element (visibility-toggled) on the window root, centred, containing a `TextBox` + `ListView`; avoids `Popup` z-order issues and ContentDialog modal constraints
- Invocation: Ctrl+P registered in `SetupKeyboardAccelerators()` alongside existing accelerators
- Dispatch: selected command routes through `ExecuteContextCommand(id, itemIndex)` — reuses the exact same path as keyboard/context-menu actions

### Sub-slices

## R13a: CommandRegistry + Palette Scaffold
**Goal:** Ctrl+P opens a centred overlay with a fuzzy-filtered command list; Escape closes it. No commands wired to real actions yet — scaffold only.
**Status:** Complete (2026-03-22)
**Touched area:**
- `native/FileScout.Native/Domain/CommandEntry.h` (new)
- `native/FileScout.Native/Services/CommandRegistry.h` (new)
- `native/FileScout.Native/Services/CommandRegistry.cpp` (new)
- `native/FileScout.Native/MainWindow.xaml` (palette overlay Grid)
- `native/FileScout.Native/MainWindow.xaml.h` (ShowCommandPalette / HideCommandPalette declarations)
- `native/FileScout.Native/MainWindow.xaml.cpp` (Ctrl+P accelerator, TextChanged handler, ItemInvoked stub)
- `native/FileScout.Native/FileScout.Native.vcxproj` (new files)
- `native/FileScout.Tests/tests/CommandRegistryTests.cpp` (new)
**Validation:** build pass + tests pass; Ctrl+P opens overlay, typing filters list, Escape closes

## R13b: Navigation Commands
**Goal:** Wire navigation commands into the registry so they execute correctly.
**Status:** Complete (2026-03-22)
**Commands:** New Tab, Close Tab, Switch Pane, Back, Forward, Refresh, Go to Path (opens address TextBox)
**Touched area:** `CommandRegistry.cpp`, `MainWindow.xaml.cpp` (ExecuteContextCommand hook-up)
**Validation:** each command executes correctly when selected from palette

## R13c: File Operation Commands
**Goal:** Wire file operation commands — context-sensitive to active selection.
**Status:** Complete (2026-03-22)
**Commands:** Copy, Cut, Paste, Delete, Rename, New Folder, Open, Open With, Properties
**Touched area:** `CommandRegistry.cpp`, `CommandRegistryTests.cpp`
**Validation:** AllFileOperationCommandsPresent test; 80/80 tests pass; both binaries build clean

## R13d: View And Sort Commands
**Goal:** Wire view/sort/layout commands.
**Status:** Complete (2026-03-22)
**Commands wired:** Toggle View Mode (Cards↔List), Toggle Group, Sort by Name / Size / Date Modified
**Commands deferred to R13e:** Toggle Preview Panel, Toggle Dual Pane
**Touched area:** `CommandRegistry.cpp`, `MainWindow.xaml.cpp`, `CommandRegistryTests.cpp`
**Validation:** AllViewAndSortCommandsPresent test; 81/81 tests pass; both binaries build clean

## R13e: Toggle Preview Panel and Dual Pane
**Goal:** Wire the two commands deferred from R13d — `togglePreview` collapses/restores the
sidebar column; `toggleDualPane` collapses/restores the right pane (switches active pane to
left first if needed).
**Status:** Complete (2026-03-23)
**Touched area:**
- `native/FileScout.Native/MainWindow.xaml` — `x:Name="SidebarColumn"`, `x:Name="RightPaneColumn"`, `x:Name="SidebarBorder"` added
- `native/FileScout.Native/MainWindow.xaml.h` — `m_previewPanelVisible`, `m_dualPaneVisible` flags; `TogglePreviewPanel()`/`ToggleDualPane()` declarations
- `native/FileScout.Native/MainWindow.xaml.cpp` — `TogglePreviewPanel()`, `ToggleDualPane()` implementations; dispatch wired in `ExecuteContextCommand`
- `native/FileScout.Native/Shell/CommandRegistry.cpp` — `togglePreview` and `toggleDualPane` added to View section (23 commands total)
- `native/FileScout.Tests/tests/CommandRegistryTests.cpp` — `AllPanelToggleCommandsPresent` test
**Validation:** AllPanelToggleCommandsPresent RED→GREEN; 82/82 tests pass; both binaries build clean

---

## UX+Perf Sprint Round 2 (S1–S7)

Six bugs confirmed still present after the previous UX-Perf initiative. The streaming
enumeration revert (commit 75af856) also left performance still slow. This sprint addresses
all six with confirmed root causes from source analysis on 2026-03-23.

**Plan file:** `.claude/plans/sprightly-stirring-hennessy.md`
**Status:** Planned — not started

### S1: Nav Button Passthrough Fix
**Goal:** Back/Forward buttons become clickable once they enable (currently title-bar drag
region swallows the clicks because `appendRect` skips disabled controls and regions are never
re-registered after `UpdateNavButtons` enables the buttons).
**Root cause confirmed:** `appendRect` at line 1967 returns early for disabled controls.
`UpdateNavButtons` (line 4041) enables the buttons but never calls
`UpdateTitleBarInteractiveRegions`.
**Fix:** Add `UpdateTitleBarInteractiveRegions()` at the end of `UpdateNavButtons`.
**Files:** `native/FileScout.Native/MainWindow.xaml.cpp`
**Status:** Not started

### S2: Default Left Pane Fix
**Goal:** App always opens with the left pane active (thick accent border).
**Root cause confirmed:** Pane 1's async `LoadDirectoryAsync` completion runs after
`m_initComplete = true`. `rows.Clear()` in the dispatch lambda fires ListView
`SelectionChanged` → `SetActivePaneIndex(1)` because `tab_switching` is `false` at that point.
**Fix:** Wrap `rows.Clear()` and all `rows.Append()` batches in `tab_switching = true/false`
inside the async dispatch lambdas (both archive and regular paths).
**Files:** `native/FileScout.Native/MainWindow.xaml.cpp`
**Status:** Not started

### S3: Breadcrumb Path Normalization
**Goal:** Breadcrumb always shows full path (e.g. `C: › Windows › System32`).
**Root cause confirmed:** First `UpdateBreadcrumbs(path)` call (line 2130) uses the raw
pre-absolutization argument. Second call (line 2274) uses `absolute` but is guarded by
`m_activePaneIndex == paneIndex` — a pane switch during enumeration silently skips it.
**Fix:** Pre-compute `absolute = std::filesystem::absolute(path).lexically_normal()` on
the UI thread before `co_await`, use it for the first breadcrumb call. Add `TraceLog`
inside `UpdateBreadcrumbs` to log path + segments.
**Files:** `native/FileScout.Native/MainWindow.xaml.cpp`
**Status:** Not started

### S4: Letter-Jump Visual Indicator
**Goal:** Pressing a letter key gives an obvious visual cue beyond the status bar text.
**Root cause:** Status bar text `→ filename` is set but sits at the window bottom; users
miss it when eyes are on the list.
**Fix:** After a successful jump, briefly flash the active pane border to a lighter accent
colour via a 300 ms `DispatcherTimer`, then restore via `UpdateActivePaneIndicator`.
**Files:** `native/FileScout.Native/MainWindow.xaml.cpp`
**Status:** Not started

### S5: Delete Progress Indicator
**Goal:** Deleting files shows a ProgressBar so the operation is not silent.
**Root cause:** "Deleting N items…" status text is set one instruction before
`co_await winrt::resume_background()` — WinUI 3 may not render a frame in between.
`IFileOperation::PerformOperations` for 2 500 recycle operations takes 10–15 s
on the background thread with no visible feedback.
**Fix:** Show `SearchProgressForPane(activePaneIndex)` (indeterminate ProgressBar) before
`co_await`, hide it in the TryEnqueue success/failure callback. Add timing log around
`PerformOperations`.
**Files:** `native/FileScout.Native/MainWindow.xaml.cpp`
**Status:** Not started

### S6: Performance — FindFirstFileW Fast Enumeration
**Goal:** Replace `std::filesystem::directory_iterator` + `SHGetFileInfoW`-per-item with
`FindFirstFileW/FindNextFile`. `WIN32_FIND_DATA` already contains name, size, attributes,
write time, and creation time. Use a two-bucket fallback icon (folder/file) — zero shell
calls. Target: enumerate_ms < 100 ms for System32 (5 120 items).
**New function:** `ShellInteropService::BuildFastShellItemRef(WIN32_FIND_DATA, dirPath)`
**Files:** `native/FileScout.Native/Shell/ShellInteropService.cpp/.h`,
`native/FileScout.Native/MainWindow.xaml.cpp` (call site)
**Status:** Not started

### S7: Performance — Icon Backfill Phase 2
**Goal:** After fast rows appear, fill in real icons silently without scroll reset.
**Approach:** Second background dispatch loads `GetIconForItem` for all `pane.items`, posts
updated items back, replaces rows via `rows.SetAt(i, CreateRowView(...))` wrapped in
`tab_switching = true/false`. Guarded by `pane.load_generation`.
**Files:** `native/FileScout.Native/MainWindow.xaml.cpp`
**Status:** Not started

