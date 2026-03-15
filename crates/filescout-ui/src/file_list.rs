use std::collections::HashSet;
use std::path::{Path, PathBuf};

use filescout_core::{FileEntry, SortColumn, SortDirection};
use iced::widget::{button, column, container, row, scrollable, text, text_input};
use iced::{Element, Length};
use iced::widget::Id;

use crate::icons::file_icon_text;

/// Unique ID for the inline rename text input (used for programmatic focus).
pub const RENAME_INPUT_ID: &str = "rename_input";

/// Messages emitted by the file list component.
#[derive(Debug, Clone)]
pub enum FileListMessage {
    /// A directory row was clicked — navigate into it (for ".." and dirs).
    Navigate(PathBuf),
    /// A column header was clicked — sort by that column.
    Sort(SortColumn),
    /// A file or directory row was clicked for selection.
    Select(PathBuf),
    /// The inline rename text input changed.
    RenameEdit(String),
}

/// Render a sortable, scrollable list of file entries.
///
/// - `parent_path` — if `Some`, a `..` row is shown at the top.
/// - `sort_col` / `sort_dir` — current sort state for column indicators.
/// - `selection` — set of selected paths (for multi-row highlight).
/// - `renaming` — if `Some((path, text))`, that row shows an inline text input.
pub fn file_list_view<'a>(
    entries: &'a [FileEntry],
    parent_path: Option<PathBuf>,
    sort_col: &'a SortColumn,
    sort_dir: &'a SortDirection,
    selection: &'a HashSet<PathBuf>,
    renaming: Option<(&'a Path, &'a str)>,
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

    // ".." parent row — always navigates, never selectable
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
        let path = &entry.path;
        let is_selected = selection.contains(path);
        let is_renaming = renaming.is_some_and(|(rp, _)| rp == path);

        let size_text = if entry.is_dir {
            String::new()
        } else {
            format_size(entry.size)
        };
        let modified_text = entry
            .modified
            .map(|dt| dt.format("%Y-%m-%d %H:%M").to_string())
            .unwrap_or_default();

        let icon = file_icon_text(entry);

        if is_renaming {
            let (_, rename_text) = renaming.unwrap();
            // Inline rename row — plain container, no button wrapper
            let rename_row = container(
                row![
                    text_input("New name…", rename_text)
                        .id(Id::new(RENAME_INPUT_ID))
                        .on_input(FileListMessage::RenameEdit)
                        .width(300),
                    text(size_text).width(100),
                    text(modified_text).width(200),
                ]
                .spacing(10)
                .padding(2),
            )
            .style(container::bordered_box)
            .width(Length::Fill);

            rows.push(rename_row.into());
        } else {
            let row_content = row![
                text(format!("{icon} {}", entry.name)).width(300),
                text(size_text).width(100),
                text(modified_text).width(200),
            ]
            .spacing(10)
            .padding(2);

            let path_clone = path.clone();
            let style = if is_selected { button::primary } else { button::text };

            let row_btn = if entry.is_dir {
                // Dirs: click → Select (navigate via Enter key)
                button(row_content)
                    .on_press(FileListMessage::Select(path_clone))
                    .style(style)
                    .width(Length::Fill)
            } else {
                // Files: click → Select
                button(row_content)
                    .on_press(FileListMessage::Select(path_clone))
                    .style(style)
                    .width(Length::Fill)
            };

            rows.push(row_btn.into());
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
