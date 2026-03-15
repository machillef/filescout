# FileScout Production-Ready — Implementation Plan

## Phase 1 (No deps, parallel)
- [x] Slice 18: Open / Shell Execute
- [x] Slice 19: Confirmation Dialogs
- [x] Slice 20: Hidden Files Toggle

## Phase 2 (Depends on 19)
- [x] Slice 21: Clipboard Copy/Cut/Paste

## Phase 3 (No deps, parallel)
- [x] Slice 22: File/Folder Icons
- [x] Slice 23: Copy/Move Progress Bar
- [x] Slice 25: Resizable Pane Split

## Phase 4 (Depends on 18+19+21)
- [x] Slice 24: Right-Click Context Menu

## Phase 5 (Depends on 20)
- [x] Slice 26: View Modes — Details View

## Phase 6 (Optional)
- [x] Slice 27: Drag-and-Drop (foundation module only — not wired into UI)

---

## UX Bug Fixes (from manual testing)

### Fixed
- [x] Bug 1: Double-click to open folders/files
- [x] Bug 3: Arrow key navigation (Up/Down)
- [x] Bug 4: App starts maximized
- [x] Bug 5: Single-pane default (F9 toggles dual-pane)
- [x] Bug 6: Starts in home directory
- [x] Bug 11: Details view is default
- [x] Bug 12: Type-ahead letter jump
- [x] Bug 14: Ctrl+T opens new tab (theme moved to Ctrl+Shift+T)
- [x] Bug 15: Scroll stability (stable scrollable IDs)
- [x] Bug 17: Delete key works
- [x] Bug 24-fix: Rename auto-focuses input

### Fixed (Round 2)
- [x] Bug 2: Right-click on empty space (Refresh, New Folder)
- [x] Bug 16: Multi-select properties with total size
- [x] Slice 23 fix: Progress bar animation (stream-based per-file updates)
- [x] Slice 25 fix: Visible pane divider

### Fixed (Round 3)
- [x] Bug 8: Address bar Tab completion (auto-complete partial paths)
- [x] Bug 9: Quick-access bar (Home, Desktop, Documents, Downloads)
- [x] Bug 10: "This PC" and navigation shortcuts in command palette

### Fixed (Round 4)
- [x] Bug 13: Improved file icons (19 distinct categories vs. original 9)

### Remaining
- [ ] Bug 7: App icon (needs .ico asset — requires graphic design)
