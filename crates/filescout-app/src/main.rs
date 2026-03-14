use std::env;
use std::path::PathBuf;

use filescout_core::config::{load_config, save_config, AppConfig, ThemePreference};
use filescout_core::fs::{list_directory, list_drives, sort_entries};
use filescout_core::ops::{
    copy_item, delete_to_trash, move_item, rename_file, undo_rename, validate_file_name, UndoAction,
};
use filescout_core::preview::{load_preview, PreviewContent};
use filescout_core::search::search_files;
use filescout_core::SortDirection;
use filescout_ui::address_bar::{address_bar_view, AddressBarMessage};
use filescout_ui::file_list::{file_list_view, FileListMessage};
use filescout_ui::preview_panel::preview_panel_view;
use filescout_ui::tab_bar::{tab_bar_view, TabBarMessage};
use iced::widget::{column, container, row, stack, text};
use iced::{event, keyboard, Background, Color, Element, Event, Length, Size, Subscription, Task, Theme};
use tracing_subscriber::EnvFilter;

mod batch_rename;
mod command_palette;
mod pane;
mod search;

use batch_rename::{batch_rename_view, BatchRenameMessage, BatchRenameState};
use command_palette::{command_palette_view, CommandId, PaletteMessage, PaletteState};
use pane::{PaneId, PaneMessage, PaneState};
use search::{search_view, SearchMessage, SearchState, MAX_SEARCH_RESULTS};

fn main() -> iced::Result {
    tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::from_default_env())
        .init();

    iced::application(FileScout::new, FileScout::update, FileScout::view)
        .title("FileScout")
        .theme(FileScout::theme)
        .subscription(FileScout::subscription)
        .window_size(Size::new(1400.0, 800.0))
        .centered()
        .run()
}

struct FileScout {
    panes: [PaneState; 2],
    active: PaneId,
    drives: Vec<PathBuf>,
    palette: Option<PaletteState>,
    search: Option<SearchState>,
    batch_rename: Option<BatchRenameState>,
    theme: ThemePreference,
    /// Label shown in the progress overlay while a copy/move is in flight.
    copy_move_label: Option<String>,
    /// Currently held modifier keys — used to detect Ctrl/Shift during mouse clicks.
    current_modifiers: keyboard::Modifiers,
}

/// Keyboard actions that apply to whichever pane is currently active.
#[derive(Debug, Clone)]
enum ActiveAction {
    StartRename,         // F2
    Delete,              // F8
    CommitRename,        // Enter (commit rename OR navigate into selected dir — routed in update_pane)
    CancelEdit,          // Esc
    Undo,                // Ctrl+Z
    TogglePreview,       // F3
    SelectAll,           // Ctrl+A
    ExtendSelectionDown, // Shift+Down
    ExtendSelectionUp,   // Shift+Up
}

#[derive(Debug, Clone)]
enum Message {
    Pane(PaneId, PaneMessage),
    DrivesLoaded(Vec<PathBuf>),
    /// A keyboard shortcut directed at the currently active pane.
    ActivePaneAction(ActiveAction),
    /// A message from the command palette UI.
    Palette(PaletteMessage),
    /// A message from the search overlay UI or background task.
    Search(SearchMessage),
    /// A message from the batch rename overlay.
    BatchRename(BatchRenameMessage),
    /// Config loaded from disk at startup.
    ConfigLoaded(AppConfig),
    /// Toggle between dark and light theme.
    ToggleTheme,
    /// No-op sink for fire-and-forget async tasks.
    Noop,
    /// Raw key press forwarded from the subscription for stateful routing.
    KeyPressed(keyboard::Key, keyboard::Modifiers),
    /// Modifier keys changed (used to track Ctrl/Shift for click handling).
    ModifiersChanged(keyboard::Modifiers),
    /// Copy the active pane's selected item to the other pane's current directory (F5).
    CopyToOtherPane,
    /// Move the active pane's selected item to the other pane's current directory (F6).
    MoveToOtherPane,
    /// Result of an async copy or move operation.
    CopyMoveDone {
        dest_pane: PaneId,
        dest_tab: usize,
        dest_dir: PathBuf,
        /// Source pane/tab/dir to refresh after a move; `None` for copy.
        src_refresh: Option<(PaneId, usize, PathBuf)>,
        result: Result<(), String>,
    },
}

impl FileScout {
    fn new() -> (Self, Task<Message>) {
        let cwd = env::current_dir().unwrap_or_else(|_| PathBuf::from("C:\\"));

        let app = Self {
            panes: [PaneState::new(cwd.clone()), PaneState::new(cwd.clone())],
            active: PaneId::Left,
            drives: Vec::new(),
            palette: None,
            search: None,
            batch_rename: None,
            theme: ThemePreference::Dark,
            copy_move_label: None,
            current_modifiers: keyboard::Modifiers::default(),
        };

        let load_left = load_directory_for(PaneId::Left, 0, cwd.clone());
        let load_right = load_directory_for(PaneId::Right, 0, cwd);
        let load_drvs = Task::perform(
            async { tokio::task::spawn_blocking(list_drives).await.unwrap_or_default() },
            Message::DrivesLoaded,
        );
        let load_cfg = Task::perform(
            async { tokio::task::spawn_blocking(load_config).await.unwrap_or_default() },
            Message::ConfigLoaded,
        );

        (app, Task::batch([load_left, load_right, load_drvs, load_cfg]))
    }

    fn subscription(_state: &Self) -> Subscription<Message> {
        event::listen_with(|ev, _status, _window| match ev {
            Event::Keyboard(keyboard::Event::KeyPressed { key, modifiers, .. }) => {
                Some(Message::KeyPressed(key, modifiers))
            }
            Event::Keyboard(keyboard::Event::ModifiersChanged(mods)) => {
                Some(Message::ModifiersChanged(mods))
            }
            _ => None,
        })
    }

    fn update(&mut self, message: Message) -> Task<Message> {
        match message {
            Message::DrivesLoaded(drives) => {
                self.drives = drives;
                Task::none()
            }
            Message::Pane(id, msg) => {
                self.active = id;
                self.update_pane(id, msg)
            }
            Message::ActivePaneAction(action) => {
                let id = self.active;
                let msg = match action {
                    ActiveAction::StartRename => PaneMessage::StartRename,
                    ActiveAction::Delete => PaneMessage::DeleteSelected,
                    ActiveAction::CommitRename => PaneMessage::CommitRename,
                    ActiveAction::CancelEdit => PaneMessage::CancelRename,
                    ActiveAction::Undo => PaneMessage::Undo,
                    ActiveAction::TogglePreview => PaneMessage::TogglePreview,
                    ActiveAction::SelectAll => PaneMessage::SelectAll,
                    ActiveAction::ExtendSelectionDown => PaneMessage::ExtendSelectionDown,
                    ActiveAction::ExtendSelectionUp => PaneMessage::ExtendSelectionUp,
                };
                self.update_pane(id, msg)
            }
            Message::Palette(msg) => self.handle_palette(msg),
            Message::Search(msg) => self.handle_search(msg),
            Message::BatchRename(msg) => self.handle_batch_rename(msg),
            Message::ConfigLoaded(cfg) => {
                self.theme = cfg.theme;
                Task::none()
            }
            Message::ToggleTheme => {
                self.theme = self.theme.toggled();
                let cfg = AppConfig { theme: self.theme };
                Task::perform(
                    async move {
                        tokio::task::spawn_blocking(move || {
                            let _ = save_config(&cfg);
                        })
                        .await
                        .ok();
                    },
                    |_| Message::Noop,
                )
            }
            Message::Noop => Task::none(),
            Message::KeyPressed(key, modifiers) => self.route_key(key, modifiers),
            Message::ModifiersChanged(mods) => {
                self.current_modifiers = mods;
                Task::none()
            }
            Message::CopyToOtherPane => self.start_copy(),
            Message::MoveToOtherPane => self.start_move(),
            Message::CopyMoveDone { dest_pane, dest_tab, dest_dir, src_refresh, result } => {
                self.copy_move_label = None;
                match result {
                    Err(e) => {
                        self.panes[self.active.index()].active_mut().error = Some(e);
                        Task::none()
                    }
                    Ok(()) => {
                        let dest_task = load_directory_for(dest_pane, dest_tab, dest_dir);
                        if let Some((src_pane, src_tab, src_dir)) = src_refresh {
                            if src_tab < self.panes[src_pane.index()].tabs.len() {
                                self.panes[src_pane.index()].tabs[src_tab].selection.clear();
                            }
                            Task::batch([dest_task, load_directory_for(src_pane, src_tab, src_dir)])
                        } else {
                            dest_task
                        }
                    }
                }
            }
        }
    }

    fn start_copy(&mut self) -> Task<Message> {
        if self.copy_move_label.is_some() {
            return Task::none();
        }
        let src_pane = self.active;
        let dest_pane = src_pane.other();
        let paths: Vec<PathBuf> =
            self.panes[src_pane.index()].active().selection.items().iter().cloned().collect();
        if paths.is_empty() {
            return Task::none();
        }
        let dest_dir = self.panes[dest_pane.index()].active().current_path.clone();
        let dest_tab = self.panes[dest_pane.index()].active_tab;
        let label = if paths.len() == 1 {
            format!(
                "Copying '{}'  →  {}…",
                paths[0].file_name().unwrap_or_default().to_string_lossy(),
                dest_dir.display()
            )
        } else {
            format!("Copying {} items  →  {}…", paths.len(), dest_dir.display())
        };
        self.copy_move_label = Some(label);
        let dest_dir2 = dest_dir.clone();
        Task::perform(
            async move {
                tokio::task::spawn_blocking(move || {
                    let errors: Vec<String> = paths
                        .into_iter()
                        .filter_map(|p| copy_item(p, dest_dir.clone()).err().map(|e| e.to_string()))
                        .collect();
                    if errors.is_empty() { Ok(()) } else { Err(errors.join("; ")) }
                })
                .await
                .unwrap_or_else(|e| Err(e.to_string()))
            },
            move |result| Message::CopyMoveDone {
                dest_pane,
                dest_tab,
                dest_dir: dest_dir2,
                src_refresh: None,
                result,
            },
        )
    }

    fn start_move(&mut self) -> Task<Message> {
        if self.copy_move_label.is_some() {
            return Task::none();
        }
        let src_pane = self.active;
        let dest_pane = src_pane.other();
        let paths: Vec<PathBuf> =
            self.panes[src_pane.index()].active().selection.items().iter().cloned().collect();
        if paths.is_empty() {
            return Task::none();
        }
        let dest_dir = self.panes[dest_pane.index()].active().current_path.clone();
        let dest_tab = self.panes[dest_pane.index()].active_tab;
        let src_tab = self.panes[src_pane.index()].active_tab;
        let src_dir = self.panes[src_pane.index()].active().current_path.clone();
        let label = if paths.len() == 1 {
            format!(
                "Moving '{}'  →  {}…",
                paths[0].file_name().unwrap_or_default().to_string_lossy(),
                dest_dir.display()
            )
        } else {
            format!("Moving {} items  →  {}…", paths.len(), dest_dir.display())
        };
        self.copy_move_label = Some(label);
        let dest_dir2 = dest_dir.clone();
        Task::perform(
            async move {
                tokio::task::spawn_blocking(move || {
                    let errors: Vec<String> = paths
                        .into_iter()
                        .filter_map(|p| move_item(p, dest_dir.clone()).err().map(|e| e.to_string()))
                        .collect();
                    if errors.is_empty() { Ok(()) } else { Err(errors.join("; ")) }
                })
                .await
                .unwrap_or_else(|e| Err(e.to_string()))
            },
            move |result| Message::CopyMoveDone {
                dest_pane,
                dest_tab,
                dest_dir: dest_dir2,
                src_refresh: Some((src_pane, src_tab, src_dir)),
                result,
            },
        )
    }

    fn route_key(&mut self, key: keyboard::Key, modifiers: keyboard::Modifiers) -> Task<Message> {
        use keyboard::key::Named;

        // Command palette takes highest priority.
        if self.palette.is_some() {
            // Character input is handled by the text_input widget; route only navigation keys.
            match key.as_ref() {
                keyboard::Key::Named(Named::ArrowUp) => self.handle_palette(PaletteMessage::MoveUp),
                keyboard::Key::Named(Named::ArrowDown) => self.handle_palette(PaletteMessage::MoveDown),
                keyboard::Key::Named(Named::Enter) => self.handle_palette(PaletteMessage::Execute),
                keyboard::Key::Named(Named::Escape) => self.handle_palette(PaletteMessage::Close),
                keyboard::Key::Character("p") if modifiers.control() && modifiers.shift() => {
                    self.handle_palette(PaletteMessage::Close)
                }
                _ => Task::none(),
            }
        } else if self.search.is_some() {
            // Search overlay is open: route navigation keys, let text_input handle typing.
            match key.as_ref() {
                keyboard::Key::Named(Named::ArrowUp) => self.handle_search(SearchMessage::MoveUp),
                keyboard::Key::Named(Named::ArrowDown) => self.handle_search(SearchMessage::MoveDown),
                keyboard::Key::Named(Named::Enter) => self.handle_search(SearchMessage::Navigate),
                keyboard::Key::Named(Named::Escape) => self.handle_search(SearchMessage::Close),
                keyboard::Key::Character("f") if modifiers.control() => {
                    self.handle_search(SearchMessage::Close)
                }
                _ => Task::none(),
            }
        } else if self.batch_rename.is_some() {
            // Batch rename overlay is open: route Esc/Enter, let text_input handle typing.
            match key.as_ref() {
                keyboard::Key::Named(Named::Enter) => {
                    self.handle_batch_rename(BatchRenameMessage::Apply)
                }
                keyboard::Key::Named(Named::Escape) => {
                    self.handle_batch_rename(BatchRenameMessage::Close)
                }
                keyboard::Key::Character("r") if modifiers.control() => {
                    self.handle_batch_rename(BatchRenameMessage::Close)
                }
                _ => Task::none(),
            }
        } else {
            match key.as_ref() {
                keyboard::Key::Character("p") if modifiers.control() && modifiers.shift() => {
                    self.palette = Some(PaletteState::new());
                    Task::none()
                }
                keyboard::Key::Character("f") if modifiers.control() => {
                    let root = self.panes[self.active.index()].active().current_path.clone();
                    self.search = Some(SearchState::new(root));
                    Task::none()
                }
                keyboard::Key::Character("r") if modifiers.control() => {
                    let entries = self.panes[self.active.index()].active().entries.clone();
                    self.batch_rename = Some(BatchRenameState::new(&entries));
                    Task::none()
                }
                keyboard::Key::Character("t") if modifiers.control() => {
                    self.update(Message::ToggleTheme)
                }
                keyboard::Key::Named(Named::F5) => self.update(Message::CopyToOtherPane),
                keyboard::Key::Named(Named::F6) => self.update(Message::MoveToOtherPane),
                keyboard::Key::Character("a") if modifiers.control() => {
                    self.update(Message::ActivePaneAction(ActiveAction::SelectAll))
                }
                keyboard::Key::Named(Named::ArrowDown) if modifiers.shift() => {
                    self.update(Message::ActivePaneAction(ActiveAction::ExtendSelectionDown))
                }
                keyboard::Key::Named(Named::ArrowUp) if modifiers.shift() => {
                    self.update(Message::ActivePaneAction(ActiveAction::ExtendSelectionUp))
                }
                keyboard::Key::Named(Named::F2) => {
                    self.update(Message::ActivePaneAction(ActiveAction::StartRename))
                }
                keyboard::Key::Named(Named::F3) => {
                    self.update(Message::ActivePaneAction(ActiveAction::TogglePreview))
                }
                keyboard::Key::Named(Named::F8) => {
                    self.update(Message::ActivePaneAction(ActiveAction::Delete))
                }
                keyboard::Key::Named(Named::Enter) => {
                    self.update(Message::ActivePaneAction(ActiveAction::CommitRename))
                }
                keyboard::Key::Named(Named::Escape) => {
                    self.update(Message::ActivePaneAction(ActiveAction::CancelEdit))
                }
                keyboard::Key::Character("z") if modifiers.control() => {
                    self.update(Message::ActivePaneAction(ActiveAction::Undo))
                }
                _ => Task::none(),
            }
        }
    }

    fn handle_palette(&mut self, msg: PaletteMessage) -> Task<Message> {
        match msg {
            PaletteMessage::QueryChanged(q) => {
                if let Some(ref mut p) = self.palette {
                    p.query = q;
                    p.selected = 0;
                }
                Task::none()
            }
            PaletteMessage::MoveUp => {
                if let Some(ref mut p) = self.palette {
                    p.move_up();
                }
                Task::none()
            }
            PaletteMessage::MoveDown => {
                if let Some(ref mut p) = self.palette {
                    let n = p.filtered_commands().len();
                    p.move_down(n);
                }
                Task::none()
            }
            PaletteMessage::Close => {
                self.palette = None;
                Task::none()
            }
            PaletteMessage::Execute => {
                let cmd_id = self.palette.as_ref().and_then(|p| p.selected_command());
                self.palette = None;
                self.execute_command(cmd_id)
            }
            PaletteMessage::ExecuteCommand(cmd_id) => {
                self.palette = None;
                self.execute_command(Some(cmd_id))
            }
        }
    }

    fn handle_search(&mut self, msg: SearchMessage) -> Task<Message> {
        match msg {
            SearchMessage::QueryChanged(q) => {
                if let Some(ref mut s) = self.search {
                    s.query = q;
                    s.selected = 0;
                    if s.should_search() {
                        s.loading = true;
                        return spawn_search_task(s);
                    }
                    s.results.clear();
                }
                Task::none()
            }
            SearchMessage::ExtFilterChanged(f) => {
                if let Some(ref mut s) = self.search {
                    s.ext_filter = f;
                    s.selected = 0;
                    if s.should_search() {
                        s.loading = true;
                        return spawn_search_task(s);
                    }
                    s.results.clear();
                }
                Task::none()
            }
            SearchMessage::ResultsLoaded(results) => {
                if let Some(ref mut s) = self.search {
                    s.results = results;
                    s.loading = false;
                    if s.selected >= s.results.len() {
                        s.selected = 0;
                    }
                }
                Task::none()
            }
            SearchMessage::MoveUp => {
                if let Some(ref mut s) = self.search {
                    s.move_up();
                }
                Task::none()
            }
            SearchMessage::MoveDown => {
                if let Some(ref mut s) = self.search {
                    s.move_down();
                }
                Task::none()
            }
            SearchMessage::Navigate => {
                let path = self.search.as_ref().and_then(|s| s.selected_path().cloned());
                self.search = None;
                self.navigate_to_file(path)
            }
            SearchMessage::NavigateTo(path) => {
                self.search = None;
                self.navigate_to_file(Some(path))
            }
            SearchMessage::Close => {
                self.search = None;
                Task::none()
            }
        }
    }

    fn handle_batch_rename(&mut self, msg: BatchRenameMessage) -> Task<Message> {
        match msg {
            BatchRenameMessage::PatternChanged(p) => {
                if let Some(ref mut s) = self.batch_rename {
                    s.set_pattern(p);
                }
                Task::none()
            }
            BatchRenameMessage::ExtFilterChanged(f) => {
                if let Some(ref mut s) = self.batch_rename {
                    s.set_ext_filter(f);
                }
                Task::none()
            }
            BatchRenameMessage::Close => {
                self.batch_rename = None;
                Task::none()
            }
            BatchRenameMessage::Apply => {
                let Some(ref state) = self.batch_rename else {
                    return Task::none();
                };
                let renames = state.valid_renames();
                if renames.is_empty() {
                    self.batch_rename = None;
                    return Task::none();
                }
                let id = self.active;
                let tab_idx = self.panes[id.index()].active_tab;
                let refresh_path = self.panes[id.index()].active().current_path.clone();
                self.batch_rename = None;
                Task::perform(
                    async move {
                        tokio::task::spawn_blocking(move || {
                            renames
                                .into_iter()
                                .map(|(old_path, new_name)| {
                                    let result = rename_file(old_path.clone(), new_name)
                                        .map_err(|e| e.to_string());
                                    (old_path, result)
                                })
                                .collect::<Vec<_>>()
                        })
                        .await
                        .unwrap_or_default()
                    },
                    move |results| {
                        Message::Pane(id, PaneMessage::BatchRenameDone(tab_idx, refresh_path, results))
                    },
                )
            }
        }
    }

    fn navigate_to_file(&mut self, path: Option<PathBuf>) -> Task<Message> {
        let Some(path) = path else { return Task::none() };
        let id = self.active;
        self.update_pane(id, PaneMessage::NavigateToFile(path))
    }

    fn execute_command(&mut self, cmd_id: Option<CommandId>) -> Task<Message> {
        let Some(cmd) = cmd_id else {
            return Task::none();
        };
        let id = self.active;
        let tab_idx = self.panes[id.index()].active_tab;
        match cmd {
            CommandId::NavigateBack => {
                self.update_pane(id, PaneMessage::AddressBar(AddressBarMessage::Back))
            }
            CommandId::NavigateForward => {
                self.update_pane(id, PaneMessage::AddressBar(AddressBarMessage::Forward))
            }
            CommandId::NewTab => self.update_pane(id, PaneMessage::TabBar(TabBarMessage::New)),
            CommandId::CloseTab => {
                self.update_pane(id, PaneMessage::TabBar(TabBarMessage::Close(tab_idx)))
            }
            CommandId::Rename => self.update_pane(id, PaneMessage::StartRename),
            CommandId::Delete => self.update_pane(id, PaneMessage::DeleteSelected),
            CommandId::Undo => self.update_pane(id, PaneMessage::Undo),
            CommandId::SearchFiles => {
                let root = self.panes[id.index()].active().current_path.clone();
                self.search = Some(SearchState::new(root));
                Task::none()
            }
            CommandId::TogglePreview => {
                self.update_pane(id, PaneMessage::TogglePreview)
            }
            CommandId::BatchRename => {
                let entries = self.panes[id.index()].active().entries.clone();
                self.batch_rename = Some(BatchRenameState::new(&entries));
                Task::none()
            }
            CommandId::ToggleTheme => self.update(Message::ToggleTheme),
            CommandId::CopyToOther => self.update(Message::CopyToOtherPane),
            CommandId::MoveToOther => self.update(Message::MoveToOtherPane),
        }
    }

    fn update_pane(&mut self, id: PaneId, msg: PaneMessage) -> Task<Message> {
        let current_mods = self.current_modifiers;
        let pane = &mut self.panes[id.index()];

        match msg {
            // --- Tab management ---
            PaneMessage::TabBar(TabBarMessage::Switch(idx)) => {
                pane.switch_tab(idx);
                Task::none()
            }

            PaneMessage::TabBar(TabBarMessage::Close(idx)) => {
                pane.close_tab(idx);
                Task::none()
            }

            PaneMessage::TabBar(TabBarMessage::New) => {
                let path = pane.active().current_path.clone();
                let tab_idx = pane.new_tab(path.clone());
                load_directory_for(id, tab_idx, path)
            }

            // --- Navigation ---
            PaneMessage::FileList(FileListMessage::Navigate(path)) => {
                let tab_idx = pane.active_tab;
                let tab = &mut pane.tabs[tab_idx];
                tab.history.navigate(path.clone());
                tab.address_text = path.display().to_string();
                tab.selection.clear();
                load_directory_for(id, tab_idx, path)
            }

            PaneMessage::FileList(FileListMessage::Select(path)) => {
                let tab_idx = pane.active_tab;
                let entries = pane.active().entries.clone();
                let tab = pane.active_mut();
                if current_mods.control() {
                    tab.selection.ctrl_click(path.clone());
                } else if current_mods.shift() {
                    tab.selection.shift_click(path.clone(), &entries);
                } else {
                    tab.selection.single_select(path.clone());
                }
                if pane.show_preview
                    && let Some(focus) = pane.active().selection.focus().map(|p| p.to_path_buf())
                {
                    pane.active_mut().preview = PreviewContent::Loading;
                    return load_preview_for(id, tab_idx, focus);
                }
                Task::none()
            }

            PaneMessage::NavigateSelected => {
                let tab_idx = pane.active_tab;
                let tab = &mut pane.tabs[tab_idx];
                // Ignore if rename is active — Enter commits the rename instead
                if tab.renaming.is_some() {
                    return Task::none();
                }
                if let Some(focus) = tab.selection.focus().map(|p| p.to_path_buf())
                    && focus.is_dir()
                {
                    tab.history.navigate(focus.clone());
                    tab.address_text = focus.display().to_string();
                    tab.selection.clear();
                    return load_directory_for(id, tab_idx, focus);
                }
                Task::none()
            }

            PaneMessage::NavigateToFile(path) => {
                let tab_idx = pane.active_tab;
                let tab = &mut pane.tabs[tab_idx];
                // Navigate to the parent directory; keep `path` as the pending selection.
                let parent = path.parent().map(|p| p.to_path_buf()).unwrap_or_else(|| path.clone());
                tab.history.navigate(parent.clone());
                tab.address_text = parent.display().to_string();
                tab.selection.single_select(path);
                load_directory_for(id, tab_idx, parent)
            }

            // --- Preview ---
            PaneMessage::TogglePreview => {
                pane.show_preview = !pane.show_preview;
                if pane.show_preview {
                    // Load preview for the focused file, if any.
                    let tab_idx = pane.active_tab;
                    if let Some(path) = pane.tabs[tab_idx].selection.focus().map(|p| p.to_path_buf()) {
                        pane.tabs[tab_idx].preview = PreviewContent::Loading;
                        return load_preview_for(id, tab_idx, path);
                    }
                } else {
                    // Clear preview to free memory when panel is hidden.
                    let tab_idx = pane.active_tab;
                    pane.tabs[tab_idx].preview = PreviewContent::None;
                }
                Task::none()
            }

            PaneMessage::PreviewLoaded(tab_idx, content) => {
                if tab_idx < pane.tabs.len() {
                    pane.tabs[tab_idx].preview = content;
                }
                Task::none()
            }

            // --- Sorting ---
            PaneMessage::FileList(FileListMessage::Sort(col)) => {
                let tab = pane.active_mut();
                if tab.sort_col == col {
                    tab.sort_dir = if tab.sort_dir == SortDirection::Asc {
                        SortDirection::Desc
                    } else {
                        SortDirection::Asc
                    };
                } else {
                    tab.sort_col = col;
                    tab.sort_dir = SortDirection::Asc;
                }
                sort_entries(&mut tab.entries, &tab.sort_col, &tab.sort_dir);
                Task::none()
            }

            // --- Address bar ---
            PaneMessage::AddressBar(AddressBarMessage::Back) => {
                let tab_idx = pane.active_tab;
                let tab = &mut pane.tabs[tab_idx];
                if let Some(path) = tab.history.back().cloned() {
                    tab.address_text = path.display().to_string();
                    tab.selection.clear();
                    load_directory_for(id, tab_idx, path)
                } else {
                    Task::none()
                }
            }

            PaneMessage::AddressBar(AddressBarMessage::Forward) => {
                let tab_idx = pane.active_tab;
                let tab = &mut pane.tabs[tab_idx];
                if let Some(path) = tab.history.forward().cloned() {
                    tab.address_text = path.display().to_string();
                    tab.selection.clear();
                    load_directory_for(id, tab_idx, path)
                } else {
                    Task::none()
                }
            }

            PaneMessage::AddressBar(AddressBarMessage::Edit(t)) => {
                pane.active_mut().address_text = t;
                Task::none()
            }

            PaneMessage::AddressBar(AddressBarMessage::Submit) => {
                let tab_idx = pane.active_tab;
                let tab = &mut pane.tabs[tab_idx];
                let path = PathBuf::from(&tab.address_text);
                tab.history.navigate(path.clone());
                tab.selection.clear();
                load_directory_for(id, tab_idx, path)
            }

            PaneMessage::AddressBar(AddressBarMessage::BreadcrumbNavigate(path)) => {
                let tab_idx = pane.active_tab;
                let tab = &mut pane.tabs[tab_idx];
                tab.history.navigate(path.clone());
                tab.address_text = path.display().to_string();
                tab.selection.clear();
                load_directory_for(id, tab_idx, path)
            }

            PaneMessage::AddressBar(AddressBarMessage::DriveSelected(path)) => {
                let tab_idx = pane.active_tab;
                let tab = &mut pane.tabs[tab_idx];
                tab.history.navigate(path.clone());
                tab.address_text = path.display().to_string();
                tab.selection.clear();
                load_directory_for(id, tab_idx, path)
            }

            // --- Directory load result ---
            PaneMessage::DirectoryLoaded(tab_idx, path, Ok(mut entries)) => {
                let tab = &mut pane.tabs[tab_idx];
                sort_entries(&mut entries, &tab.sort_col, &tab.sort_dir);
                tab.entries = entries;
                tab.current_path = path.clone();
                tab.address_text = path.display().to_string();
                tab.error = None;
                Task::none()
            }

            PaneMessage::DirectoryLoaded(tab_idx, _, Err(e)) => {
                pane.tabs[tab_idx].error = Some(e);
                Task::none()
            }

            // --- File operations ---
            PaneMessage::StartRename => {
                let tab = pane.active_mut();
                if let Some(focus) = tab.selection.focus().map(|p| p.to_path_buf()) {
                    let name = focus
                        .file_name()
                        .map(|n| n.to_string_lossy().into_owned())
                        .unwrap_or_default();
                    tab.renaming = Some((focus, name));
                }
                Task::none()
            }

            PaneMessage::FileList(FileListMessage::RenameEdit(text)) => {
                if let Some(ref mut r) = pane.active_mut().renaming {
                    r.1 = text;
                }
                Task::none()
            }

            PaneMessage::CommitRename => {
                let tab_idx = pane.active_tab;
                let tab = &mut pane.tabs[tab_idx];
                // If not renaming, try navigate-into-selected-dir (Enter dual purpose)
                if tab.renaming.is_none() {
                    let _ = tab;
                    return self.update_pane(id, PaneMessage::NavigateSelected);
                }
                let (old_path, new_name) = tab.renaming.take().unwrap();
                // Validate the new name before attempting rename
                if let Err(e) = validate_file_name(&new_name) {
                    tab.error = Some(e.to_string());
                    return Task::none();
                }
                let refresh_path = tab.current_path.clone();
                let old_path_for_msg = old_path.clone();
                Task::perform(
                    async move {
                        tokio::task::spawn_blocking(move || rename_file(old_path, new_name))
                            .await
                            .unwrap_or_else(|e| Err(anyhow::anyhow!(e)))
                    },
                    move |result| {
                        Message::Pane(
                            id,
                            PaneMessage::RenameDone(
                                tab_idx,
                                refresh_path,
                                old_path_for_msg,
                                result.map_err(|e| e.to_string()),
                            ),
                        )
                    },
                )
            }

            PaneMessage::CancelRename => {
                let tab = pane.active_mut();
                if tab.renaming.is_some() {
                    tab.renaming = None;
                } else {
                    tab.selection.clear();
                }
                Task::none()
            }

            PaneMessage::DeleteSelected => {
                let tab_idx = pane.active_tab;
                let tab = &mut pane.tabs[tab_idx];
                // Don't delete while rename is in progress or nothing is selected
                if tab.renaming.is_some() || tab.selection.is_empty() {
                    return Task::none();
                }
                let paths: Vec<PathBuf> = tab.selection.items().iter().cloned().collect();
                tab.selection.clear();
                let refresh_path = tab.current_path.clone();
                tab.error = None;
                Task::perform(
                    async move {
                        tokio::task::spawn_blocking(move || {
                            let errors: Vec<String> = paths
                                .into_iter()
                                .filter_map(|p| delete_to_trash(p).err().map(|e| e.to_string()))
                                .collect();
                            if errors.is_empty() {
                                Ok(())
                            } else {
                                Err(format!("{} delete(s) failed: {}", errors.len(), errors.join("; ")))
                            }
                        })
                        .await
                        .unwrap_or_else(|e| Err(e.to_string()))
                    },
                    move |result| {
                        Message::Pane(
                            id,
                            PaneMessage::OperationDone(
                                tab_idx,
                                refresh_path,
                                result,
                            ),
                        )
                    },
                )
            }

            PaneMessage::SelectAll => {
                let entries = pane.active().entries.clone();
                pane.active_mut().selection.select_all(&entries);
                Task::none()
            }

            PaneMessage::ExtendSelectionDown => {
                let tab = pane.active_mut();
                let current_idx = tab
                    .selection
                    .focus()
                    .and_then(|f| tab.entries.iter().position(|e| e.path == *f));
                let next_idx = match current_idx {
                    Some(i) if i + 1 < tab.entries.len() => i + 1,
                    None if !tab.entries.is_empty() => 0,
                    _ => return Task::none(),
                };
                let entries = tab.entries.clone();
                tab.selection.extend_to(&entries, next_idx);
                Task::none()
            }

            PaneMessage::ExtendSelectionUp => {
                let tab = pane.active_mut();
                let current_idx = tab
                    .selection
                    .focus()
                    .and_then(|f| tab.entries.iter().position(|e| e.path == *f));
                let prev_idx = match current_idx {
                    Some(i) if i > 0 => i - 1,
                    None if !tab.entries.is_empty() => 0,
                    _ => return Task::none(),
                };
                let entries = tab.entries.clone();
                tab.selection.extend_to(&entries, prev_idx);
                Task::none()
            }

            PaneMessage::Undo => {
                let tab_idx = pane.active_tab;
                let tab = &mut pane.tabs[tab_idx];
                // Don't undo while rename is active (Ctrl+Z edits text_input text)
                if tab.renaming.is_some() {
                    return Task::none();
                }
                if let Some(UndoAction::Rename { from, to }) = tab.undo_stack.pop() {
                    let refresh_path = tab.current_path.clone();
                    Task::perform(
                        async move {
                            tokio::task::spawn_blocking(move || undo_rename(from, to))
                                .await
                                .unwrap_or_else(|e| Err(anyhow::anyhow!(e)))
                        },
                        move |result| {
                            Message::Pane(
                                id,
                                PaneMessage::OperationDone(
                                    tab_idx,
                                    refresh_path,
                                    result.map_err(|e| e.to_string()),
                                ),
                            )
                        },
                    )
                } else {
                    Task::none()
                }
            }

            // --- Async results ---
            PaneMessage::RenameDone(tab_idx, refresh_path, old_path, result) => {
                let tab = &mut pane.tabs[tab_idx];
                match result {
                    Ok(new_path) => {
                        tab.error = None;
                        tab.undo_stack
                            .push(UndoAction::Rename { from: old_path, to: new_path.clone() });
                        tab.selection.single_select(new_path);
                        load_directory_for(id, tab_idx, refresh_path)
                    }
                    Err(e) => {
                        tab.error = Some(e);
                        Task::none()
                    }
                }
            }

            PaneMessage::OperationDone(tab_idx, refresh_path, result) => {
                let tab = &mut pane.tabs[tab_idx];
                match result {
                    Ok(()) => {
                        tab.error = None;
                        load_directory_for(id, tab_idx, refresh_path)
                    }
                    Err(e) => {
                        tab.error = Some(e);
                        Task::none()
                    }
                }
            }

            PaneMessage::BatchRenameDone(tab_idx, refresh_path, results) => {
                let tab = &mut pane.tabs[tab_idx];
                let mut errors: Vec<String> = Vec::new();
                for (old_path, result) in results {
                    match result {
                        Ok(new_path) => {
                            tab.undo_stack.push(UndoAction::Rename { from: old_path, to: new_path });
                        }
                        Err(e) => errors.push(e),
                    }
                }
                tab.error = if errors.is_empty() {
                    None
                } else {
                    Some(format!("{} rename(s) failed: {}", errors.len(), errors.join("; ")))
                };
                load_directory_for(id, tab_idx, refresh_path)
            }
        }
    }

    fn view(&self) -> Element<'_, Message> {
        let left = pane_view(PaneId::Left, &self.panes[0], &self.drives, self.active == PaneId::Left);
        let right = pane_view(PaneId::Right, &self.panes[1], &self.drives, self.active == PaneId::Right);

        let main: Element<'_, Message> = container(
            row![left, right]
                .spacing(4)
                .width(Length::Fill)
                .height(Length::Fill),
        )
        .padding(10)
        .width(Length::Fill)
        .height(Length::Fill)
        .into();

        if let Some(ref pal) = self.palette {
            let overlay = command_palette_view(pal).map(Message::Palette);
            stack([main, overlay]).into()
        } else if let Some(ref search_state) = self.search {
            let overlay = search_view(search_state).map(Message::Search);
            stack([main, overlay]).into()
        } else if let Some(ref br_state) = self.batch_rename {
            let overlay = batch_rename_view(br_state).map(Message::BatchRename);
            stack([main, overlay]).into()
        } else if let Some(ref label) = self.copy_move_label {
            let overlay = copy_move_progress_overlay(label);
            stack([main, overlay]).into()
        } else {
            main
        }
    }

    fn theme(&self) -> Theme {
        if self.theme.is_dark() { Theme::Dark } else { Theme::Light }
    }
}

fn pane_view<'a>(
    id: PaneId,
    pane: &'a PaneState,
    drives: &'a [PathBuf],
    is_active: bool,
) -> Element<'a, Message> {
    let tab_titles: Vec<String> = pane.tabs.iter().map(|t| t.title()).collect();
    let tabs = tab_bar_view(tab_titles, pane.active_tab)
        .map(move |msg| Message::Pane(id, PaneMessage::TabBar(msg)));

    let active_tab = pane.active();
    let parent = active_tab.current_path.parent().map(|p| p.to_path_buf());

    let addr_bar = address_bar_view(
        &active_tab.current_path,
        &active_tab.address_text,
        drives,
        active_tab.history.can_go_back(),
        active_tab.history.can_go_forward(),
    )
    .map(move |msg| Message::Pane(id, PaneMessage::AddressBar(msg)));

    let selection_items = active_tab.selection.items();
    let renaming_ref = active_tab
        .renaming
        .as_ref()
        .map(|(p, s)| (p.as_path(), s.as_str()));

    let file_list: Element<'_, Message> = if let Some(ref err) = active_tab.error {
        text(format!("Error: {err}")).into()
    } else {
        file_list_view(
            &active_tab.entries,
            parent,
            &active_tab.sort_col,
            &active_tab.sort_dir,
            selection_items,
            renaming_ref,
        )
        .map(move |msg| Message::Pane(id, PaneMessage::FileList(msg)))
    };

    // When preview is enabled, split the content area between file list and preview panel.
    let content: Element<'_, Message> = if pane.show_preview {
        let preview = preview_panel_view::<Message>(&active_tab.preview);
        row![
            container(file_list).width(Length::FillPortion(3)).height(Length::Fill),
            container(preview).width(Length::FillPortion(2)).height(Length::Fill),
        ]
        .spacing(4)
        .width(Length::Fill)
        .height(Length::Fill)
        .into()
    } else {
        file_list
    };

    // Status line: show selection count when ≥1 item selected
    let sel_count = active_tab.selection.len();
    let status_line: Element<'_, Message> = if sel_count > 0 {
        let label = if sel_count == 1 { "1 item selected".to_string() } else { format!("{sel_count} items selected") };
        text(label).size(12).into()
    } else {
        text("").size(12).into()
    };

    let inner = column![tabs, addr_bar, content, status_line]
        .spacing(4)
        .padding(8)
        .width(Length::Fill)
        .height(Length::Fill);

    if is_active {
        container(inner)
            .style(container::bordered_box)
            .width(Length::Fill)
            .height(Length::Fill)
            .into()
    } else {
        container(inner)
            .width(Length::Fill)
            .height(Length::Fill)
            .into()
    }
}

/// Spawn a background search task from the current `SearchState` query/filter/root.
/// Returns a `Task` that produces `Message::Search(SearchMessage::ResultsLoaded(...))`.
fn spawn_search_task(state: &SearchState) -> Task<Message> {
    let root = state.root.clone();
    let query = state.query.clone();
    let ext = state.ext_filter_opt().map(str::to_owned);
    Task::perform(
        async move {
            tokio::task::spawn_blocking(move || {
                search_files(&root, &query, ext.as_deref(), MAX_SEARCH_RESULTS)
            })
            .await
            .unwrap_or_default()
        },
        |results| Message::Search(SearchMessage::ResultsLoaded(results)),
    )
}

/// Spawn an async preview load for `path` in the given pane/tab.
fn load_preview_for(pane: PaneId, tab_idx: usize, path: PathBuf) -> Task<Message> {
    Task::perform(
        async move {
            tokio::task::spawn_blocking(move || load_preview(&path))
                .await
                .unwrap_or_else(|e| PreviewContent::Error(e.to_string()))
        },
        move |content| Message::Pane(pane, PaneMessage::PreviewLoaded(tab_idx, content)),
    )
}

fn copy_move_backdrop_style(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(Background::Color(Color::from_rgba(0.0, 0.0, 0.0, 0.7))),
        ..Default::default()
    }
}

/// Renders a centered progress card while a copy/move is in flight.
fn copy_move_progress_overlay(label: &str) -> Element<'_, Message> {
    let card = container(
        column![text(label).size(14), text("Please wait…").size(12)]
            .spacing(8)
            .align_x(iced::alignment::Horizontal::Center),
    )
    .padding(28)
    .style(container::rounded_box);

    container(card)
        .center(Length::Fill)
        .style(copy_move_backdrop_style)
        .into()
}

fn load_directory_for(pane: PaneId, tab_idx: usize, path: PathBuf) -> Task<Message> {
    let path_for_msg = path.clone();
    Task::perform(
        async move {
            tokio::task::spawn_blocking(move || list_directory(&path))
                .await
                .unwrap_or_else(|e| Err(anyhow::anyhow!(e)))
        },
        move |result| {
            Message::Pane(
                pane,
                PaneMessage::DirectoryLoaded(tab_idx, path_for_msg, result.map_err(|e| e.to_string())),
            )
        },
    )
}
