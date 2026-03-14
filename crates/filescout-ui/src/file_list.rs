use filescout_core::FileEntry;
use iced::widget::{column, row, scrollable, text};
use iced::Element;

/// Render a list of file entries as a scrollable column.
pub fn file_list_view<'a, Message: 'a>(entries: &'a [FileEntry]) -> Element<'a, Message> {
    let header = row![
        text("Name").width(300),
        text("Size").width(100),
        text("Modified").width(200),
    ]
    .spacing(10)
    .padding(5);

    let rows: Vec<Element<'a, Message>> = entries
        .iter()
        .map(|entry| {
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

            row![
                text(format!("{icon} {}", entry.name)).width(300),
                text(size_text).width(100),
                text(modified_text).width(200),
            ]
            .spacing(10)
            .padding(2)
            .into()
        })
        .collect();

    let list = column(rows).spacing(1);

    scrollable(column![header, list].spacing(5)).into()
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
