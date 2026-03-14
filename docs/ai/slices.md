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

## Slice 5: File Operations
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

## Slice 11: AI Features
**Goal:** Natural language search, smart organization suggestions
**Status:** Not Started
**Depends on:** Slice 7
