//! Bookmarks overlay — shows saved directory shortcuts, lets user navigate or remove them.
//!
//! Opened with Ctrl+B; directory bookmarked with Ctrl+D.
//! Layout mirrors the command palette pattern: full-screen dimmed backdrop + centered card.

use std::path::PathBuf;

use filescout_core::Bookmarks;
use iced::widget::{button, column, container, row, scrollable, text};
use iced::{Background, Color, Element, Length, Theme};

/// Messages emitted by the bookmarks overlay.
#[derive(Debug, Clone)]
pub enum BookmarkMessage {
    /// Navigate the active pane to the given bookmark path and close the overlay.
    Navigate(PathBuf),
    /// Remove the bookmark at the given index.
    Remove(usize),
    /// Close without navigating.
    Close,
}

fn backdrop_style(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(Background::Color(Color::from_rgba(0.0, 0.0, 0.0, 0.7))),
        ..Default::default()
    }
}

/// Renders the bookmarks overlay as a full-screen overlay with a dimmed backdrop.
///
/// `current_path` is used to show a visual indicator when the user is already
/// inside a bookmarked directory.
pub fn bookmarks_view<'a>(
    bookmarks: &'a Bookmarks,
    current_path: &'a PathBuf,
) -> Element<'a, BookmarkMessage> {
    let items: Vec<Element<'_, BookmarkMessage>> = if bookmarks.is_empty() {
        vec![
            container(text("No bookmarks yet.  Press Ctrl+D to bookmark the current directory.").size(13))
                .padding([8, 12])
                .into(),
        ]
    } else {
        bookmarks
            .items
            .iter()
            .enumerate()
            .map(|(i, bm)| {
                let is_current = &bm.path == current_path;
                let path_display = bm.path.display().to_string();

                let name_label = if is_current {
                    text(format!("▶ {}", bm.name)).size(14)
                } else {
                    text(bm.name.clone()).size(14)
                };

                let path_label = text(path_display).size(11);

                let navigate_btn = button(
                    column![name_label, path_label].spacing(2).padding([0, 4]),
                )
                .on_press(BookmarkMessage::Navigate(bm.path.clone()))
                .style(button::text)
                .width(Length::Fill)
                .padding([6, 12]);

                let remove_btn = button(text("✕").size(12))
                    .on_press(BookmarkMessage::Remove(i))
                    .style(button::text)
                    .padding([6, 8]);

                row![navigate_btn, remove_btn]
                    .width(Length::Fill)
                    .align_y(iced::alignment::Vertical::Center)
                    .into()
            })
            .collect()
    };

    let list = scrollable(column(items).spacing(2).width(Length::Fill)).height(320);

    let header = row![
        text("Bookmarks").size(15),
        button(text("✕").size(12))
            .on_press(BookmarkMessage::Close)
            .style(button::text)
            .padding([2, 8]),
    ]
    .width(Length::Fill)
    .align_y(iced::alignment::Vertical::Center);

    let card = container(column![header, list].spacing(4))
        .width(500)
        .padding(16)
        .style(container::rounded_box);

    container(card)
        .center(Length::Fill)
        .style(backdrop_style)
        .into()
}
