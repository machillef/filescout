use std::path::PathBuf;

use filescout_core::{FileEntry, SortColumn, SortDirection};
use iced::widget::{button, column, row, scrollable, text};
use iced::{Element, Length};

/// Messages emitted by the file list component.
#[derive(Debug, Clone)]
pub enum FileListMessage {
    Navigate(PathBuf),
    Sort(SortColumn),
}

/// Render a sortable, scrollable list of file entries.
///
/// `parent_path` — if `Some`, a `..` row is shown at the top for navigating up.
/// `sort_col` / `sort_dir` — current sort state, used to render column indicators.
pub fn file_list_view<'a>(
    entries: &'a [FileEntry],
    parent_path: Option<PathBuf>,
    sort_col: &'a SortColumn,
    sort_dir: &'a SortDirection,
) -> Element<'a, FileListMessage> {
    let header = row![
        button(text(format!("Name{}", sort_indicator(sort_col, sort_dir, &SortColumn::Name))).width(300))
            .on_press(FileListMessage::Sort(SortColumn::Name))
            .style(button::text),
        button(text(format!("Size{}", sort_indicator(sort_col, sort_dir, &SortColumn::Size))).width(100))
            .on_press(FileListMessage::Sort(SortColumn::Size))
            .style(button::text),
        button(text(format!("Modified{}", sort_indicator(sort_col, sort_dir, &SortColumn::Modified))).width(200))
            .on_press(FileListMessage::Sort(SortColumn::Modified))
            .style(button::text),
    ]
    .spacing(10)
    .padding(5);

    let mut rows: Vec<Element<'a, FileListMessage>> = Vec::new();

    if let Some(parent) = parent_path {
        let up_row = button(
            row![
                text("📁 ..").width(300),
                text("").width(100),
                text("").width(200),
            ]
            .spacing(10)
            .padding(2),
        )
        .on_press(FileListMessage::Navigate(parent))
        .style(button::text)
        .width(Length::Fill);

        rows.push(up_row.into());
    }

    for entry in entries {
        let icon = if entry.is_dir { "📁" } else { "📄" };
        let size_text = if entry.is_dir {
            String::new()
        } else {
            format_size(entry.size)
        };
        let modified_text = entry
            .modified
            .map(|dt| dt.format("%Y-%m-%d %H:%M").to_string())
            .unwrap_or_default();

        let row_content = row![
            text(format!("{icon} {}", entry.name)).width(300),
            text(size_text).width(100),
            text(modified_text).width(200),
        ]
        .spacing(10)
        .padding(2);

        if entry.is_dir {
            let path = entry.path.clone();
            rows.push(
                button(row_content)
                    .on_press(FileListMessage::Navigate(path))
                    .style(button::text)
                    .width(Length::Fill)
                    .into(),
            );
        } else {
            rows.push(row_content.into());
        }
    }

    let list = column(rows).spacing(1);

    scrollable(column![header, list].spacing(5)).into()
}

fn sort_indicator<'a>(current: &SortColumn, dir: &SortDirection, col: &SortColumn) -> &'a str {
    if current == col {
        if *dir == SortDirection::Asc { " ▲" } else { " ▼" }
    } else {
        ""
    }
}

fn format_size(bytes: u64) -> String {
    const KB: u64 = 1024;
    const MB: u64 = KB * 1024;
    const GB: u64 = MB * 1024;

    if bytes >= GB {
        format!("{:.1} GB", bytes as f64 / GB as f64)
    } else if bytes >= MB {
        format!("{:.1} MB", bytes as f64 / MB as f64)
    } else if bytes >= KB {
        format!("{:.1} KB", bytes as f64 / KB as f64)
    } else {
        format!("{bytes} B")
    }
}
