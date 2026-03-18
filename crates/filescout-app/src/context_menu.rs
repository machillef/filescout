//! Right-click context menu overlay.

use filescout_core::win32_context_menu::ShellContextCommand;
use iced::mouse;
use iced::widget::{button, column, container, mouse_area, row, scrollable, text};
use iced::{Alignment, Background, Border, Color, Element, Length, Padding, Point, Theme};

/// Actions available from the context menu.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ContextAction {
    Open,
    OpenWith,
    OpenInTerminal,
    MoreWindowsActions,
    Copy,
    Cut,
    Paste,
    Delete,
    Rename,
    Properties,
    WindowsProperties,
    Refresh,
    NewFolder,
    ShellVerb(String),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum ActionTone {
    Default,
    Windows,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct MenuActionEntry {
    title: String,
    subtitle: Option<String>,
    shortcut: Option<&'static str>,
    action: ContextAction,
    tone: ActionTone,
}

#[derive(Debug, Clone, PartialEq, Eq)]
enum ContextMenuEntry {
    Section(&'static str),
    Action(MenuActionEntry),
}

/// Context menu state.
pub struct ContextMenuState {
    pub position: Point,
    entries: Vec<ContextMenuEntry>,
    pub selected: usize,
}

impl ContextMenuState {
    /// Build a context menu for the current selection state.
    pub fn new(
        position: Point,
        has_selection: bool,
        can_open_with: bool,
        has_clipboard: bool,
        shell_commands: Vec<ShellContextCommand>,
        has_windows_fallback: bool,
    ) -> Self {
        let mut entries = Vec::new();
        if has_selection {
            push_section(&mut entries, "File");
            push_action(
                &mut entries,
                "Open",
                None::<String>,
                Some("Enter"),
                ContextAction::Open,
                ActionTone::Default,
            );
            if can_open_with {
                push_action(
                    &mut entries,
                    "Open With...",
                    None::<String>,
                    None,
                    ContextAction::OpenWith,
                    ActionTone::Default,
                );
            }
            push_action(
                &mut entries,
                "Rename",
                None::<String>,
                Some("F2"),
                ContextAction::Rename,
                ActionTone::Default,
            );
            push_action(
                &mut entries,
                "Delete",
                None::<String>,
                Some("F8"),
                ContextAction::Delete,
                ActionTone::Default,
            );
        }
        if has_selection || has_clipboard {
            push_section(&mut entries, "Clipboard");
        }
        if has_selection {
            push_action(
                &mut entries,
                "Copy",
                None::<String>,
                Some("F5"),
                ContextAction::Copy,
                ActionTone::Default,
            );
            push_action(
                &mut entries,
                "Cut",
                None::<String>,
                Some("F6"),
                ContextAction::Cut,
                ActionTone::Default,
            );
        }
        if has_clipboard {
            push_action(
                &mut entries,
                "Paste",
                None::<String>,
                None,
                ContextAction::Paste,
                ActionTone::Default,
            );
        }
        if has_selection {
            push_section(&mut entries, "Inspect");
            push_action(
                &mut entries,
                "Properties",
                None::<String>,
                Some("Alt+Enter"),
                ContextAction::Properties,
                ActionTone::Default,
            );
            push_action(
                &mut entries,
                "Windows Properties",
                None::<String>,
                None,
                ContextAction::WindowsProperties,
                ActionTone::Windows,
            );
        }
        if !shell_commands.is_empty() {
            push_section(&mut entries, "Suggested Windows Actions");
            for command in shell_commands {
                let subtitle = format!("Windows shell action ({})", command.canonical_verb);
                push_action(
                    &mut entries,
                    command.label,
                    Some(subtitle),
                    None,
                    ContextAction::ShellVerb(command.canonical_verb),
                    ActionTone::Windows,
                );
            }
        }
        if has_windows_fallback {
            push_section(&mut entries, "Windows");
            push_action(
                &mut entries,
                "More Windows actions...",
                None::<String>,
                None,
                ContextAction::MoreWindowsActions,
                ActionTone::Windows,
            );
        }
        push_section(&mut entries, "Workspace");
        push_action(
            &mut entries,
            "Open in Terminal",
            None::<String>,
            None,
            ContextAction::OpenInTerminal,
            ActionTone::Default,
        );
        push_action(
            &mut entries,
            "New Folder",
            None::<String>,
            None,
            ContextAction::NewFolder,
            ActionTone::Default,
        );
        push_action(
            &mut entries,
            "Refresh",
            None::<String>,
            Some("F5"),
            ContextAction::Refresh,
            ActionTone::Default,
        );

        let selected = first_action_index(&entries).unwrap_or(0);

        Self {
            position,
            entries,
            selected,
        }
    }

    #[cfg(test)]
    pub fn entry_count(&self) -> usize {
        self.entries.len()
    }

    #[cfg(test)]
    pub fn section_count(&self) -> usize {
        self.entries
            .iter()
            .filter(|entry| matches!(entry, ContextMenuEntry::Section(_)))
            .count()
    }

    pub fn move_up(&mut self) {
        if let Some(index) = previous_action_index(&self.entries, self.selected) {
            self.selected = index;
        }
    }

    pub fn move_down(&mut self) {
        if let Some(index) = next_action_index(&self.entries, self.selected) {
            self.selected = index;
        }
    }

    pub fn selected_action(&self) -> Option<ContextAction> {
        match self.entries.get(self.selected) {
            Some(ContextMenuEntry::Action(entry)) => Some(entry.action.clone()),
            _ => None,
        }
    }
}

fn push_section(entries: &mut Vec<ContextMenuEntry>, label: &'static str) {
    if matches!(entries.last(), Some(ContextMenuEntry::Section(_))) {
        return;
    }
    entries.push(ContextMenuEntry::Section(label));
}

fn push_action(
    entries: &mut Vec<ContextMenuEntry>,
    title: impl Into<String>,
    subtitle: Option<impl Into<String>>,
    shortcut: Option<&'static str>,
    action: ContextAction,
    tone: ActionTone,
) {
    entries.push(ContextMenuEntry::Action(MenuActionEntry {
        title: title.into(),
        subtitle: subtitle.map(Into::into),
        shortcut,
        action,
        tone,
    }));
}

fn first_action_index(entries: &[ContextMenuEntry]) -> Option<usize> {
    entries
        .iter()
        .position(|entry| matches!(entry, ContextMenuEntry::Action(_)))
}

fn next_action_index(entries: &[ContextMenuEntry], current: usize) -> Option<usize> {
    entries
        .iter()
        .enumerate()
        .skip(current + 1)
        .find(|(_, entry)| matches!(entry, ContextMenuEntry::Action(_)))
        .map(|(index, _)| index)
}

fn previous_action_index(entries: &[ContextMenuEntry], current: usize) -> Option<usize> {
    entries
        .iter()
        .enumerate()
        .take(current)
        .rev()
        .find(|(_, entry)| matches!(entry, ContextMenuEntry::Action(_)))
        .map(|(index, _)| index)
}

/// Messages emitted by the context menu.
#[derive(Debug, Clone)]
pub enum ContextMenuMessage {
    Select(ContextAction),
    MoveUp,
    MoveDown,
    Execute,
    Close,
    IgnoreScroll,
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
        .entries
        .iter()
        .enumerate()
        .map(|(i, entry)| match entry {
            ContextMenuEntry::Section(label) => {
                let heading =
                    text(*label)
                        .size(11)
                        .style(|theme: &Theme| iced::widget::text::Style {
                            color: Some(Color {
                                a: 0.75,
                                ..theme.extended_palette().secondary.strong.color
                            }),
                        });

                container(heading)
                    .padding(Padding {
                        top: 10.0,
                        right: 10.0,
                        bottom: 4.0,
                        left: 10.0,
                    })
                    .width(Length::Fill)
                    .into()
            }
            ContextMenuEntry::Action(entry) => {
                let is_keyboard_selected = i == state.selected;
                let accent = match entry.tone {
                    ActionTone::Default => None,
                    ActionTone::Windows => Some(Color::from_rgb(0.29, 0.56, 0.93)),
                };
                let action = entry.action.clone();
                let content = row![
                    text(entry.title.as_str())
                        .size(13)
                        .style(move |theme: &Theme| iced::widget::text::Style {
                            color: Some(accent.unwrap_or(theme.palette().text)),
                        })
                        .width(Length::Fill),
                    text(entry.shortcut.unwrap_or(""))
                        .size(11)
                        .style(|theme: &Theme| iced::widget::text::Style {
                            color: Some(Color {
                                a: 0.7,
                                ..theme.extended_palette().secondary.strong.color
                            }),
                        }),
                ]
                .spacing(10)
                .align_y(Alignment::Center);
                button(content)
                    .on_press(ContextMenuMessage::Select(action))
                    .style(move |theme: &Theme, status| {
                        let palette = theme.palette();
                        match status {
                            button::Status::Hovered | button::Status::Pressed => button::Style {
                                background: Some(Background::Color(palette.primary)),
                                text_color: Color::WHITE,
                                border: Border::default().rounded(6),
                                ..Default::default()
                            },
                            _ if is_keyboard_selected => button::Style {
                                background: Some(Background::Color(Color {
                                    a: 0.18,
                                    ..palette.primary
                                })),
                                text_color: palette.text,
                                border: Border::default().rounded(6),
                                ..Default::default()
                            },
                            _ => button::Style {
                                background: Some(Background::Color(Color::TRANSPARENT)),
                                text_color: palette.text,
                                border: Border::default().rounded(6),
                                ..Default::default()
                            },
                        }
                    })
                    .width(Length::Fill)
                    .padding([6, 10])
                    .into()
            }
        })
        .collect();

    let menu = container(scrollable(column(items).spacing(1)).height(Length::Shrink))
        .width(230)
        .max_height(360)
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

    mouse_area(
        container(positioned)
            .width(Length::Fill)
            .height(Length::Fill)
            .style(menu_backdrop_style),
    )
    .on_press(ContextMenuMessage::Close)
    .on_scroll(|_| ContextMenuMessage::IgnoreScroll)
    .interaction(mouse::Interaction::Pointer)
    .into()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn menu_with_selection_has_all_items() {
        let state =
            ContextMenuState::new(Point::new(100.0, 200.0), true, true, false, vec![], false);
        assert_eq!(state.entry_count(), 15);
        assert_eq!(state.section_count(), 4);
    }

    #[test]
    fn menu_with_selection_and_clipboard() {
        let state = ContextMenuState::new(Point::new(0.0, 0.0), true, true, true, vec![], false);
        assert_eq!(state.entry_count(), 16);
        assert_eq!(state.section_count(), 4);
    }

    #[test]
    fn menu_without_selection_paste_only() {
        let state = ContextMenuState::new(Point::new(0.0, 0.0), false, false, true, vec![], false);
        assert_eq!(state.entry_count(), 6);
        assert_eq!(state.section_count(), 2);
    }

    #[test]
    fn menu_has_new_folder_and_refresh_without_selection() {
        let state = ContextMenuState::new(Point::new(0.0, 0.0), false, false, false, vec![], false);
        assert_eq!(state.entry_count(), 4);
        assert_eq!(state.section_count(), 1);
    }

    #[test]
    fn menu_with_windows_fallback_adds_explicit_entry() {
        let state = ContextMenuState::new(Point::new(0.0, 0.0), true, true, false, vec![], true);
        assert_eq!(state.entry_count(), 17);
        assert_eq!(state.section_count(), 5);
    }

    #[test]
    fn menu_with_inline_shell_commands_adds_windows_section() {
        let commands = vec![ShellContextCommand {
            label: "Scan with Example AV".into(),
            canonical_verb: "example.scan".into(),
        }];
        let state = ContextMenuState::new(Point::new(0.0, 0.0), true, true, false, commands, true);
        assert_eq!(state.entry_count(), 19);
        assert_eq!(state.section_count(), 6);
    }

    #[test]
    fn menu_hides_open_with_for_non_file_selection() {
        let state = ContextMenuState::new(Point::new(0.0, 0.0), true, false, false, vec![], false);
        assert_eq!(state.entry_count(), 14);
        assert_eq!(state.section_count(), 4);
    }

    #[test]
    fn selected_action_skips_section_headers_by_default() {
        let state = ContextMenuState::new(Point::new(0.0, 0.0), true, true, false, vec![], false);
        assert_eq!(state.selected, 1);
        assert_eq!(state.selected_action(), Some(ContextAction::Open));
    }

    #[test]
    fn move_down_advances_to_next_action() {
        let mut state =
            ContextMenuState::new(Point::new(0.0, 0.0), true, true, false, vec![], false);
        state.move_down();
        assert_eq!(state.selected_action(), Some(ContextAction::OpenWith));
    }

    #[test]
    fn move_down_skips_section_headers() {
        let mut state =
            ContextMenuState::new(Point::new(0.0, 0.0), true, true, false, vec![], false);
        for _ in 0..4 {
            state.move_down();
        }
        assert_eq!(state.selected_action(), Some(ContextAction::Copy));
    }

    #[test]
    fn move_up_saturates_on_first_action() {
        let mut state =
            ContextMenuState::new(Point::new(0.0, 0.0), true, true, false, vec![], false);
        state.move_up();
        assert_eq!(state.selected_action(), Some(ContextAction::Open));
    }

    #[test]
    fn selected_action_returns_first_by_default() {
        let state = ContextMenuState::new(Point::new(0.0, 0.0), true, true, false, vec![], false);
        assert_eq!(state.selected_action(), Some(ContextAction::Open));
    }
}
