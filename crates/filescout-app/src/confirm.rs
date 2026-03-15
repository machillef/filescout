//! Confirmation dialog overlay — "Are you sure?" modal before destructive operations.

use std::path::PathBuf;

use iced::widget::{button, column, container, row, text};
use iced::{Background, Color, Element, Length, Theme};

/// What action to perform if the user confirms.
#[derive(Debug, Clone)]
pub enum ConfirmAction {
    /// Delete the given paths to trash.
    DeletePaths(Vec<PathBuf>),
}

/// State for the confirmation dialog overlay.
pub struct ConfirmState {
    pub title: String,
    pub message: String,
    pub action: ConfirmAction,
}

impl ConfirmState {
    /// Build a confirmation dialog for deleting paths.
    pub fn for_delete(paths: Vec<PathBuf>) -> Self {
        let (title, message) = if paths.len() == 1 {
            let name = paths[0]
                .file_name()
                .map(|n| n.to_string_lossy().into_owned())
                .unwrap_or_else(|| paths[0].display().to_string());
            (
                "Delete Item".to_string(),
                format!("Are you sure you want to delete '{name}'?"),
            )
        } else {
            (
                "Delete Items".to_string(),
                format!("Are you sure you want to delete {} items?", paths.len()),
            )
        };
        Self {
            title,
            message,
            action: ConfirmAction::DeletePaths(paths),
        }
    }
}

/// Messages emitted by the confirmation dialog.
#[derive(Debug, Clone)]
pub enum ConfirmMessage {
    /// User confirmed the action.
    Confirm,
    /// User cancelled.
    Cancel,
}

fn backdrop_style(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(Background::Color(Color::from_rgba(0.0, 0.0, 0.0, 0.7))),
        ..Default::default()
    }
}

/// Renders the confirmation dialog as a centered modal overlay.
pub fn confirm_view(state: &ConfirmState) -> Element<'_, ConfirmMessage> {
    let buttons = row![
        button(text("Cancel").size(14))
            .on_press(ConfirmMessage::Cancel)
            .style(button::secondary)
            .padding([8, 20]),
        button(text("Delete").size(14))
            .on_press(ConfirmMessage::Confirm)
            .style(button::danger)
            .padding([8, 20]),
    ]
    .spacing(12);

    let dialog = container(
        column![
            text(&state.title).size(18),
            text(&state.message).size(14),
            buttons,
        ]
        .spacing(16)
        .padding(8),
    )
    .width(420)
    .padding(20)
    .style(container::rounded_box);

    container(dialog)
        .center(Length::Fill)
        .style(backdrop_style)
        .into()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn for_delete_single_item_title() {
        let state = ConfirmState::for_delete(vec![PathBuf::from("C:\\docs\\readme.txt")]);
        assert_eq!(state.title, "Delete Item");
        assert!(state.message.contains("readme.txt"));
    }

    #[test]
    fn for_delete_multiple_items_title() {
        let paths = vec![
            PathBuf::from("a.txt"),
            PathBuf::from("b.txt"),
            PathBuf::from("c.txt"),
        ];
        let state = ConfirmState::for_delete(paths);
        assert_eq!(state.title, "Delete Items");
        assert!(state.message.contains("3 items"));
    }

    #[test]
    fn confirm_action_contains_paths() {
        let paths = vec![PathBuf::from("a.txt")];
        let state = ConfirmState::for_delete(paths.clone());
        match state.action {
            ConfirmAction::DeletePaths(p) => assert_eq!(p, paths),
        }
    }

    #[test]
    fn for_delete_empty_paths() {
        let state = ConfirmState::for_delete(vec![]);
        assert!(state.message.contains("0 items"));
    }
}
