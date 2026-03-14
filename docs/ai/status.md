# FileScout Status

## Current Slice
**Slice 10: Theming** — Complete

## Last Completed
Dark/light theme toggle (Ctrl+T) with TOML persistence — 2026-03-14

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
- **Command Palette** — Ctrl+Shift+P opens overlay; fuzzy-search via SkimMatcherV2; arrow keys + Enter to execute; Esc closes; mouse click on any row executes; dimmed backdrop using `stack` overlay
- **File Search** — Ctrl+F opens search overlay; fuzzy recursive search via `walkdir` + `fuzzy-matcher`; optional extension filter; arrow keys navigate results; Enter / click navigates active pane to result and selects the file; Esc closes; "Search Files" also in command palette
- **File Preview** — F3 toggles per-pane preview panel (60/40 split); text files show first 16 KB in monospace scrollable; images rendered via `iced::widget::image` (`Handle::from_path`); other files show type + size metadata; preview loads async on selection; "Toggle Preview" in command palette; `iced` feature `image` enabled
- **Batch Rename** — Ctrl+R opens modal overlay; pattern tokens `{name}` `{ext}` `{n}` `{n:NN}`; optional extension filter; live preview table (original → new name); validation errors shown inline; Enter/Apply executes renames async; each successful rename pushed to undo stack; "Batch Rename" in command palette
- **Theming** — Ctrl+T toggles between Dark and Light theme; persisted to `%APPDATA%\filescout\config.toml` (TOML via `serde`/`toml`); loaded async at startup; "Toggle Theme" in command palette; `Message::Noop` sink for fire-and-forget async tasks
- 124 unit tests total (39 app + 82 core + 3 ui), all passing, clippy clean

## What's Next (Slice 11: AI Features)
- Natural language search, smart organization suggestions

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
- `event::listen_with` in iced 0.14 takes a `fn` pointer (no captures) — all key events are forwarded as `Message::KeyPressed(key, modifiers)` and routed in `update()` based on current state
- `command_palette.rs` in `filescout-app/src/` — self-contained module with `PaletteState`, `CommandId`, `ALL_COMMANDS`, and `command_palette_view()`
- `iced::widget::stack([base, overlay])` used for the dimmed backdrop overlay
- `PaletteState::filtered_commands()` uses `SkimMatcherV2` from `fuzzy-matcher` crate
- Palette query change resets `selected` to 0 in the `handle_palette` update handler
- `search.rs` in `filescout-app/src/` — `SearchState`, `SearchMessage`, `search_view()` overlays same way as palette
- `search.rs` in `filescout-core/src/` — `search_files(root, query, ext_filter, max)` using `walkdir` + `SkimMatcherV2`
- `PaneMessage::NavigateToFile(PathBuf)` navigates to parent dir and sets `tab.selected = Some(path)` so file is highlighted after load
- `spawn_search_task()` in main.rs — free fn taking `&SearchState`, spawning blocking search and mapping result to `Message::Search(SearchMessage::ResultsLoaded(...))`
- Search overlay is second priority in `route_key` (palette > search > pane keys)
- `preview.rs` in `filescout-core/src/` — `PreviewContent` enum, `load_preview(path)`, `is_text_extension()`, `is_image_extension()` helpers
- `batch_rename.rs` in `filescout-core/src/` — `apply_pattern(pattern, name, ext, index)`, `preview_batch_rename(entries, pattern, ext_filter)`, `BatchRenamePreview` struct
- `batch_rename.rs` in `filescout-app/src/` — `BatchRenameState`, `BatchRenameMessage`, `batch_rename_view()`; opens via Ctrl+R or command palette; entries snapshot taken at open time; previews recomputed on every keystroke; `valid_renames()` returns only error-free pairs
- `PaneMessage::BatchRenameDone(tab_idx, refresh_path, Vec<(PathBuf, Result<PathBuf, String>)>)` — each rename result pushed individually to undo stack
- Pattern tokens: `{name}` (file stem), `{ext}` (extension without dot), `{n}` (1-based counter), `{n:NN}` (zero-padded N digits)
- `config.rs` in `filescout-core/src/` — `ThemePreference` enum (Dark/Light), `AppConfig { theme }`, `load_config()` (returns default on missing/corrupt file), `save_config(&config)` (creates dirs); config path: `dirs::config_dir().join("filescout/config.toml")`
- `ThemePreference::toggled()` returns the opposite theme; `is_dark()` helper
- `FileScout.theme: ThemePreference` drives `fn theme(&self) -> Theme`; `Message::ConfigLoaded(AppConfig)` applies on startup; `Message::ToggleTheme` flips + persists; `Message::Noop` is the fire-and-forget sink
- `preview_panel.rs` in `filescout-ui/src/` — `preview_panel_view<M>(content)` generic over message type
- `PaneState.show_preview: bool` per pane; `TabState.preview: PreviewContent` per tab
- `PaneMessage::TogglePreview` and `PaneMessage::PreviewLoaded(tab_idx, content)` for async results
- `load_preview_for()` in main.rs — free fn mirroring `load_directory_for` pattern
- Preview triggered on `FileListMessage::Select` when `show_preview` is true
- `image` feature added to `iced` in workspace `Cargo.toml`; `Handle::from_path` used (lazy decode at render time)
