# FileScout UI/UX Modernization Plan

## Previous Work (Complete)
All 10 feature slices (18-27), UX bug fixes, and polish phase items are done.

---

## Modernization — Execution Order

### Phase 1: Performance
- [ ] **P2** — Virtual scrolling for file list (highest impact)
- [ ] **P1** — Throttle CursorMoved and reduce event noise
- [ ] **P3** — Navigation load debounce

### Phase 2: Sidebar
- [ ] **S1** — Sidebar module and data model
- [ ] **S2** — Integrate sidebar into main layout
- [ ] **S3** — Sidebar visual polish

### Phase 3: Visual Polish
- [ ] **V1** — Custom theme system
- [ ] **V2** — File list row polish
- [ ] **V3** — Address bar and tab bar polish

### Phase 4: Native Shell Integration
- [ ] **N1** — Native file type icons (SHGetFileInfo)
- [ ] **N2** — Icon cache async pipeline
- [ ] **N3** — Native Windows context menu (IContextMenu COM)
- [ ] **N4** — Hybrid context menu fallback

---

## Slice Details

### P2: Virtual Scrolling
- Add `scroll_offset: f32` to `TabState`
- Add `ScrollChanged(f32)` to `FileListMessage`
- Rewrite `file_list_view()`: spacer-based, only visible rows
- Fixed 28px row height, calculate visible range
- Reset scroll on navigation/sort
- Arrow key nav scrolls into view

### P1: Throttle CursorMoved
- Remove `CursorMoved` from subscription
- Capture cursor position from right-click event directly

### P3: Navigation Load Debounce
- Add `load_generation: u64` to `TabState`
- Increment on nav, carry through load task, discard stale results
