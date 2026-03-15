//! Right-click context menu overlay.

use iced::widget::{button, column, container, text};
use iced::{Background, Border, Color, Element, Length, Padding, Point, Theme};

/// Actions available from the context menu.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ContextAction {
    Open,
    OpenWith,
    OpenInTerminal,
    Copy,
    Cut,
    Paste,
    Delete,
    Rename,
    Properties,
    WindowsProperties,
    Refresh,
    NewFolder,
}

/// A single menu item.
struct ContextMenuItem {
    label: &'static str,
    action: ContextAction,
}

/// Context menu state.
pub struct ContextMenuState {
    pub position: Point,
    items: Vec<ContextMenuItem>,
    pub selected: usize,
}

impl ContextMenuState {
    /// Build a context menu for the current selection state.
    pub fn new(position: Point, has_selection: bool, has_clipboard: bool) -> Self {
        let mut items = Vec::new();
        if has_selection {
            items.push(ContextMenuItem { label: "Open", action: ContextAction::Open });
            items.push(ContextMenuItem { label: "Open With...", action: ContextAction::OpenWith });
            items.push(ContextMenuItem { label: "Copy", action: ContextAction::Copy });
            items.push(ContextMenuItem { label: "Cut", action: ContextAction::Cut });
        }
        if has_clipboard {
            items.push(ContextMenuItem { label: "Paste", action: ContextAction::Paste });
        }
        if has_selection {
            items.push(ContextMenuItem { label: "Delete", action: ContextAction::Delete });
            items.push(ContextMenuItem { label: "Rename", action: ContextAction::Rename });
            items.push(ContextMenuItem { label: "Properties", action: ContextAction::Properties });
            items.push(ContextMenuItem { label: "Windows Properties", action: ContextAction::WindowsProperties });
        }
        // Always available actions (even when right-clicking empty space).
        items.push(ContextMenuItem { label: "Open in Terminal", action: ContextAction::OpenInTerminal });
        items.push(ContextMenuItem { label: "New Folder", action: ContextAction::NewFolder });
        items.push(ContextMenuItem { label: "Refresh", action: ContextAction::Refresh });
        Self {
            position,
            items,
            selected: 0,
        }
    }

    #[cfg(test)]
    pub fn item_count(&self) -> usize {
        self.items.len()
    }

    pub fn move_up(&mut self) {
        self.selected = self.selected.saturating_sub(1);
    }

    pub fn move_down(&mut self) {
        if !self.items.is_empty() {
            self.selected = (self.selected + 1).min(self.items.len() - 1);
        }
    }

    pub fn selected_action(&self) -> Option<ContextAction> {
        self.items.get(self.selected).map(|item| item.action)
    }
}

/// Messages emitted by the context menu.
#[derive(Debug, Clone)]
pub enum ContextMenuMessage {
    Select(ContextAction),
    MoveUp,
    MoveDown,
    Execute,
    Close,
}

fn menu_backdrop_style(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(Background::Color(Color::from_rgba(0.0, 0.0, 0.0, 0.3))),
        ..Default::default()
    }
}

/// Renders the context menu as a positioned overlay.
pub fn context_menu_view(state: &ContextMenuState) -> Element<'_, ContextMenuMessage> {
    let items: Vec<Element<'_, ContextMenuMessage>> = state
        .items
        .iter()
        .enumerate()
        .map(|(i, item)| {
            let is_keyboard_selected = i == state.selected;
            button(text(item.label).size(13))
                .on_press(ContextMenuMessage::Select(item.action))
                .style(move |theme: &Theme, status| {
                    let palette = theme.palette();
                    match status {
                        button::Status::Hovered | button::Status::Pressed => button::Style {
                            background: Some(Background::Color(palette.primary)),
                            text_color: Color::WHITE,
                            border: Border::default().rounded(4),
                            ..Default::default()
                        },
                        _ if is_keyboard_selected => button::Style {
                            background: Some(Background::Color(Color {
                                a: 0.3,
                                ..palette.primary
                            })),
                            text_color: palette.text,
                            border: Border::default().rounded(4),
                            ..Default::default()
                        },
                        _ => button::Style {
                            background: Some(Background::Color(Color::TRANSPARENT)),
                            text_color: palette.text,
                            border: Border::default().rounded(4),
                            ..Default::default()
                        },
                    }
                })
                .width(Length::Fill)
                .padding([6, 16])
                .into()
        })
        .collect();

    let menu = container(column(items).spacing(1))
        .width(160)
        .padding(4)
        .style(container::rounded_box);

    // Position the menu using padding from top-left.
    let x_pad = state.position.x.max(0.0);
    let y_pad = state.position.y.max(0.0);
    let positioned = container(menu)
        .padding(Padding {
            top: y_pad,
            right: 0.0,
            bottom: 0.0,
            left: x_pad,
        })
        .width(Length::Fill)
        .height(Length::Fill);

    container(positioned)
        .width(Length::Fill)
        .height(Length::Fill)
        .style(menu_backdrop_style)
        .into()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn menu_with_selection_has_all_items() {
        let state = ContextMenuState::new(Point::new(100.0, 200.0), true, false);
        // Open, OpenWith, Copy, Cut, Delete, Rename, Properties, WinProperties + OpenInTerminal, NewFolder, Refresh
        assert_eq!(state.item_count(), 11);
    }

    #[test]
    fn menu_with_selection_and_clipboard() {
        let state = ContextMenuState::new(Point::new(0.0, 0.0), true, true);
        // + Paste
        assert_eq!(state.item_count(), 12);
    }

    #[test]
    fn menu_without_selection_paste_only() {
        let state = ContextMenuState::new(Point::new(0.0, 0.0), false, true);
        // Paste + OpenInTerminal + New Folder + Refresh
        assert_eq!(state.item_count(), 4);
    }

    #[test]
    fn menu_has_new_folder_and_refresh_without_selection() {
        let state = ContextMenuState::new(Point::new(0.0, 0.0), false, false);
        // OpenInTerminal + New Folder + Refresh always present
        assert_eq!(state.item_count(), 3);
    }

    #[test]
    fn move_down_advances() {
        let mut state = ContextMenuState::new(Point::new(0.0, 0.0), true, false);
        state.move_down();
        assert_eq!(state.selected, 1);
    }

    #[test]
    fn move_up_saturates() {
        let mut state = ContextMenuState::new(Point::new(0.0, 0.0), true, false);
        state.move_up();
        assert_eq!(state.selected, 0);
    }

    #[test]
    fn selected_action_returns_first_by_default() {
        let state = ContextMenuState::new(Point::new(0.0, 0.0), true, false);
        assert_eq!(state.selected_action(), Some(ContextAction::Open));
    }
}
