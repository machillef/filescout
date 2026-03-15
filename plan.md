# FileScout UI/UX Modernization Plan

## Previous Work (Complete)
All 10 feature slices (18-27), UX bug fixes, and polish phase items are done.

---

## Modernization — Execution Order

### Phase 1: Performance
- [x] **P2** — Virtual scrolling for file list (highest impact)
- [x] **P1** — Throttle CursorMoved and reduce event noise
- [x] **P3** — Navigation load debounce

### Phase 2: Sidebar
- [x] **S1** — Sidebar module and data model
- [x] **S2** — Integrate sidebar into main layout
- [x] **S3** — Sidebar visual polish

### Phase 3: Visual Polish
- [x] **V1** — Custom theme system
- [x] **V2** — File list row polish
- [x] **V3** — Address bar and tab bar polish

### Phase 4: Native Shell Integration
- [x] **N1** — Native file type icons (SHGetFileInfo)
- [x] **N2** — Icon cache async pipeline
- [ ] **N3** — Native Windows context menu (IContextMenu COM) — SKIPPED (too complex for iced HWND access)
- [x] **N4** — Hybrid context menu with native shell actions

---

## Status: ALL SLICES COMPLETE

All 4 phases (13 slices) of the UI/UX Modernization plan are implemented.

**N3 decision:** Full IContextMenu COM integration was skipped because accessing the raw HWND from within iced 0.14's Elm architecture is impractical without upstream support. Instead, N4 provides native shell actions (Open With, Windows Properties, Open in Terminal) via ShellExecuteW, covering the most useful context menu enhancements.
