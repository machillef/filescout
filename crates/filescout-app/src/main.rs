use std::env;
use std::path::PathBuf;

use filescout_core::fs::list_directory;
use filescout_core::FileEntry;
use filescout_ui::file_list::file_list_view;
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
}

#[derive(Debug, Clone)]
enum Message {
    // Placeholder for future messages
}

impl FileScout {
    fn new() -> (Self, Task<Message>) {
        let current_path = env::current_dir().unwrap_or_else(|_| PathBuf::from("C:\\"));

        let (entries, error) = match list_directory(&current_path) {
            Ok(entries) => (entries, None),
            Err(e) => (Vec::new(), Some(e.to_string())),
        };

        let app = Self {
            current_path,
            entries,
            error,
        };

        (app, Task::none())
    }

    fn update(&mut self, message: Message) -> Task<Message> {
        match message {}
    }

    fn view(&self) -> Element<'_, Message> {
        let header = text(format!("📂 {}", self.current_path.display())).size(18);

        let content: Element<'_, Message> = if let Some(ref err) = self.error {
            text(format!("Error: {err}")).into()
        } else {
            file_list_view(&self.entries)
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
