use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};

use filescout_core::{FileEntry, SortColumn, SortDirection, ViewMode};
use iced::widget::scrollable::Viewport;
use iced::widget::{button, column, container, image, row, scrollable, text, text_input, Space};
use iced::{Element, Length};
use iced::widget::Id;

use crate::theme;

use crate::icons::file_icon_text;
use filescout_core::win32_icons::DIR_ICON_KEY;

/// Unique ID for the inline rename text input (used for programmatic focus).
pub const RENAME_INPUT_ID: &str = "rename_input";

/// Fixed row height in pixels for virtual scrolling calculations.
pub const ROW_HEIGHT: f32 = 28.0;

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
    /// The scrollable viewport changed (scroll or resize).
    ScrollChanged(Viewport),
}

// Column width proportions (FillPortion values).
const NAME_PORTION: u16 = 6;
const EXT_PORTION: u16 = 1;
const TYPE_PORTION: u16 = 2;
const SIZE_PORTION: u16 = 2;
const MODIFIED_PORTION: u16 = 3;

/// Render a sortable, scrollable list of file entries with virtual scrolling.
///
/// Only renders rows visible in the viewport, using spacers above and below
/// to maintain correct scroll height and position.
///
/// - `parent_path` — if `Some`, a `..` row is shown at the top (index 0).
/// - `sort_col` / `sort_dir` — current sort state for column indicators.
/// - `selection` — set of selected paths (for multi-row highlight).
/// - `renaming` — if `Some((path, text))`, that row shows an inline text input.
/// - `scroll_offset` — current vertical scroll offset in pixels.
/// - `viewport_height` — current viewport height in pixels.
#[allow(clippy::too_many_arguments)]
pub fn file_list_view<'a>(
    entries: &'a [FileEntry],
    parent_path: Option<PathBuf>,
    sort_col: &'a SortColumn,
    sort_dir: &'a SortDirection,
    selection: &'a HashSet<PathBuf>,
    renaming: Option<(&'a Path, &'a str)>,
    view_mode: &'a ViewMode,
    pane_id: &'static str,
    scroll_offset: f32,
    viewport_height: f32,
    icon_cache: &'a HashMap<String, image::Handle>,
) -> Element<'a, FileListMessage> {
    let is_details = *view_mode == ViewMode::Details;

    // --- Header row (fixed, outside scrollable) ---
    let header = build_header(sort_col, sort_dir, is_details);

    // --- Virtual scrolling calculation ---
    let has_parent = parent_path.is_some();
    let parent_count: usize = if has_parent { 1 } else { 0 };
    let total_rows = parent_count + entries.len();

    // Row spacing is 1px between rows
    let row_with_spacing = ROW_HEIGHT + 1.0;
    // Calculate visible range with 2-row buffer on each side
    let first_visible = ((scroll_offset / row_with_spacing).floor() as usize).saturating_sub(2);
    let visible_count = (viewport_height / row_with_spacing).ceil() as usize + 5;
    let last_visible = (first_visible + visible_count).min(total_rows);

    // --- Build only visible rows ---
    let mut rows: Vec<Element<'a, FileListMessage>> = Vec::with_capacity(visible_count + 2);

    // Top spacer
    let top_spacer_height = first_visible as f32 * row_with_spacing;
    if top_spacer_height > 0.0 {
        rows.push(Space::new().width(Length::Fill).height(top_spacer_height).into());
    }

    for idx in first_visible..last_visible {
        if idx < parent_count {
            // ".." parent row
            if let Some(ref parent) = parent_path {
                rows.push(build_parent_row(parent.clone(), is_details));
            }
        } else {
            let entry_idx = idx - parent_count;
            if let Some(entry) = entries.get(entry_idx) {
                let is_selected = selection.contains(&entry.path);
                let is_renaming = renaming.is_some_and(|(rp, _)| rp == entry.path);

                if is_renaming {
                    let (_, rename_text) = renaming.unwrap();
                    rows.push(build_rename_row(entry, rename_text, is_details));
                } else {
                    rows.push(build_entry_row(entry, is_selected, is_details, idx, icon_cache));
                }
            }
        }
    }

    // Bottom spacer
    let bottom_spacer_height = (total_rows.saturating_sub(last_visible)) as f32 * row_with_spacing;
    if bottom_spacer_height > 0.0 {
        rows.push(Space::new().width(Length::Fill).height(bottom_spacer_height).into());
    }

    let list = column(rows).spacing(1);

    let scrollable_list = scrollable(list)
        .id(Id::new(pane_id))
        .on_scroll(FileListMessage::ScrollChanged);

    column![header, scrollable_list]
        .spacing(5)
        .into()
}

/// Calculate the total number of logical rows (parent + entries).
pub fn total_row_count(entries_len: usize, has_parent: bool) -> usize {
    let parent_count: usize = if has_parent { 1 } else { 0 };
    parent_count + entries_len
}

// --- Private helpers ---

fn build_header<'a>(
    sort_col: &'a SortColumn,
    sort_dir: &'a SortDirection,
    is_details: bool,
) -> Element<'a, FileListMessage> {
    let mut header_items: Vec<Element<'a, FileListMessage>> = vec![
        button(text(format!("Name{}", sort_indicator(sort_col, sort_dir, &SortColumn::Name))))
            .on_press(FileListMessage::Sort(SortColumn::Name))
            .style(button::text)
            .width(Length::FillPortion(NAME_PORTION))
            .into(),
    ];
    if is_details {
        header_items.push(
            text("Ext")
                .width(Length::FillPortion(EXT_PORTION))
                .into(),
        );
        header_items.push(
            text("Type")
                .width(Length::FillPortion(TYPE_PORTION))
                .into(),
        );
    }
    header_items.push(
        button(text(format!("Size{}", sort_indicator(sort_col, sort_dir, &SortColumn::Size))))
            .on_press(FileListMessage::Sort(SortColumn::Size))
            .style(button::text)
            .width(Length::FillPortion(SIZE_PORTION))
            .into(),
    );
    header_items.push(
        button(text(format!("Modified{}", sort_indicator(sort_col, sort_dir, &SortColumn::Modified))))
            .on_press(FileListMessage::Sort(SortColumn::Modified))
            .style(button::text)
            .width(Length::FillPortion(MODIFIED_PORTION))
            .into(),
    );

    container(row(header_items).spacing(10).padding(5))
        .width(Length::Fill)
        .style(theme::file_header_style)
        .into()
}

fn build_parent_row(parent: PathBuf, is_details: bool) -> Element<'static, FileListMessage> {
    let mut up_items: Vec<Element<'static, FileListMessage>> = vec![
        text("\u{1F4C1} ..")
            .width(Length::FillPortion(NAME_PORTION))
            .into(),
    ];
    if is_details {
        up_items.push(text("").width(Length::FillPortion(EXT_PORTION)).into());
        up_items.push(text("").width(Length::FillPortion(TYPE_PORTION)).into());
    }
    up_items.push(text("").width(Length::FillPortion(SIZE_PORTION)).into());
    up_items.push(text("").width(Length::FillPortion(MODIFIED_PORTION)).into());

    button(row(up_items).spacing(10).padding(2))
        .on_press(FileListMessage::Navigate(parent))
        .style(button::text)
        .width(Length::Fill)
        .into()
}

fn build_rename_row<'a>(
    entry: &'a FileEntry,
    rename_text: &'a str,
    is_details: bool,
) -> Element<'a, FileListMessage> {
    let size_text = format_size_for(entry);
    let modified_text = format_modified(entry);
    let ext_text = if is_details { extract_extension(&entry.name) } else { String::new() };
    let type_text = if is_details { file_type_description(entry) } else { String::new() };

    let mut rename_items: Vec<Element<'a, FileListMessage>> = vec![
        text_input("New name…", rename_text)
            .id(Id::new(RENAME_INPUT_ID))
            .on_input(FileListMessage::RenameEdit)
            .width(Length::FillPortion(NAME_PORTION))
            .into(),
    ];
    if is_details {
        rename_items.push(text(ext_text).width(Length::FillPortion(EXT_PORTION)).into());
        rename_items.push(text(type_text).width(Length::FillPortion(TYPE_PORTION)).into());
    }
    rename_items.push(text(size_text).width(Length::FillPortion(SIZE_PORTION)).into());
    rename_items.push(text(modified_text).width(Length::FillPortion(MODIFIED_PORTION)).into());

    container(row(rename_items).spacing(10).padding(2))
        .style(container::bordered_box)
        .width(Length::Fill)
        .into()
}

fn build_entry_row<'a>(
    entry: &'a FileEntry,
    is_selected: bool,
    is_details: bool,
    row_idx: usize,
    icon_cache: &'a HashMap<String, image::Handle>,
) -> Element<'a, FileListMessage> {
    let size_text = format_size_for(entry);
    let modified_text = format_modified(entry);
    let ext_text = if is_details { extract_extension(&entry.name) } else { String::new() };
    let type_text = if is_details { file_type_description(entry) } else { String::new() };

    // Look up native icon: directories use DIR_ICON_KEY, files use their extension
    let icon_key = if entry.is_dir {
        DIR_ICON_KEY.to_string()
    } else {
        extract_extension(&entry.name)
    };

    let name_cell: Element<'a, FileListMessage> = if let Some(handle) = icon_cache.get(&icon_key) {
        row![
            image(handle.clone())
                .width(Length::Fixed(16.0))
                .height(Length::Fixed(16.0)),
            text(&entry.name),
        ]
        .spacing(4)
        .align_y(iced::Alignment::Center)
        .width(Length::FillPortion(NAME_PORTION))
        .into()
    } else {
        let icon = file_icon_text(entry);
        text(format!("{icon} {}", entry.name))
            .width(Length::FillPortion(NAME_PORTION))
            .into()
    };

    let mut row_items: Vec<Element<'a, FileListMessage>> = vec![name_cell];
    if is_details {
        row_items.push(text(ext_text).width(Length::FillPortion(EXT_PORTION)).into());
        row_items.push(text(type_text).width(Length::FillPortion(TYPE_PORTION)).into());
    }
    row_items.push(text(size_text).width(Length::FillPortion(SIZE_PORTION)).into());
    row_items.push(text(modified_text).width(Length::FillPortion(MODIFIED_PORTION)).into());

    let row_content = row(row_items).spacing(10).padding(2);
    let path_clone = entry.path.clone();

    if is_selected {
        // Selected row: accent background via theme style
        let btn: Element<'a, FileListMessage> = button(row_content)
            .on_press(FileListMessage::Select(path_clone))
            .style(|theme, status| {
                let mut style = theme::file_row_selected_style(theme);
                if matches!(status, button::Status::Hovered) {
                    style.background = Some(iced::Background::Color(
                        if theme::is_dark_theme(theme) { theme::dark::ACCENT_HOVER } else { theme::light::ACCENT_HOVER }
                    ));
                }
                style
            })
            .width(Length::Fill)
            .into();
        btn
    } else if row_idx % 2 == 1 {
        // Alternating row: subtle zebra background
        container(
            button(row_content)
                .on_press(FileListMessage::Select(path_clone))
                .style(button::text)
                .width(Length::Fill),
        )
        .style(theme::file_row_alt_style)
        .width(Length::Fill)
        .into()
    } else {
        button(row_content)
            .on_press(FileListMessage::Select(path_clone))
            .style(button::text)
            .width(Length::Fill)
            .into()
    }
}

fn format_size_for(entry: &FileEntry) -> String {
    if entry.is_dir {
        "<DIR>".to_string()
    } else {
        format_size(entry.size)
    }
}

fn format_modified(entry: &FileEntry) -> String {
    entry
        .modified
        .map(|dt| dt.format("%Y-%m-%d %H:%M").to_string())
        .unwrap_or_default()
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn row_height_is_positive() {
        assert!(ROW_HEIGHT > 0.0);
    }

    #[test]
    fn total_row_count_without_parent() {
        assert_eq!(total_row_count(10, false), 10);
    }

    #[test]
    fn total_row_count_with_parent() {
        assert_eq!(total_row_count(10, true), 11);
    }

    #[test]
    fn total_row_count_empty() {
        assert_eq!(total_row_count(0, false), 0);
        assert_eq!(total_row_count(0, true), 1);
    }

    #[test]
    fn format_size_bytes() {
        assert_eq!(format_size(500), "500 B");
    }

    #[test]
    fn format_size_kilobytes() {
        assert_eq!(format_size(2048), "2.0 KB");
    }

    #[test]
    fn format_size_megabytes() {
        assert_eq!(format_size(5 * 1024 * 1024), "5.0 MB");
    }

    #[test]
    fn format_size_gigabytes() {
        assert_eq!(format_size(3 * 1024 * 1024 * 1024), "3.0 GB");
    }

    #[test]
    fn extract_extension_normal() {
        assert_eq!(extract_extension("test.rs"), "rs");
    }

    #[test]
    fn extract_extension_no_dot() {
        assert_eq!(extract_extension("makefile"), "");
    }

    #[test]
    fn extract_extension_uppercase() {
        assert_eq!(extract_extension("IMAGE.PNG"), "png");
    }

    #[test]
    fn file_type_folder() {
        let entry = FileEntry {
            name: "src".to_string(),
            path: PathBuf::from("src"),
            is_dir: true,
            size: 0,
            modified: None,
            is_hidden: false,
        };
        assert_eq!(file_type_description(&entry), "Folder");
    }

    #[test]
    fn file_type_rust() {
        let entry = FileEntry {
            name: "main.rs".to_string(),
            path: PathBuf::from("main.rs"),
            is_dir: false,
            size: 100,
            modified: None,
            is_hidden: false,
        };
        assert_eq!(file_type_description(&entry), "Rust Source");
    }

    #[test]
    fn file_type_unknown_extension() {
        let entry = FileEntry {
            name: "data.xyz".to_string(),
            path: PathBuf::from("data.xyz"),
            is_dir: false,
            size: 100,
            modified: None,
            is_hidden: false,
        };
        assert_eq!(file_type_description(&entry), "XYZ File");
    }
}
