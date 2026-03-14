use std::env;
use std::path::PathBuf;

use filescout_core::fs::{list_directory, list_drives, sort_entries};
use filescout_core::SortDirection;
use filescout_ui::address_bar::{address_bar_view, AddressBarMessage};
use filescout_ui::file_list::{file_list_view, FileListMessage};
use filescout_ui::tab_bar::{tab_bar_view, TabBarMessage};
use iced::widget::{column, container, row, text};
use iced::{Element, Length, Size, Task, Theme};
use tracing_subscriber::EnvFilter;

mod pane;
use pane::{PaneId, PaneMessage, PaneState};

fn main() -> iced::Result {
    tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::from_default_env())
        .init();

    iced::application(FileScout::new, FileScout::update, FileScout::view)
        .title("FileScout")
        .theme(FileScout::theme)
        .window_size(Size::new(1400.0, 800.0))
        .centered()
        .run()
}

struct FileScout {
    panes: [PaneState; 2],
    active: PaneId,
    drives: Vec<PathBuf>,
}

#[derive(Debug, Clone)]
enum Message {
    Pane(PaneId, PaneMessage),
    DrivesLoaded(Vec<PathBuf>),
}

impl FileScout {
    fn new() -> (Self, Task<Message>) {
        let cwd = env::current_dir().unwrap_or_else(|_| PathBuf::from("C:\\"));

        let app = Self {
            panes: [PaneState::new(cwd.clone()), PaneState::new(cwd.clone())],
            active: PaneId::Left,
            drives: Vec::new(),
        };

        let load_left = load_directory_for(PaneId::Left, 0, cwd.clone());
        let load_right = load_directory_for(PaneId::Right, 0, cwd);
        let load_drvs = Task::perform(
            async { tokio::task::spawn_blocking(list_drives).await.unwrap_or_default() },
            Message::DrivesLoaded,
        );

        (app, Task::batch([load_left, load_right, load_drvs]))
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
        }
    }

    fn update_pane(&mut self, id: PaneId, msg: PaneMessage) -> Task<Message> {
        let pane = &mut self.panes[id.index()];

        match msg {
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

            PaneMessage::FileList(FileListMessage::Navigate(path)) => {
                let tab_idx = pane.active_tab;
                let tab = &mut pane.tabs[tab_idx];
                tab.history.navigate(path.clone());
                tab.address_text = path.display().to_string();
                load_directory_for(id, tab_idx, path)
            }

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

            PaneMessage::AddressBar(AddressBarMessage::Back) => {
                let tab_idx = pane.active_tab;
                let tab = &mut pane.tabs[tab_idx];
                if let Some(path) = tab.history.back().cloned() {
                    tab.address_text = path.display().to_string();
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
                load_directory_for(id, tab_idx, path)
            }

            PaneMessage::AddressBar(AddressBarMessage::BreadcrumbNavigate(path)) => {
                let tab_idx = pane.active_tab;
                let tab = &mut pane.tabs[tab_idx];
                tab.history.navigate(path.clone());
                tab.address_text = path.display().to_string();
                load_directory_for(id, tab_idx, path)
            }

            PaneMessage::AddressBar(AddressBarMessage::DriveSelected(path)) => {
                let tab_idx = pane.active_tab;
                let tab = &mut pane.tabs[tab_idx];
                tab.history.navigate(path.clone());
                tab.address_text = path.display().to_string();
                load_directory_for(id, tab_idx, path)
            }

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
        }
    }

    fn view(&self) -> Element<'_, Message> {
        let left = pane_view(PaneId::Left, &self.panes[0], &self.drives, self.active == PaneId::Left);
        let right = pane_view(PaneId::Right, &self.panes[1], &self.drives, self.active == PaneId::Right);

        container(
            row![left, right]
                .spacing(4)
                .width(Length::Fill)
                .height(Length::Fill),
        )
        .padding(10)
        .width(Length::Fill)
        .height(Length::Fill)
        .into()
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

    let content: Element<'_, Message> = if let Some(ref err) = active_tab.error {
        text(format!("Error: {err}")).into()
    } else {
        file_list_view(&active_tab.entries, parent, &active_tab.sort_col, &active_tab.sort_dir)
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
