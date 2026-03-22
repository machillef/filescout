# FileScout Native Rewrite Plan

## Active Track

- Same repo
- Parallel greenfield rewrite
- C++20 + C++/WinRT + WinUI 3
- Windows 11 only
- Unpackaged desktop app

## Phase 1: Explorer Parity Foundation
- [x] `R1` native workspace + unpackaged WinUI 3 shell scaffold
- [x] `R2` shell-backed file pane with native icon extraction and basic selection
  - [x] `R2a` selectable shell pane foundation
  - [x] `R2b` native icon extraction and richer row visuals
- [x] `R3` Windows 11 style context menu model with native overflow
  - [x] `R3a` FileScout-owned context menu foundation
  - [x] `R3b` shell-backed overflow / `More Windows actions...` invocation
- [x] `R4` shell-backed open/open-with/properties/delete/rename/copy/cut/paste
  - [x] `R4a` shell-backed open/open-with/properties and file activation
  - [x] `R4b` new folder/rename/delete
  - [x] `R4c` copy/cut/paste
- [x] `R5` native drag/drop and clipboard interop

## Phase 2: FileScout Baseline
- [x] `R6` tabs
- [x] `R7` dual-pane layout
- [x] `R8` favorites, sidebar tree, and pinned locations
  - [x] `R8b` Places icons, pin-to-Places affordance, and compact drive usage cards
- [x] `R9` sort/group/view mode parity
  - [x] `R9a` sort column/direction with clickable column headers
  - [x] `R9b` view mode toggle (Cards / List) with per-pane state
  - [x] `R9c` grouped rows tied to the active sort column
- [x] `R10` preview and inspect surface

## Phase 3: Differentiated Features
- [x] `R11` archive mode
  - [x] `R11a` ZIP virtual-view foundation
  - [x] `R11b` archived-file temp extraction and shell open
  - [x] `R11c` explicit extract/reveal affordances and conflict-safe extraction naming
  - [x] `R11d` shell-capability-based archive detection beyond ZIP
  - [x] `R11e` unsupported-archive feedback and archive affordances in normal listings
- [x] `R12` batch rename
  - [x] `R12a` multi-selection batch rename foundation
  - [x] `R12b` richer sequential controls and dialog preview polish
  - [x] `R12c` preset prefix/suffix rename modes
  - [x] `R12d` literal find/replace preset
  - [x] `R12e` case-transform preset family
  - [x] `R12f` shared Windows file-name validation parity
  - [x] `R12g` dialog layout and preview readability polish
- [ ] `R13` command palette
- [ ] `R14` Git-aware and power-user workflows
- [ ] `R15` AI features after parity stabilizes

## Differentiated Features (Phase 3 — Completed)
- [x] `R11` archive mode (R11a–R11e)
- [x] `R12` batch rename (R12a–R12g)
- [x] `R16` visual polish and Explorer chrome (R16a–R16b)
- [x] `R17` fuzzy search (R17a–R17e) — FuzzyMatcher, SearchService, Filter + Recursive modes
- [x] `R18` chrome UX redesign (R18a–R18c) — drive icons, breadcrumb nav, keyboard type-ahead

## Current Focus

- Keep all active work inside `native/`
- `native/` is now tracked in git; keep further alpha work isolated there
- `R17` (fuzzy search) is complete (R17a–R17e): FuzzyMatcher with SIMD scoring,
  SearchService background walk + min-heap, Filter mode auto-entered from SearchTextBox,
  Recursive mode with streaming ProgressBar and navigate-on-select; 71/71 tests pass
- `R18` (chrome UX redesign) is complete (R18a–R18c): drive root icon fix, breadcrumb
  navigation bar + dedicated search box, keyboard type-ahead in file list; 71/71 tests pass
- Perf staged row publication (Tasks 1–3) is complete: first-batch publish of
  min(150, total) rows + final-batch dispatch, stale-load and rebuild cancellation
  guards, live-selection protection; build + tests pass; Task 4 (manual benchmark
  acceptance on a 1k–5k item folder) is pending — observe TraceLog `PerfLoad`
  lines in the running app to capture before/after timings and close the slice
- Next automatable slice: `R13` (command palette) — no spec yet; requires writing
  a plan before implementation can begin
- Async icon loading (perf) — large folders (Downloads, System32) still slow on
  first open; separate perf initiative needed
- Keep archive mode scoped to shell-enumerable archive formats plus the current
  temp-open and explicit extract/reveal flows until a broader non-shell archive
  strategy is designed and validated
- Keep the pre-existing `MSB8027` + `LNK4042` WinAppSDK duplicate-object warning
  visible in validation reports until the upstream warning path is removed
