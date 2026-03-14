# FileScout Status

## Current Slice
**Slice 5: File Operations** — Complete

## Last Completed
File selection, inline rename, delete-to-trash, undo — 2026-03-14

## What's Working
- Cargo workspace builds and passes clippy
- Directory listing with scrollable view
- Column headers: Name / Size / Modified with ▲/▼ sort indicators
- Click column header to sort; click again to reverse direction
- Directories always sorted before files
- `..` parent row navigates to parent on click
- Async directory loading via `Task::perform` (UI never blocks)
- **Back / Forward buttons** — enabled when history allows, disabled otherwise
- **Address bar text input** — always editable; Enter navigates to typed path
- **Breadcrumb trail** — clickable path segments below address bar
- **Drive selector** — one button per available Windows drive (A-Z probed on startup)
- Navigation history: `History` struct in `filescout-core`
- **Dual pane** — two independent panes side by side, each with own history/state
- **Active pane indicator** — bordered box highlights the active pane
- **Tabs** — multiple tabs per pane, each with independent navigation state
- **Tab bar** — `+` opens new tab, `×` closes, active tab highlighted
- **Selection** — single-click any row to select (highlighted with primary button style)
- **Navigate into dir** — Enter key navigates into the selected directory
- **Rename** — F2 opens inline text_input in the file row; Enter commits, Esc cancels
- **Delete to trash** — F8 moves selected item to Recycle Bin (async); refreshes listing
- **Undo rename** — Ctrl+Z reverses the last rename (LIFO stack, capped at 50)
- **File name validation** — rejects empty names, invalid characters, Windows reserved names
- 45 unit tests total, all passing, clippy clean

## What's Next (Slice 6: Command Palette)
- Fuzzy-matched command search (Ctrl+Shift+P)

## Blockers
None

## Session Notes
- `History` struct lives in `filescout-core/src/navigation.rs` — pure logic, no iced dep
- `address_bar_view()` in `filescout-ui/src/address_bar.rs` takes `address_text: &str` and the path for breadcrumbs separately
- History is updated in the message handler (Navigate/Back/Forward), NOT in `DirectoryLoaded`
- `list_drives()` probes A:\ through Z:\ with `.exists()` — fast enough for startup, no Win32 dep needed
- `button` without `.on_press()` renders as disabled in iced — used for Back/Forward and × close when last tab
- `SortColumn` / `SortDirection` live in `filescout-core` to avoid circular deps
- `tokio::task::spawn_blocking` keeps sync FS calls safe inside async context
- `PaneId` is `Copy` — safe to capture in move closures for iced's `.map()` calls
- `Message::Pane(id, msg)` sets `self.active = id` before dispatching
- `container::bordered_box` used for active pane border in iced 0.14 dark theme
- `pane.rs` holds `PaneId`, `PaneState`, `PaneMessage`, `TabState` — keeps main.rs focused
- `PaneMessage::DirectoryLoaded` carries a `tab_idx` so async results land in the correct tab
- `tab_bar_view()` takes `Vec<String>` by value → `Element<'static, _>` to avoid lifetime issues
- `TabState::title()` returns last path component, or full path at drive roots
- `PaneState::close_tab()` adjusts `active_tab`: decrements if active > removed, clamps if at end
- `keyboard::on_key_press` does NOT exist in iced 0.14 — use `event::listen_with` instead
- `text_input::Id` and `text_input::focus` do NOT exist in iced 0.14 — use `iced::widget::Id`; auto-focus not available via `Task`
- `iced::widget::Id::new(str)` is the correct widget ID type in iced 0.14
- `PaneMessage::RenameDone` carries `old_path` for pushing to undo stack after async success
- `validate_file_name` in ops.rs guards against empty, invalid chars, and Windows reserved names (CON, PRN, etc.)
- Enter key routes to CommitRename OR NavigateSelected based on `tab.renaming.is_some()` check in handler
