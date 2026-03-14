# FileScout Status

## Current Slice
**Slice 4: Tabs** — Complete

## Last Completed
Multiple tabs per pane with open/close/switch — 2026-03-14

## What's Working
- Cargo workspace builds and passes clippy
- Directory listing with scrollable view
- Column headers: Name / Size / Modified with ▲/▼ sort indicators
- Click column header to sort; click again to reverse direction
- Directories always sorted before files
- `..` parent row at the top (hidden at filesystem root)
- Single-click on a directory row navigates into it
- Async directory loading via `Task::perform` (UI never blocks)
- **Back / Forward buttons** — enabled when history allows, disabled otherwise
- **Address bar text input** — always editable; Enter navigates to typed path
- **Breadcrumb trail** — clickable path segments below address bar
- **Drive selector** — one button per available Windows drive (A-Z probed on startup)
- Navigation history: `History` struct in `filescout-core`, 9 unit tests
- **Dual pane** — two independent panes side by side, each with own history/state
- **Active pane indicator** — bordered box highlights the active pane
- Clicking any element in a pane makes it the active pane automatically
- **Tabs** — multiple tabs per pane, each with independent navigation state
- **Tab bar** — shows all open tabs with titles, active tab highlighted (primary button style)
- **New tab** (`+` button) — opens a new tab at the current directory
- **Close tab** (`×` button) — disabled on last tab; adjusts active index correctly
- **Tab switching** — click any tab to activate it
- 32 unit tests total, all passing, clippy clean

## What's Next (Slice 5: File Operations)
- Copy, move, delete (to trash), rename with undo/redo

## Blockers
None

## Session Notes
- `History` struct lives in `filescout-core/src/navigation.rs` — pure logic, no iced dep
- `address_bar_view()` in `filescout-ui/src/address_bar.rs` takes `address_text: &str` and the path for breadcrumbs separately (text input and breadcrumbs can diverge mid-edit)
- History is updated in the message handler (Navigate/Back/Forward), NOT in `DirectoryLoaded`
- `list_drives()` probes A:\ through Z:\ with `.exists()` — fast enough for startup, no Win32 dep needed
- `button` without `.on_press()` renders as disabled in iced — used for Back/Forward when at history bounds, and for the × close button when only one tab remains
- `SortColumn` / `SortDirection` live in `filescout-core` to avoid circular deps
- `tokio::task::spawn_blocking` keeps sync FS calls safe inside async context
- `PaneId` is `Copy` — safe to capture in move closures for iced's `.map()` calls
- `Message::Pane(id, msg)` sets `self.active = id` before dispatching — any interaction makes a pane active
- `container::bordered_box` used for active pane border in iced 0.14 dark theme
- `pane.rs` module in `filescout-app` holds `PaneId`, `PaneState`, `PaneMessage`, `TabState` — keeps main.rs focused
- `PaneMessage::DirectoryLoaded` carries a `tab_idx: usize` so async results land in the correct tab
- `tab_bar_view()` takes `Vec<String>` by value (not a borrow) to return `Element<'static, _>` — avoids lifetime issues with locally-computed tab titles
- `TabState::title()` returns the last path component, or the full display path for drive roots
- `PaneState::close_tab()` adjusts `active_tab` to stay valid: decrements if active > removed index, clamps if at end
