# FileScout Status

## Current Slice
**Polish Phase** — Complete

## Last Completed
Polish round: dynamic window title (shows current dir path), keyboard shortcuts help overlay (F1), address bar dropdown autocomplete — 2026-03-15

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
- **Copy & Move** — F5 copies selected item(s) to other pane's current dir; F6 moves; both async with progress overlay; batch-aware (operates on entire selection); "Copy to Other Pane" / "Move to Other Pane" in command palette; cross-device fallback (copy+delete)
- **Multi-Selection** — Ctrl+click toggles; Shift+click range-selects from anchor; Ctrl+A selects all; Shift+↑/↓ extends selection; modifier state tracked via `ModifiersChanged` subscription event; selection count shown in pane status bar; `SelectionState` in `pane.rs` holds items/focus/anchor; F8 delete / F5 copy / F6 move operate on all selected items
- **Bookmarks / Favorites** — Ctrl+D bookmarks current directory; Ctrl+B opens overlay; click navigates active pane; ✕ removes; persisted in `AppConfig.bookmarks` (same `config.toml`); "Open Bookmarks" / "Bookmark This Directory" in command palette; `Bookmark`/`Bookmarks` in `filescout-core/src/bookmarks.rs`; overlay in `filescout-app/src/bookmarks.rs`; 161 unit tests total (54 app + 104 core + 3 ui), all passing, clippy clean
- **File Watcher** — `watcher.rs` in `filescout-app/src/`; `WatcherRecipe` implements `iced::advanced::subscription::Recipe`; `watch_directories(paths)` returns `Subscription<PathBuf>`; identity derived from sorted+deduped paths so iced restarts watcher on navigation; `iced = { features = ["advanced"] }` required; return type uses `iced::futures::stream::BoxStream<'static, PathBuf>`; `Message::DirectoryChanged(PathBuf)` refreshes all matching tabs; blocked while `copy_move_label.is_some()`
- **Properties & Permissions** — Alt+Enter or command palette "File Properties"; `properties.rs` in both crates; `FileProperties` struct (name, path, is_dir, size_bytes, created/modified/accessed, read_only/hidden/system/archive); `load_file_properties(path)` blocking; `compute_dir_size(path)` async via walkdir; `format_size(bytes)` helper; Windows attributes via `std::os::windows::fs::MetadataExt::file_attributes()`; `PropertiesState { props, dir_size }` + `PropertiesMessage { DirSizeComputed, Close }`; modal overlay with dimmed backdrop
- **Archive Support** — `archive.rs` in `filescout-core/src/`; `is_zip(path)`, `list_archive_dir(zip_path, inner_dir)`, `extract_archive(zip_path, dest)`, `extract_entry(zip_path, inner_path, dest)`; `ArchiveContext { zip_path, inner_dir }` in `pane.rs`; `TabState.archive_context: Option<ArchiveContext>`; `PaneMessage::ArchiveDirLoaded`; `load_archive_for` helper; Enter on .zip enters archive; Enter on virtual dir navigates within archive; `..` exits or goes up within archive; `archive_context = None` on all real-navigation paths (Back/Forward/Submit/Breadcrumb/Drive/NavigateToFile); `TabBar::New` opens at zip's parent when in archive; Write ops (rename/delete/copy/move/batch-rename) disabled in archive; `DirectoryChanged` skips archive tabs; F7 / "Extract Archive" in command palette extracts selection or whole archive to other pane; status bar shows `Archive: name.zip[/inner/dir]`; 184 tests total, all passing

- **Dynamic Window Title** — title bar shows `"C:\current\path — FileScout"`, updates on pane/tab switch and navigation
- **Keyboard Shortcuts Help** — F1 opens categorized overlay listing all keybindings (7 categories); Esc/F1 closes; "Keyboard Shortcuts" in command palette; dimmed backdrop
- **Address Bar Dropdown Autocomplete** — as user types in address bar, matching subdirectory suggestions appear in a dropdown below the input; click to navigate; `path_suggestions(partial, max)` in `filescout-core/src/ops.rs`; suggestions clear on navigation

## What's Next (Slice 17: AI Features — Optional)
- Natural language search, smart organization suggestions, AI-assisted rename

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
- `copy_item(src, dest_dir)` / `move_item(src, dest_dir)` in `filescout-core/src/ops.rs` — recursive dir copy via `walkdir`; move tries `rename` first, falls back to copy+delete for cross-device
- `SelectionState` in `filescout-app/src/pane.rs` — `items: HashSet<PathBuf>`, `focus: Option<PathBuf>`, `anchor: Option<PathBuf>`; methods: `single_select`, `ctrl_click`, `shift_click`, `select_all`, `extend_to`, `clear`
- `TabState.selection: SelectionState` replaces `selected: Option<PathBuf>`; `selection.focus()` used for rename/preview/Enter-navigate; `selection.items()` used for batch delete/copy/move
- `FileScout.current_modifiers: keyboard::Modifiers` updated via `Message::ModifiersChanged`; read in `update_pane` before the mutable borrow to guide click routing (single/ctrl/shift)
- `file_list_view` accepts `selection: &HashSet<PathBuf>` instead of `selected: Option<&Path>`; rows highlighted when their path is in the set
- `PaneMessage::SelectAll` / `ExtendSelectionDown` / `ExtendSelectionUp`; `ActiveAction::SelectAll` / `ExtendSelectionUp/Down`; Ctrl+A / Shift+↑↓ in route_key
- Selection count status bar shown at bottom of pane when ≥1 item selected
- `PaneId::other()` returns the opposite pane ID
- `FileScout.copy_move_label: Option<String>` drives the progress overlay during in-flight copy/move
- `Message::CopyToOtherPane` / `MoveToOtherPane` routed to `start_copy()` / `start_move()`
- `Message::CopyMoveDone` carries `dest_pane/tab/dir` and optional `src_refresh` (pane/tab/dir) for moves
- `copy_move_progress_overlay(label)` in main.rs — centered card with dimmed backdrop, same stack pattern as palette/search
- Copy/move blocked while `copy_move_label.is_some()` to prevent concurrent operations
- `ThemePreference::toggled()` returns the opposite theme; `is_dark()` helper
- `FileScout.theme: ThemePreference` drives `fn theme(&self) -> Theme`; `Message::ConfigLoaded(AppConfig)` applies on startup; `Message::ToggleTheme` flips + persists; `Message::Noop` is the fire-and-forget sink
- `preview_panel.rs` in `filescout-ui/src/` — `preview_panel_view<M>(content)` generic over message type
- `PaneState.show_preview: bool` per pane; `TabState.preview: PreviewContent` per tab
- `PaneMessage::TogglePreview` and `PaneMessage::PreviewLoaded(tab_idx, content)` for async results
- `load_preview_for()` in main.rs — free fn mirroring `load_directory_for` pattern
- Preview triggered on `FileListMessage::Select` when `show_preview` is true
- `image` feature added to `iced` in workspace `Cargo.toml`; `Handle::from_path` used (lazy decode at render time)
