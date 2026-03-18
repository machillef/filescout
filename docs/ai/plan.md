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
- [x] `R9` sort/group/view mode parity
  - [x] `R9a` sort column/direction with clickable column headers
  - [x] `R9b` view mode toggle (Cards / List) with per-pane state
  - [x] `R9c` grouped rows tied to the active sort column
- [x] `R10` preview and inspect surface

## Phase 3: Differentiated Features
- [ ] `R11` archive mode
- [ ] `R12` batch rename
- [ ] `R13` command palette
- [ ] `R14` Git-aware and power-user workflows
- [ ] `R15` AI features after parity stabilizes

## Current Focus

- Keep all active work inside `native/`
- Treat `crates/` as reference-only unless a native design question needs prior
  behavior as input
- Do not add new durable initiative state for Rust-only slices
- `native/` is now tracked in git; keep further alpha work isolated there
- Phase 2 baseline is complete; continue with `R11` archive mode next
- Keep the pre-existing `MSB8027` + `LNK4042` WinAppSDK duplicate-object warning
  visible in validation reports until the upstream warning path is removed
