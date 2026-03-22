---
name: WinUI 3 staged dispatcher publish for first-paint optimization
description: Split a large UI batch into first-batch + final-batch TryEnqueue dispatches to let WinUI render the first visible rows before blocking on the rest
type: project
consolidated-into: execution-loop/gotchas/winui3-staged-dispatcher-publish.md
consolidated-date: 2026-03-22
---

## Pattern

When `LoadDirectoryAsync` builds all rows in a single `TryEnqueue` callback,
large folders (1k–5k items) block the UI thread until all rows are materialized.

Split into two consecutive `TryEnqueue` calls from within the first callback:

1. **First dispatch**: Build `min(K, total)` rows (prefix of final sorted order).
   Record timing. Set `awaiting_final_batch = true`.
2. **Second dispatch** (posted from inside first): Guard with `load_generation`,
   `active_tab_index`, AND `awaiting_final_batch` before appending. On return,
   clear `awaiting_final_batch = false`.

## Why two guards work

- `load_generation` catches stale navigation (user navigated away)
- `awaiting_final_batch` catches mid-load full rebuilds (sort/group/viewmode change
  via `RebuildAllTabRowsForPane`) — prevents duplicate row appends

## Key details

- `pane.items` must be set to the FULL sorted list before the first batch so
  selection-index lookups (`items[index]`) remain valid during the staged window
- Capture `previousGroupLabel` from first batch in the second lambda to maintain
  group-header continuity across the split
- Use `(std::min)(K, total)` — not `std::min` — to avoid Windows.h `min` macro expansion
- Track `user_interacted_after_first_batch` to avoid clobbering live selection on
  deferred restoration (`SelectItemByPath`) in the second dispatch

## Where implemented

`native/FileScout.Native/MainWindow.xaml.cpp` — `LoadDirectoryAsync` (first batch),
`RebuildAllTabRowsForPane` (clears `awaiting_final_batch`),
`FileRowsListView_SelectionChanged` (sets `user_interacted_after_first_batch`)
`native/FileScout.Native/MainWindow.xaml.h` — `PaneContext` fields
