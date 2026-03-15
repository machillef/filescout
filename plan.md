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

### Remaining
- [ ] Bug 2: Right-click on empty space (Refresh, New Folder)
- [ ] Bug 7: App icon
- [ ] Bug 8: Address bar autocomplete
- [ ] Bug 9: Pinned favorites sidebar
- [ ] Bug 10: "This PC" shortcut
- [ ] Bug 13: Better file icons (SVG icon set)
- [ ] Bug 16: Multi-select properties with total size
- [ ] Slice 23 fix: Progress bar animation
- [ ] Slice 25 fix: Visible pane divider
