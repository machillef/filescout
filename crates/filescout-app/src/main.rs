use std::env;
use std::path::PathBuf;

use filescout_core::fs::{list_directory, list_drives, sort_entries};
use filescout_core::SortDirection;
use filescout_ui::address_bar::{address_bar_view, AddressBarMessage};
use filescout_ui::file_list::{file_list_view, FileListMessage};
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

        let load_left = load_directory_for(PaneId::Left, cwd.clone());
        let load_right = load_directory_for(PaneId::Right, cwd);
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
            PaneMessage::FileList(FileListMessage::Navigate(path)) => {
                pane.history.navigate(path.clone());
                pane.address_text = path.display().to_string();
                load_directory_for(id, path)
            }

            PaneMessage::FileList(FileListMessage::Sort(col)) => {
                if pane.sort_col == col {
                    pane.sort_dir = if pane.sort_dir == SortDirection::Asc {
                        SortDirection::Desc
                    } else {
                        SortDirection::Asc
                    };
                } else {
                    pane.sort_col = col;
                    pane.sort_dir = SortDirection::Asc;
                }
                sort_entries(&mut pane.entries, &pane.sort_col, &pane.sort_dir);
                Task::none()
            }

            PaneMessage::AddressBar(AddressBarMessage::Back) => {
                if let Some(path) = pane.history.back().cloned() {
                    pane.address_text = path.display().to_string();
                    load_directory_for(id, path)
                } else {
                    Task::none()
                }
            }

            PaneMessage::AddressBar(AddressBarMessage::Forward) => {
                if let Some(path) = pane.history.forward().cloned() {
                    pane.address_text = path.display().to_string();
                    load_directory_for(id, path)
                } else {
                    Task::none()
                }
            }

            PaneMessage::AddressBar(AddressBarMessage::Edit(t)) => {
                pane.address_text = t;
                Task::none()
            }

            PaneMessage::AddressBar(AddressBarMessage::Submit) => {
                let path = PathBuf::from(&pane.address_text);
                pane.history.navigate(path.clone());
                load_directory_for(id, path)
            }

            PaneMessage::AddressBar(AddressBarMessage::BreadcrumbNavigate(path)) => {
                pane.history.navigate(path.clone());
                pane.address_text = path.display().to_string();
                load_directory_for(id, path)
            }

            PaneMessage::AddressBar(AddressBarMessage::DriveSelected(path)) => {
                pane.history.navigate(path.clone());
                pane.address_text = path.display().to_string();
                load_directory_for(id, path)
            }

            PaneMessage::DirectoryLoaded(path, Ok(mut entries)) => {
                sort_entries(&mut entries, &pane.sort_col, &pane.sort_dir);
                pane.entries = entries;
                pane.current_path = path.clone();
                pane.address_text = path.display().to_string();
                pane.error = None;
                Task::none()
            }

            PaneMessage::DirectoryLoaded(_, Err(e)) => {
                pane.error = Some(e);
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
    let parent = pane.current_path.parent().map(|p| p.to_path_buf());

    let addr_bar = address_bar_view(
        &pane.current_path,
        &pane.address_text,
        drives,
        pane.history.can_go_back(),
        pane.history.can_go_forward(),
    )
    .map(move |msg| Message::Pane(id, PaneMessage::AddressBar(msg)));

    let content: Element<'_, Message> = if let Some(ref err) = pane.error {
        text(format!("Error: {err}")).into()
    } else {
        file_list_view(&pane.entries, parent, &pane.sort_col, &pane.sort_dir)
            .map(move |msg| Message::Pane(id, PaneMessage::FileList(msg)))
    };

    let inner = column![addr_bar, content]
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

fn load_directory_for(pane: PaneId, path: PathBuf) -> Task<Message> {
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
                PaneMessage::DirectoryLoaded(path_for_msg, result.map_err(|e| e.to_string())),
            )
        },
    )
}
