# FileScout Native Rewrite Plan

## Pivot Decision

The Rust + `iced` prototype is now treated as a completed learning phase, not
the path to Explorer parity. It proved performance and feature velocity, but it
did not meet the Windows-native integration bar required for FileScout's target
product.

The active implementation track is now:

- same repo
- parallel greenfield rewrite
- pure C++20
- C++/WinRT + WinUI 3
- Windows 11 only
- shell-backed parity before differentiated features

## Legacy Work Preserved

The modernization work remains valuable and should not be deleted:

- performance learnings
- dual-pane and tab workflow learnings
- native context-menu spike learnings
- shell-integration failure modes in the `iced` host layer

Those learnings now inform the native rewrite instead of the Rust app.

## Native Rewrite Phases

### Phase 1: Explorer Parity Foundation
- [x] **R1** — Same-repo native workspace and unpackaged WinUI 3 shell scaffold
- [ ] **R2** — Shell-backed file pane with native icon extraction and basic selection
- [ ] **R3** — Windows 11 style context menu model with native overflow path
- [ ] **R4** — Shell-backed open/open-with/properties/delete/rename/copy/cut/paste
- [ ] **R5** — Native drag/drop and clipboard interop

### Phase 2: FileScout Baseline
- [ ] **R6** — Tabs
- [ ] **R7** — Dual-pane layout
- [ ] **R8** — Favorites, sidebar tree, and pinned locations
- [ ] **R9** — Sort/group/view mode parity
- [ ] **R10** — Preview and inspect surfaces

### Phase 3: Differentiated Features
- [ ] **R11** — Archive mode
- [ ] **R12** — Batch rename
- [ ] **R13** — Command palette
- [ ] **R14** — Git-aware and power-user workflows
- [ ] **R15** — AI features after parity stabilizes

## Current Status

`R1` is the active bootstrap slice: the native workspace, contract layer, and
basic filesystem-backed shell window now exist in `native/`.
