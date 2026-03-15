# FileScout Slices

## Slice 1: Directory Listing
**Goal:** Display files/folders in a scrollable list with name, size, date, type columns
**Status:** Complete
**Acceptance:** User sees sorted file list for any directory, directories first ✓

## Slice 2: Navigation
**Goal:** Address bar, breadcrumbs, back/forward, drive selector
**Status:** Complete
**Depends on:** Slice 1 ✓

## Slice 3: Dual Pane
**Goal:** Split view with independent navigation per pane
**Status:** Complete
**Depends on:** Slice 2 ✓

## Slice 4: Tabs
**Goal:** Multiple tabs per pane with tab management
**Status:** Complete
**Depends on:** Slice 3 ✓

## Slice 5: File Operations (Basic)
**Goal:** Copy, move, delete (to trash), rename with undo/redo
**Status:** Complete (delete+rename+undo; copy/move deferred to Slice 5b)
**Depends on:** Slice 1 ✓

## Slice 6: Command Palette
**Goal:** Fuzzy-matched command search (Ctrl+Shift+P)
**Status:** Complete
**Depends on:** Slice 2 ✓

## Slice 7: Search
**Goal:** Fuzzy file search, recursive, with extension filtering
**Status:** Complete
**Depends on:** Slice 1 ✓

## Slice 8: File Preview
**Goal:** Preview panel for images, text, PDFs
**Status:** Complete (text + images + metadata; PDF shows as metadata)
**Depends on:** Slice 1 ✓

## Slice 9: Batch Rename
**Goal:** Pattern-based multi-file rename with preview
**Status:** Complete (Ctrl+R; tokens {name}/{ext}/{n}/{n:NN}; ext filter; live preview; undo support)
**Depends on:** Slice 5 ✓

## Slice 10: Theming
**Goal:** Dark/light themes, custom color schemes
**Status:** Complete (Ctrl+T toggle; TOML persistence; startup load; command palette entry)

## Slice 11: Copy & Move
**Goal:** Copy (F5) and Move (F6) files/directories between panes with progress feedback
**Status:** Complete (F5/F6 + command palette; async with progress overlay; cross-device support)

## Slice 12: Multi-Selection
**Goal:** Select multiple files with Shift+click, Ctrl+click, Shift+arrows, Ctrl+A; batch delete/copy/move
**Status:** Complete (Ctrl+click, Shift+click, Shift+↑↓, Ctrl+A; batch F5/F6/F8; selection count status bar)

## Slice 13: Bookmarks / Favorites
**Goal:** Save and jump to frequently-used directories; sidebar or overlay
**Status:** Complete (Ctrl+D adds; Ctrl+B opens overlay; TOML persistence; command palette entries)
**Depends on:** Slice 2 ✓

## Slice 14: File Watcher (Auto-Refresh)
**Goal:** Watch current directory for changes and refresh listing automatically
**Status:** Complete (notify crate; Recipe subscription; debounce 300 ms; auto-refresh on create/modify/remove)
**Depends on:** Slice 1 ✓

## Slice 15: Properties & Permissions
**Goal:** File/directory properties dialog (size, dates, attributes, permissions)
**Status:** Complete (Alt+Enter; modal overlay; timestamps; Windows attrs; async dir size)
**Depends on:** Slice 1 ✓

## Slice 16: Archive Support
**Goal:** Browse ZIP archives as virtual directories; extract to current/other pane
**Status:** Not Started
**Depends on:** Slice 3 ✓

---

## Slice 17: AI Features (Optional — Last)
**Goal:** Natural language search, smart organization suggestions, AI-assisted rename
**Status:** Not Started
**Depends on:** Slices 7, 9 ✓
**Notes:** Deferred until core functionality is solid; uses `AiProvider` trait for swappable backends
