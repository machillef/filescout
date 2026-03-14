//! Preview panel — renders the content of the selected file.

use filescout_core::preview::PreviewContent;
use iced::widget::image::Handle as ImageHandle;
use iced::widget::{column, container, image as img_widget, scrollable, text};
use iced::{Element, Font, Length};

/// Render the preview panel for the given `content`.
///
/// Returns an `Element` using generic message type `M` so the caller can `.map()` it
/// into the appropriate message variant without extra wrapping.
pub fn preview_panel_view<M: 'static>(content: &PreviewContent) -> Element<'_, M> {
    let body: Element<'_, M> = match content {
        PreviewContent::None => centered_label("No file selected"),
        PreviewContent::Loading => centered_label("Loading…"),
        PreviewContent::Text { content: text_content, truncated } => {
            let mut lines = text_content.as_str();
            // Trim trailing whitespace for a tidier display.
            lines = lines.trim_end();

            let display = if *truncated {
                format!("{lines}\n\n[preview truncated at 16 KB]")
            } else {
                lines.to_owned()
            };

            let t = text(display).size(12).font(Font::MONOSPACE);
            scrollable(
                container(t).padding([4, 8]).width(Length::Fill),
            )
            .width(Length::Fill)
            .height(Length::Fill)
            .into()
        }
        PreviewContent::Image(path) => {
            let handle = ImageHandle::from_path(path);
            container(img_widget(handle).width(Length::Fill))
                .width(Length::Fill)
                .height(Length::Fill)
                .padding(4)
                .center(Length::Fill)
                .into()
        }
        PreviewContent::Metadata { kind, size } => {
            let size_str = format_size(*size);
            column![
                text(format!("Type:  {kind}")).size(13),
                text(format!("Size:  {size_str}")).size(13),
            ]
            .spacing(6)
            .padding(8)
            .into()
        }
        PreviewContent::Error(msg) => {
            text(format!("⚠ {msg}")).size(12).into()
        }
    };

    container(body)
        .width(Length::Fill)
        .height(Length::Fill)
        .padding(6)
        .style(container::bordered_box)
        .into()
}

fn centered_label<M: 'static>(label: &str) -> Element<'static, M> {
    container(text(label.to_owned()).size(13))
        .center(Length::Fill)
        .width(Length::Fill)
        .height(Length::Fill)
        .into()
}

fn format_size(bytes: u64) -> String {
    const KB: u64 = 1024;
    const MB: u64 = KB * 1024;
    const GB: u64 = MB * 1024;

    if bytes == 0 {
        "—".to_owned()
    } else if bytes >= GB {
        format!("{:.1} GB", bytes as f64 / GB as f64)
    } else if bytes >= MB {
        format!("{:.1} MB", bytes as f64 / MB as f64)
    } else if bytes >= KB {
        format!("{:.1} KB", bytes as f64 / KB as f64)
    } else {
        format!("{bytes} B")
    }
}
