# FileScout Status

## Current Slice
**Slice 3: Dual Pane** — Complete

## Last Completed
Dual-pane layout with independent navigation and active pane indicator — 2026-03-14

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
- 20 unit tests total, all passing, clippy clean

## What's Next (Slice 4: Tabs)
- Multiple tabs per pane
- Tab bar with open/close, tab switching
- Each tab has its own navigation state

## Blockers
None

## Session Notes
- `History` struct lives in `filescout-core/src/navigation.rs` — pure logic, no iced dep
- `address_bar_view()` in `filescout-ui/src/address_bar.rs` takes `address_text: &str` and the path for breadcrumbs separately (text input and breadcrumbs can diverge mid-edit)
- History is updated in the message handler (Navigate/Back/Forward), NOT in `DirectoryLoaded`
- `list_drives()` probes A:\ through Z:\ with `.exists()` — fast enough for startup, no Win32 dep needed
- `button` without `.on_press()` renders as disabled in iced — used for Back/Forward when at history bounds
- `SortColumn` / `SortDirection` live in `filescout-core` to avoid circular deps
- `tokio::task::spawn_blocking` keeps sync FS calls safe inside async context
- `PaneId` is `Copy` — safe to capture in move closures for iced's `.map()` calls
- `Message::Pane(id, msg)` sets `self.active = id` before dispatching — any interaction makes a pane active
- `container::bordered_box` used for active pane border in iced 0.14 dark theme
- `pane.rs` module in `filescout-app` holds `PaneId`, `PaneState`, `PaneMessage` — keeps main.rs focused
