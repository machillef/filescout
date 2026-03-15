use iced::widget::{column, container, row, scrollable, text};
use iced::{Background, Border, Color, Element, Length, Theme};

#[derive(Debug, Clone)]
pub enum ShortcutsHelpMessage {
    Close,
}

struct Shortcut {
    key: &'static str,
    description: &'static str,
}

struct Category {
    name: &'static str,
    shortcuts: &'static [Shortcut],
}

const CATEGORIES: &[Category] = &[
    Category {
        name: "Navigation",
        shortcuts: &[
            Shortcut { key: "Enter", description: "Open directory / commit rename" },
            Shortcut { key: "Backspace / ..", description: "Go to parent directory" },
            Shortcut { key: "← →", description: "Back / Forward in history" },
            Shortcut { key: "↑ ↓", description: "Move focus up / down" },
            Shortcut { key: "Tab", description: "Auto-complete path in address bar" },
            Shortcut { key: "Letter key", description: "Jump to first file starting with letter" },
        ],
    },
    Category {
        name: "Selection",
        shortcuts: &[
            Shortcut { key: "Click", description: "Select single item" },
            Shortcut { key: "Ctrl+Click", description: "Toggle item in selection" },
            Shortcut { key: "Shift+Click", description: "Range select" },
            Shortcut { key: "Ctrl+A", description: "Select all" },
            Shortcut { key: "Shift+↑ / Shift+↓", description: "Extend selection" },
        ],
    },
    Category {
        name: "File Operations",
        shortcuts: &[
            Shortcut { key: "F2", description: "Rename selected file" },
            Shortcut { key: "F5", description: "Copy to other pane" },
            Shortcut { key: "F6", description: "Move to other pane" },
            Shortcut { key: "F7", description: "Extract archive to other pane" },
            Shortcut { key: "F8 / Delete", description: "Delete to Recycle Bin" },
            Shortcut { key: "Ctrl+C", description: "Copy to clipboard" },
            Shortcut { key: "Ctrl+X", description: "Cut to clipboard" },
            Shortcut { key: "Ctrl+V", description: "Paste from clipboard" },
            Shortcut { key: "Ctrl+Z", description: "Undo last rename" },
        ],
    },
    Category {
        name: "Overlays & Views",
        shortcuts: &[
            Shortcut { key: "F1", description: "Show this help" },
            Shortcut { key: "F3", description: "Toggle file preview panel" },
            Shortcut { key: "Ctrl+Shift+P", description: "Command palette" },
            Shortcut { key: "Ctrl+F", description: "Search files" },
            Shortcut { key: "Ctrl+R", description: "Batch rename" },
            Shortcut { key: "Alt+Enter", description: "File properties" },
        ],
    },
    Category {
        name: "Tabs & Panes",
        shortcuts: &[
            Shortcut { key: "Ctrl+T", description: "New tab" },
            Shortcut { key: "F9", description: "Toggle dual-pane mode" },
        ],
    },
    Category {
        name: "Bookmarks & Display",
        shortcuts: &[
            Shortcut { key: "Ctrl+B", description: "Open bookmarks" },
            Shortcut { key: "Ctrl+D", description: "Bookmark current directory" },
            Shortcut { key: "Ctrl+H", description: "Toggle hidden files" },
            Shortcut { key: "Ctrl+1", description: "List view" },
            Shortcut { key: "Ctrl+2", description: "Details view" },
            Shortcut { key: "Ctrl+Shift+T", description: "Toggle dark/light theme" },
        ],
    },
    Category {
        name: "General",
        shortcuts: &[
            Shortcut { key: "Escape", description: "Close overlay / cancel rename" },
        ],
    },
];

fn shortcut_row<'a>(shortcut: &Shortcut) -> Element<'a, ShortcutsHelpMessage> {
    let key_badge = container(
        text(shortcut.key).size(13),
    )
    .padding([2, 8])
    .style(|theme: &Theme| {
        let palette = theme.palette();
        container::Style {
            background: Some(Background::Color(Color {
                a: 0.15,
                ..palette.primary
            })),
            border: Border {
                color: Color { a: 0.3, ..palette.primary },
                width: 1.0,
                radius: 4.0.into(),
            },
            ..Default::default()
        }
    });

    row![
        container(key_badge).width(200),
        text(shortcut.description).size(14),
    ]
    .spacing(12)
    .into()
}

fn category_section<'a>(category: &Category) -> Element<'a, ShortcutsHelpMessage> {
    let mut col = column![
        text(category.name).size(16).style(|theme: &Theme| {
            text::Style { color: Some(theme.palette().primary) }
        }),
    ]
    .spacing(4);

    for s in category.shortcuts {
        col = col.push(shortcut_row(s));
    }

    col.push(text("").size(8)).into()
}

pub fn shortcuts_help_view<'a>() -> Element<'a, ShortcutsHelpMessage> {
    let mut content = column![
        text("Keyboard Shortcuts").size(22),
        text("Press F1 or Escape to close").size(12).style(|theme: &Theme| {
            text::Style {
                color: Some(Color {
                    a: 0.6,
                    ..theme.palette().text
                }),
            }
        }),
        text("").size(8),
    ]
    .spacing(4)
    .width(500);

    for cat in CATEGORIES {
        content = content.push(category_section(cat));
    }

    let card = container(scrollable(content).height(Length::Fill))
        .padding(24)
        .max_height(600)
        .style(|theme: &Theme| {
            let palette = theme.palette();
            container::Style {
                background: Some(Background::Color(palette.background)),
                border: Border {
                    color: palette.primary,
                    width: 1.0,
                    radius: 8.0.into(),
                },
                ..Default::default()
            }
        });

    let centered = container(card)
        .width(Length::Fill)
        .height(Length::Fill)
        .center_x(Length::Fill)
        .center_y(Length::Fill)
        .style(|_theme: &Theme| container::Style {
            background: Some(Background::Color(Color::from_rgba(0.0, 0.0, 0.0, 0.7))),
            ..Default::default()
        });

    centered.into()
}
