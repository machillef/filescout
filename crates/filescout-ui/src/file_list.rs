use std::collections::HashSet;
use std::path::{Path, PathBuf};

use filescout_core::{FileEntry, SortColumn, SortDirection, ViewMode};
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
    view_mode: &'a ViewMode,
) -> Element<'a, FileListMessage> {
    let is_details = *view_mode == ViewMode::Details;

    let mut header_items: Vec<Element<'a, FileListMessage>> = vec![
        button(text(format!("Name{}", sort_indicator(sort_col, sort_dir, &SortColumn::Name))).width(300))
            .on_press(FileListMessage::Sort(SortColumn::Name))
            .style(button::text)
            .into(),
    ];
    if is_details {
        header_items.push(text("Ext").width(60).into());
        header_items.push(text("Type").width(100).into());
    }
    header_items.push(
        button(text(format!("Size{}", sort_indicator(sort_col, sort_dir, &SortColumn::Size))).width(100))
            .on_press(FileListMessage::Sort(SortColumn::Size))
            .style(button::text)
            .into(),
    );
    header_items.push(
        button(text(format!("Modified{}", sort_indicator(sort_col, sort_dir, &SortColumn::Modified))).width(200))
            .on_press(FileListMessage::Sort(SortColumn::Modified))
            .style(button::text)
            .into(),
    );

    let header = row(header_items).spacing(10).padding(5);

    let mut rows: Vec<Element<'a, FileListMessage>> = Vec::new();

    // ".." parent row — always navigates, never selectable
    if let Some(parent) = parent_path {
        let mut up_items: Vec<Element<'a, FileListMessage>> =
            vec![text("\u{1F4C1} ..").width(300).into()];
        if is_details {
            up_items.push(text("").width(60).into());
            up_items.push(text("").width(100).into());
        }
        up_items.push(text("").width(100).into());
        up_items.push(text("").width(200).into());

        let up_row = button(row(up_items).spacing(10).padding(2))
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
        let ext_text = if is_details {
            extract_extension(&entry.name)
        } else {
            String::new()
        };
        let type_text = if is_details {
            file_type_description(entry)
        } else {
            String::new()
        };

        if is_renaming {
            let (_, rename_text) = renaming.unwrap();
            let mut rename_items: Vec<Element<'a, FileListMessage>> = vec![
                text_input("New name…", rename_text)
                    .id(Id::new(RENAME_INPUT_ID))
                    .on_input(FileListMessage::RenameEdit)
                    .width(300)
                    .into(),
            ];
            if is_details {
                rename_items.push(text(ext_text).width(60).into());
                rename_items.push(text(type_text).width(100).into());
            }
            rename_items.push(text(size_text).width(100).into());
            rename_items.push(text(modified_text).width(200).into());

            let rename_row = container(row(rename_items).spacing(10).padding(2))
                .style(container::bordered_box)
                .width(Length::Fill);

            rows.push(rename_row.into());
        } else {
            let mut row_items: Vec<Element<'a, FileListMessage>> =
                vec![text(format!("{icon} {}", entry.name)).width(300).into()];
            if is_details {
                row_items.push(text(ext_text).width(60).into());
                row_items.push(text(type_text).width(100).into());
            }
            row_items.push(text(size_text).width(100).into());
            row_items.push(text(modified_text).width(200).into());

            let row_content = row(row_items).spacing(10).padding(2);
            let path_clone = path.clone();
            let style = if is_selected { button::primary } else { button::text };

            let row_btn = button(row_content)
                .on_press(FileListMessage::Select(path_clone))
                .style(style)
                .width(Length::Fill);

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

/// Extract the file extension (lowercase, without dot).
fn extract_extension(name: &str) -> String {
    name.rsplit('.')
        .next()
        .filter(|ext| name.contains('.') && !ext.is_empty())
        .map(|ext| ext.to_lowercase())
        .unwrap_or_default()
}

/// Return a human-readable type description for a file entry.
fn file_type_description(entry: &FileEntry) -> String {
    if entry.is_dir {
        return "Folder".to_string();
    }
    let ext = extract_extension(&entry.name);
    match ext.as_str() {
        "txt" | "log" => "Text File".to_string(),
        "rs" => "Rust Source".to_string(),
        "py" => "Python Script".to_string(),
        "js" | "jsx" => "JavaScript".to_string(),
        "ts" | "tsx" => "TypeScript".to_string(),
        "html" | "htm" => "HTML File".to_string(),
        "css" | "scss" => "Stylesheet".to_string(),
        "json" => "JSON File".to_string(),
        "xml" => "XML File".to_string(),
        "yaml" | "yml" => "YAML File".to_string(),
        "toml" => "TOML File".to_string(),
        "md" => "Markdown".to_string(),
        "pdf" => "PDF Document".to_string(),
        "doc" | "docx" => "Word Document".to_string(),
        "xls" | "xlsx" => "Spreadsheet".to_string(),
        "ppt" | "pptx" => "Presentation".to_string(),
        "png" | "jpg" | "jpeg" | "gif" | "bmp" | "svg" | "webp" => "Image".to_string(),
        "mp3" | "wav" | "flac" | "aac" | "ogg" => "Audio".to_string(),
        "mp4" | "avi" | "mkv" | "mov" | "webm" => "Video".to_string(),
        "zip" | "tar" | "gz" | "7z" | "rar" => "Archive".to_string(),
        "exe" | "msi" => "Executable".to_string(),
        "dll" => "Library".to_string(),
        "" => "File".to_string(),
        other => format!("{} File", other.to_uppercase()),
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
