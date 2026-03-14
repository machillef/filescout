use std::env;
use std::path::PathBuf;

use filescout_core::fs::{list_directory, sort_entries};
use filescout_core::{FileEntry, SortColumn, SortDirection};
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
}

#[derive(Debug, Clone)]
enum Message {
    FileList(FileListMessage),
    DirectoryLoaded(PathBuf, Result<Vec<FileEntry>, String>),
}

impl FileScout {
    fn new() -> (Self, Task<Message>) {
        let current_path = env::current_dir().unwrap_or_else(|_| PathBuf::from("C:\\"));

        let app = Self {
            current_path: current_path.clone(),
            entries: Vec::new(),
            error: None,
            sort_col: SortColumn::Name,
            sort_dir: SortDirection::Asc,
        };

        (app, load_directory(current_path))
    }

    fn update(&mut self, message: Message) -> Task<Message> {
        match message {
            Message::FileList(FileListMessage::Navigate(path)) => load_directory(path),

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

            Message::DirectoryLoaded(path, Ok(mut entries)) => {
                sort_entries(&mut entries, &self.sort_col, &self.sort_dir);
                self.entries = entries;
                self.current_path = path;
                self.error = None;
                Task::none()
            }

            Message::DirectoryLoaded(_, Err(e)) => {
                self.error = Some(e);
                Task::none()
            }
        }
    }

    fn view(&self) -> Element<'_, Message> {
        let header = text(format!("📂 {}", self.current_path.display())).size(18);

        let parent = self.current_path.parent().map(|p| p.to_path_buf());

        let content: Element<'_, Message> = if let Some(ref err) = self.error {
            text(format!("Error: {err}")).into()
        } else {
            file_list_view(&self.entries, parent, &self.sort_col, &self.sort_dir)
                .map(Message::FileList)
        };

        container(
            column![header, content]
                .spacing(10)
                .padding(15),
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
