use iced::widget::{button, container, row, text};
use iced::{Alignment, Border, Element, Length, Theme};

use crate::theme;

/// Messages emitted by the tab bar component.
#[derive(Debug, Clone)]
pub enum TabBarMessage {
    /// Switch to the tab at `idx`.
    Switch(usize),
    /// Close the tab at `idx`.
    Close(usize),
    /// Open a new tab at the current path.
    New,
}

/// Render a horizontal tab bar.
///
/// Each tab shows its title and a close button (disabled when only one tab remains).
/// Active tab has a bottom accent border. A `+` button at the end opens a new tab.
pub fn tab_bar_view(tab_titles: Vec<String>, active: usize) -> Element<'static, TabBarMessage> {
    let can_close = tab_titles.len() > 1;
    let mut elements: Vec<Element<'static, TabBarMessage>> = Vec::new();

    for (idx, title) in tab_titles.into_iter().enumerate() {
        let is_active = idx == active;

        let close_btn = {
            let b = button(text("×").size(12))
                .style(button::text)
                .padding([2, 5]);
            if can_close {
                b.on_press(TabBarMessage::Close(idx))
            } else {
                b
            }
        };

        let title_btn = button(
            row![text(title).size(13), close_btn]
                .spacing(2)
                .align_y(Alignment::Center),
        )
        .on_press(TabBarMessage::Switch(idx))
        .style(if is_active { button::primary } else { button::text })
        .padding([4, 10]);

        if is_active {
            // Active tab: wrap with bottom accent border
            elements.push(
                container(title_btn)
                    .style(active_tab_indicator_style)
                    .into(),
            );
        } else {
            elements.push(title_btn.into());
        }
    }

    elements.push(
        button(text("+").size(14))
            .on_press(TabBarMessage::New)
            .style(button::text)
            .padding([4, 8])
            .into(),
    );

    row(elements)
        .spacing(2)
        .align_y(Alignment::Center)
        .width(Length::Fill)
        .into()
}

fn active_tab_indicator_style(t: &Theme) -> container::Style {
    let accent = if theme::is_dark_theme(t) { theme::dark::ACCENT } else { theme::light::ACCENT };
    container::Style {
        border: Border {
            color: accent,
            width: 2.0,
            radius: 0.0.into(),
        },
        ..Default::default()
    }
}
