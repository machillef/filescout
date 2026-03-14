use std::env;
use std::path::PathBuf;

use filescout_core::fs::{list_directory, list_drives, sort_entries};
use filescout_core::{FileEntry, History, SortColumn, SortDirection};
use filescout_ui::address_bar::{address_bar_view, AddressBarMessage};
use filescout_ui::file_list::{file_list_view, FileListMessage};
use iced::widget::{column, container, text};
use iced::{Element, Length, Size, Task, Theme};
use tracing_subscriber::EnvFilter;

fn main() -> iced::Result {
    tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::from_default_env())
        .init();

    iced::application(FileScout::new, FileScout::update, FileScout::view)
        .title("FileScout")
        .theme(FileScout::theme)
        .window_size(Size::new(1200.0, 800.0))
        .centered()
        .run()
}

struct FileScout {
    current_path: PathBuf,
    entries: Vec<FileEntry>,
    error: Option<String>,
    sort_col: SortColumn,
    sort_dir: SortDirection,
    history: History,
    address_text: String,
    drives: Vec<PathBuf>,
}

#[derive(Debug, Clone)]
enum Message {
    FileList(FileListMessage),
    AddressBar(AddressBarMessage),
    DirectoryLoaded(PathBuf, Result<Vec<FileEntry>, String>),
    DrivesLoaded(Vec<PathBuf>),
}

impl FileScout {
    fn new() -> (Self, Task<Message>) {
        let current_path = env::current_dir().unwrap_or_else(|_| PathBuf::from("C:\\"));
        let address_text = current_path.display().to_string();

        let app = Self {
            history: History::new(current_path.clone()),
            address_text,
            current_path: current_path.clone(),
            entries: Vec::new(),
            error: None,
            sort_col: SortColumn::Name,
            sort_dir: SortDirection::Asc,
            drives: Vec::new(),
        };

        let load_dir = load_directory(current_path);
        let load_drvs = Task::perform(
            async { tokio::task::spawn_blocking(list_drives).await.unwrap_or_default() },
            Message::DrivesLoaded,
        );

        (app, Task::batch([load_dir, load_drvs]))
    }

    fn update(&mut self, message: Message) -> Task<Message> {
        match message {
            Message::FileList(FileListMessage::Navigate(path)) => {
                self.history.navigate(path.clone());
                self.address_text = path.display().to_string();
                load_directory(path)
            }

            Message::FileList(FileListMessage::Sort(col)) => {
                if self.sort_col == col {
                    self.sort_dir = match self.sort_dir {
                        SortDirection::Asc => SortDirection::Desc,
                        SortDirection::Desc => SortDirection::Asc,
                    };
                } else {
                    self.sort_col = col;
                    self.sort_dir = SortDirection::Asc;
                }
                sort_entries(&mut self.entries, &self.sort_col, &self.sort_dir);
                Task::none()
            }

            Message::AddressBar(AddressBarMessage::Back) => {
                if let Some(path) = self.history.back().cloned() {
                    self.address_text = path.display().to_string();
                    load_directory(path)
                } else {
                    Task::none()
                }
            }

            Message::AddressBar(AddressBarMessage::Forward) => {
                if let Some(path) = self.history.forward().cloned() {
                    self.address_text = path.display().to_string();
                    load_directory(path)
                } else {
                    Task::none()
                }
            }

            Message::AddressBar(AddressBarMessage::Edit(text)) => {
                self.address_text = text;
                Task::none()
            }

            Message::AddressBar(AddressBarMessage::Submit) => {
                let path = PathBuf::from(&self.address_text);
                self.history.navigate(path.clone());
                load_directory(path)
            }

            Message::AddressBar(AddressBarMessage::BreadcrumbNavigate(path)) => {
                self.history.navigate(path.clone());
                self.address_text = path.display().to_string();
                load_directory(path)
            }

            Message::AddressBar(AddressBarMessage::DriveSelected(path)) => {
                self.history.navigate(path.clone());
                self.address_text = path.display().to_string();
                load_directory(path)
            }

            Message::DirectoryLoaded(path, Ok(mut entries)) => {
                sort_entries(&mut entries, &self.sort_col, &self.sort_dir);
                self.entries = entries;
                self.current_path = path.clone();
                self.address_text = path.display().to_string();
                self.error = None;
                Task::none()
            }

            Message::DirectoryLoaded(_, Err(e)) => {
                self.error = Some(e);
                Task::none()
            }

            Message::DrivesLoaded(drives) => {
                self.drives = drives;
                Task::none()
            }
        }
    }

    fn view(&self) -> Element<'_, Message> {
        let parent = self.current_path.parent().map(|p| p.to_path_buf());

        let addr_bar = address_bar_view(
            &self.current_path,
            &self.address_text,
            &self.drives,
            self.history.can_go_back(),
            self.history.can_go_forward(),
        )
        .map(Message::AddressBar);

        let content: Element<'_, Message> = if let Some(ref err) = self.error {
            text(format!("Error: {err}")).into()
        } else {
            file_list_view(&self.entries, parent, &self.sort_col, &self.sort_dir)
                .map(Message::FileList)
        };

        container(
            column![addr_bar, content]
                .spacing(4)
                .padding(10),
        )
        .width(Length::Fill)
        .height(Length::Fill)
        .into()
    }

    fn theme(&self) -> Theme {
        Theme::Dark
    }
}

fn load_directory(path: PathBuf) -> Task<Message> {
    let path_for_msg = path.clone();
    Task::perform(
        async move {
            tokio::task::spawn_blocking(move || list_directory(&path))
                .await
                .unwrap_or_else(|e| Err(anyhow::anyhow!(e)))
        },
        move |result| {
            Message::DirectoryLoaded(
                path_for_msg,
                result.map_err(|e| e.to_string()),
            )
        },
    )
}
