# FileScout Decisions

## 001: Language — Rust

**Date:** 2026-03-14
**Status:** Accepted

Rust for maximum performance, memory safety, and tiny binary size. Closest to
File Pilot's C approach but with modern safety guarantees.

**Trade-off:** Steeper learning curve for the developer (Rust beginner), mitigated
by starting with pure-logic core crate before GUI work.

## 002: GUI Framework — iced

**Date:** 2026-03-14
**Status:** Accepted

iced 0.14 over egui. iced's Elm architecture (model-update-view) scales better
for a complex, stateful app like a file manager. CPU-efficient (redraws only on
state change vs egui's every-frame redraw). Better theming support.

**Trade-off:** Steeper initial learning curve than egui, but the structured
architecture pays off as complexity grows.

**Revisit if:** Accessibility becomes a hard requirement (both are weak here),
or iced proves too immature for production use.

## 003: Workspace Architecture

**Date:** 2026-03-14
**Status:** Accepted

Cargo workspace with four crates: app, core, ui, ai. Enables parallel
compilation, clean separation of concerns, and testable core logic without GUI.

## 004: Async-First I/O

**Date:** 2026-03-14
**Status:** Accepted

All file operations through tokio. UI thread never waits on disk I/O.
Critical for responsiveness on slow network drives.

## 005: Command Pattern for File Operations

**Date:** 2026-03-14
**Status:** Accepted

All user actions (copy, move, rename, delete) modeled as commands supporting
undo/redo. Enables reliable operation history.
