# FileScout Status

## Current Slice
**Slice 1: Directory Listing** — Complete

## Last Completed
Navigation, sortable columns, parent entry — 2026-03-14

## What's Working
- Cargo workspace builds and passes clippy
- Directory listing with scrollable view
- Column headers: Name / Size / Modified with ▲/▼ sort indicators
- Click column header to sort; click again to reverse direction
- Directories always sorted before files
- `..` parent row at the top (hidden at filesystem root)
- Single-click on a directory row navigates into it
- Async directory loading via `Task::perform` (UI never blocks)
- 3 unit tests passing (list, dir-first, sort-by-size)

## What's Next (Slice 2: Navigation)
- Address bar (editable path input)
- Breadcrumb trail
- Back / forward history
- Drive selector (list available drives on Windows)

## Blockers
None

## Session Notes
- `SortColumn` / `SortDirection` live in `filescout-core` to avoid circular deps
- `file_list_view` emits `FileListMessage`; caller maps it with `.map(Message::FileList)`
- `button::text` style in iced 0.14 gives transparent background — good for nav rows
- `tokio::task::spawn_blocking` keeps `list_directory` sync-safe inside async context
