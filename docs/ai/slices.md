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
**Status:** In Progress
**Completed:** Tasks 1–3 — `PaneLoadTiming` instrumentation (pre-existing),
staged first-batch + final-batch publish in `LoadDirectoryAsync`, `awaiting_final_batch`
guard in `RebuildAllTabRowsForPane`, `user_interacted_after_first_batch` live-state
protection in `FileRowsListView_SelectionChanged`; build + 57 tests pass
**Remaining:** Task 4 — manual benchmark acceptance (5× timings on a 1k–5k item
folder, verify ≥25%/150ms first-batch improvement and ≤10% full-completion regression)
**Depends on:** perf: background enumeration (complete)

## R13: Command Palette
**Goal:** Global fuzzy command surface in the native host.
**Status:** Not Started
**Depends on:** `R6`

## R14: Git-Aware And Power-User Workflows
**Goal:** Add differentiated workflows after Explorer-parity basics stabilize.
**Status:** Not Started
**Depends on:** `R4`, `R7`, `R13`

## R15: AI Features
**Goal:** Add AI only after the native parity baseline is stable.
**Status:** Not Started
**Depends on:** `R12`, `R13`
