# FileScout Implementation Plan

## Phase 1: Foundation (Current)
- [x] Scaffold Cargo workspace
- [x] Basic iced window with directory listing
- [x] Core models and file system operations
- [x] AI trait definition (placeholder)
- [x] Project docs and CLAUDE.md

## Phase 2: Single Pane File Browser
- [ ] Sortable columns (name, size, date, type)
- [ ] Double-click to navigate into directories
- [ ] Parent directory navigation (..)
- [ ] Virtual scrolling for large directories
- [ ] File system watcher for live updates
- [ ] File icons by type

## Phase 3: Navigation
- [x] Address bar with editable path
- [x] Breadcrumb navigation
- [x] Back/forward history
- [x] Drive/volume selector
- [ ] GoTo dialog with autocompletion

## Phase 4: Dual Pane & Tabs
- [ ] Split view layout
- [ ] Tab bar per pane
- [ ] Tab management (new, close, drag)

## Phase 5: File Operations
- [ ] Copy, move, delete (to Recycle Bin)
- [ ] Rename (single file)
- [ ] Undo/redo command history
- [ ] Progress tracking for long operations
- [ ] Clipboard integration

## Phase 6: Power Features
- [ ] Command palette (Ctrl+Shift+P)
- [ ] Fuzzy file search
- [ ] Batch rename
- [ ] File preview panel

## Phase 7: Polish
- [ ] Dark/light theme system
- [ ] Settings/preferences dialog
- [ ] Keyboard shortcuts
- [ ] Context menu integration

## Phase 8: AI Features
- [ ] AI provider trait implementation (Claude API)
- [ ] Natural language search
- [ ] Smart organization suggestions
- [ ] AI-assisted batch rename
