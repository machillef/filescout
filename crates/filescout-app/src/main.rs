use std::env;
use std::path::PathBuf;

use filescout_core::fs::{list_directory, list_drives, sort_entries};
use filescout_core::ops::{delete_to_trash, rename_file, undo_rename, validate_file_name, UndoAction};
use filescout_core::SortDirection;
use filescout_ui::address_bar::{address_bar_view, AddressBarMessage};
use filescout_ui::file_list::{file_list_view, FileListMessage};
use filescout_ui::tab_bar::{tab_bar_view, TabBarMessage};
use iced::widget::{column, container, row, stack, text};
use iced::{event, keyboard, Element, Event, Length, Size, Subscription, Task, Theme};
use tracing_subscriber::EnvFilter;

mod command_palette;
mod pane;

use command_palette::{command_palette_view, CommandId, PaletteMessage, PaletteState};
use pane::{PaneId, PaneMessage, PaneState};

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
}

/// Keyboard actions that apply to whichever pane is currently active.
#[derive(Debug, Clone)]
enum ActiveAction {
    StartRename,  // F2
    Delete,       // F8
    CommitRename, // Enter (commit rename OR navigate into selected dir — routed in update_pane)
    CancelEdit,   // Esc
    Undo,         // Ctrl+Z
}

#[derive(Debug, Clone)]
enum Message {
    Pane(PaneId, PaneMessage),
    DrivesLoaded(Vec<PathBuf>),
    /// A keyboard shortcut directed at the currently active pane.
    ActivePaneAction(ActiveAction),
    /// A message from the command palette UI.
    Palette(PaletteMessage),
    /// Raw key press forwarded from the subscription for stateful routing.
    KeyPressed(keyboard::Key, keyboard::Modifiers),
}

impl FileScout {
    fn new() -> (Self, Task<Message>) {
        let cwd = env::current_dir().unwrap_or_else(|_| PathBuf::from("C:\\"));

        let app = Self {
            panes: [PaneState::new(cwd.clone()), PaneState::new(cwd.clone())],
            active: PaneId::Left,
            drives: Vec::new(),
            palette: None,
        };

        let load_left = load_directory_for(PaneId::Left, 0, cwd.clone());
        let load_right = load_directory_for(PaneId::Right, 0, cwd);
        let load_drvs = Task::perform(
            async { tokio::task::spawn_blocking(list_drives).await.unwrap_or_default() },
            Message::DrivesLoaded,
        );

        (app, Task::batch([load_left, load_right, load_drvs]))
    }

    fn subscription(_state: &Self) -> Subscription<Message> {
        event::listen_with(|ev, _status, _window| {
            if let Event::Keyboard(keyboard::Event::KeyPressed { key, modifiers, .. }) = ev {
                Some(Message::KeyPressed(key, modifiers))
            } else {
                None
            }
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
                };
                self.update_pane(id, msg)
            }
            Message::Palette(msg) => self.handle_palette(msg),
            Message::KeyPressed(key, modifiers) => self.route_key(key, modifiers),
        }
    }

    fn route_key(&mut self, key: keyboard::Key, modifiers: keyboard::Modifiers) -> Task<Message> {
        use keyboard::key::Named;
        if self.palette.is_some() {
            // When palette is open, only palette-navigation keys are routed here.
            // Character input is handled directly by the text_input widget.
            match key.as_ref() {
                keyboard::Key::Named(Named::ArrowUp) => {
                    self.handle_palette(PaletteMessage::MoveUp)
                }
                keyboard::Key::Named(Named::ArrowDown) => {
                    self.handle_palette(PaletteMessage::MoveDown)
                }
                keyboard::Key::Named(Named::Enter) => {
                    self.handle_palette(PaletteMessage::Execute)
                }
                keyboard::Key::Named(Named::Escape) => {
                    self.handle_palette(PaletteMessage::Close)
                }
                keyboard::Key::Character("p") if modifiers.control() && modifiers.shift() => {
                    self.handle_palette(PaletteMessage::Close)
                }
                _ => Task::none(),
            }
        } else {
            match key.as_ref() {
                keyboard::Key::Character("p") if modifiers.control() && modifiers.shift() => {
                    self.palette = Some(PaletteState::new());
                    Task::none()
                }
                keyboard::Key::Named(Named::F2) => {
                    self.update(Message::ActivePaneAction(ActiveAction::StartRename))
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
        }
    }

    fn update_pane(&mut self, id: PaneId, msg: PaneMessage) -> Task<Message> {
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
                tab.selected = None;
                load_directory_for(id, tab_idx, path)
            }

            PaneMessage::FileList(FileListMessage::Select(path)) => {
                pane.active_mut().selected = Some(path);
                Task::none()
            }

            PaneMessage::NavigateSelected => {
                let tab_idx = pane.active_tab;
                let tab = &mut pane.tabs[tab_idx];
                // Ignore if rename is active — Enter commits the rename instead
                if tab.renaming.is_some() {
                    return Task::none();
                }
                if let Some(ref sel) = tab.selected.clone()
                    && sel.is_dir()
                {
                    let path = sel.clone();
                    tab.history.navigate(path.clone());
                    tab.address_text = path.display().to_string();
                    tab.selected = None;
                    return load_directory_for(id, tab_idx, path);
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
                    tab.selected = None;
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
                    tab.selected = None;
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
                tab.selected = None;
                load_directory_for(id, tab_idx, path)
            }

            PaneMessage::AddressBar(AddressBarMessage::BreadcrumbNavigate(path)) => {
                let tab_idx = pane.active_tab;
                let tab = &mut pane.tabs[tab_idx];
                tab.history.navigate(path.clone());
                tab.address_text = path.display().to_string();
                tab.selected = None;
                load_directory_for(id, tab_idx, path)
            }

            PaneMessage::AddressBar(AddressBarMessage::DriveSelected(path)) => {
                let tab_idx = pane.active_tab;
                let tab = &mut pane.tabs[tab_idx];
                tab.history.navigate(path.clone());
                tab.address_text = path.display().to_string();
                tab.selected = None;
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
                if let Some(ref selected) = tab.selected.clone() {
                    let name = selected
                        .file_name()
                        .map(|n| n.to_string_lossy().into_owned())
                        .unwrap_or_default();
                    tab.renaming = Some((selected.clone(), name));
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
                    tab.selected = None;
                }
                Task::none()
            }

            PaneMessage::DeleteSelected => {
                let tab_idx = pane.active_tab;
                let tab = &mut pane.tabs[tab_idx];
                // Don't delete while rename is in progress
                if tab.renaming.is_some() || tab.selected.is_none() {
                    return Task::none();
                }
                let path = tab.selected.take().unwrap();
                let refresh_path = tab.current_path.clone();
                tab.error = None;
                Task::perform(
                    async move {
                        tokio::task::spawn_blocking(move || delete_to_trash(path))
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
                        tab.selected = Some(new_path);
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
        } else {
            main
        }
    }

    fn theme(&self) -> Theme {
        Theme::Dark
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

    let selected_ref = active_tab.selected.as_deref();
    let renaming_ref = active_tab
        .renaming
        .as_ref()
        .map(|(p, s)| (p.as_path(), s.as_str()));

    let content: Element<'_, Message> = if let Some(ref err) = active_tab.error {
        text(format!("Error: {err}")).into()
    } else {
        file_list_view(
            &active_tab.entries,
            parent,
            &active_tab.sort_col,
            &active_tab.sort_dir,
            selected_ref,
            renaming_ref,
        )
        .map(move |msg| Message::Pane(id, PaneMessage::FileList(msg)))
    };

    let inner = column![tabs, addr_bar, content]
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
