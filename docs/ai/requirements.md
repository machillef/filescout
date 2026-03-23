# FileScout Native Rewrite Requirements

## Product Goal

Build a Windows 11 file manager that matches Explorer-class shell behavior first
and layers FileScout differentiation on top only after native parity is stable.

## Current Implementation Scope

- Active implementation path: `native/` C++20 + C++/WinRT + WinUI 3
- Packaging model: unpackaged desktop app
- Target architecture: Windows 11 x64
- Shell-backed behavior wins over custom app behavior whenever the two conflict

## Phase 1 Requirements

### Shell-Backed Browsing
- Browse real filesystem directories with folder-first ordering
- Support selection, activation, and metadata display for shell-backed items
- Show native icons in the file pane
- Support Explorer-grade context menus for real filesystem items

### Core File Actions
- Open, open with, properties, rename, delete, copy, cut, and paste using
  Windows shell semantics
- Recycle Bin support for deletes
- Clipboard and drag/drop interop

### Navigation
- Address bar with direct path entry
- Back/forward and parent navigation
- Sidebar with places, favorites, and pinned locations

## Phase 2 Requirements

- Tabs
- Dual pane
- Sort/group/view mode parity
- Preview and inspect surface

## Phase 3 Requirements

- Archive mode
- Batch rename
- Command palette

## Current Focus (Post-R13)

- UI/UX polish and performance — see `docs/ai/ux-perf-plan.md`
