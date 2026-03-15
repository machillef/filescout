//! Command palette — fuzzy-searchable list of app actions (Ctrl+Shift+P).

use fuzzy_matcher::{skim::SkimMatcherV2, FuzzyMatcher};
use iced::widget::{button, column, container, scrollable, text, text_input, Id};
use iced::{Background, Color, Element, Length, Theme};

/// Widget ID for the palette scrollable (used for programmatic scroll).
pub const PALETTE_LIST_ID: &str = "palette_list";

/// All commands the user can invoke from the palette.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CommandId {
    NavigateBack,
    NavigateForward,
    NewTab,
    CloseTab,
    Rename,
    Delete,
    Undo,
    SearchFiles,
    TogglePreview,
    BatchRename,
    ToggleTheme,
    CopyToOther,
    MoveToOther,
    OpenBookmarks,
    AddBookmark,
    FileProperties,
    ExtractArchive,
    OpenFile,
    ToggleHidden,
    ClipboardCopy,
    ClipboardCut,
    ClipboardPaste,
    ViewList,
    ViewDetails,
    ToggleDualPane,
    NewFolder,
    NavigateThisPC,
    NavigateHome,
    NavigateDownloads,
    NavigateDocuments,
    NavigateDesktop,
    RefreshDir,
    KeyboardShortcuts,
}

pub struct CommandDef {
    pub id: CommandId,
    pub label: &'static str,
    pub description: &'static str,
}

pub const ALL_COMMANDS: &[CommandDef] = &[
    CommandDef {
        id: CommandId::NavigateBack,
        label: "Navigate Back",
        description: "Go to the previous directory in history",
    },
    CommandDef {
        id: CommandId::NavigateForward,
        label: "Navigate Forward",
        description: "Go to the next directory in history",
    },
    CommandDef {
        id: CommandId::NewTab,
        label: "New Tab",
        description: "Open a new tab in the active pane (Ctrl+T)",
    },
    CommandDef {
        id: CommandId::CloseTab,
        label: "Close Tab",
        description: "Close the active tab",
    },
    CommandDef {
        id: CommandId::Rename,
        label: "Rename",
        description: "Rename the selected file or directory (F2)",
    },
    CommandDef {
        id: CommandId::Delete,
        label: "Delete to Trash",
        description: "Move the selected item to the Recycle Bin (F8)",
    },
    CommandDef {
        id: CommandId::Undo,
        label: "Undo",
        description: "Undo the last file operation (Ctrl+Z)",
    },
    CommandDef {
        id: CommandId::SearchFiles,
        label: "Search Files",
        description: "Fuzzy-search files recursively from the current directory (Ctrl+F)",
    },
    CommandDef {
        id: CommandId::TogglePreview,
        label: "Toggle Preview",
        description: "Show or hide the file preview panel (F3)",
    },
    CommandDef {
        id: CommandId::BatchRename,
        label: "Batch Rename",
        description: "Rename multiple files using a pattern (Ctrl+R)",
    },
    CommandDef {
        id: CommandId::ToggleTheme,
        label: "Toggle Theme",
        description: "Switch between dark and light theme (Ctrl+Shift+T)",
    },
    CommandDef {
        id: CommandId::CopyToOther,
        label: "Copy to Other Pane",
        description: "Copy the selected item to the other pane's directory (F5)",
    },
    CommandDef {
        id: CommandId::MoveToOther,
        label: "Move to Other Pane",
        description: "Move the selected item to the other pane's directory (F6)",
    },
    CommandDef {
        id: CommandId::OpenBookmarks,
        label: "Open Bookmarks",
        description: "Show bookmarked directories (Ctrl+B)",
    },
    CommandDef {
        id: CommandId::AddBookmark,
        label: "Bookmark This Directory",
        description: "Save the current directory as a bookmark (Ctrl+D)",
    },
    CommandDef {
        id: CommandId::FileProperties,
        label: "File Properties",
        description: "Show properties for the selected file or directory (Alt+Enter)",
    },
    CommandDef {
        id: CommandId::ExtractArchive,
        label: "Extract Archive",
        description: "Extract the current ZIP archive contents to the other pane (F7)",
    },
    CommandDef {
        id: CommandId::OpenFile,
        label: "Open File",
        description: "Open the selected file with the default application (Enter)",
    },
    CommandDef {
        id: CommandId::ToggleHidden,
        label: "Toggle Hidden Files",
        description: "Show or hide hidden and system files (Ctrl+H)",
    },
    CommandDef {
        id: CommandId::ClipboardCopy,
        label: "Copy",
        description: "Copy selected files to clipboard (Ctrl+C)",
    },
    CommandDef {
        id: CommandId::ClipboardCut,
        label: "Cut",
        description: "Cut selected files to clipboard (Ctrl+X)",
    },
    CommandDef {
        id: CommandId::ClipboardPaste,
        label: "Paste",
        description: "Paste clipboard files into current directory (Ctrl+V)",
    },
    CommandDef {
        id: CommandId::ViewList,
        label: "List View",
        description: "Show compact file list (Ctrl+1)",
    },
    CommandDef {
        id: CommandId::ViewDetails,
        label: "Details View",
        description: "Show detailed file list with extension and type columns (Ctrl+2)",
    },
    CommandDef {
        id: CommandId::ToggleDualPane,
        label: "Toggle Dual Pane",
        description: "Switch between single-pane and dual-pane layout (F9)",
    },
    CommandDef {
        id: CommandId::NewFolder,
        label: "New Folder",
        description: "Create a new folder in the current directory",
    },
    CommandDef {
        id: CommandId::NavigateThisPC,
        label: "This PC",
        description: "Navigate to drive root (C:\\)",
    },
    CommandDef {
        id: CommandId::NavigateHome,
        label: "Go to Home",
        description: "Navigate to your home directory",
    },
    CommandDef {
        id: CommandId::NavigateDownloads,
        label: "Go to Downloads",
        description: "Navigate to the Downloads folder",
    },
    CommandDef {
        id: CommandId::NavigateDocuments,
        label: "Go to Documents",
        description: "Navigate to the Documents folder",
    },
    CommandDef {
        id: CommandId::NavigateDesktop,
        label: "Go to Desktop",
        description: "Navigate to the Desktop folder",
    },
    CommandDef {
        id: CommandId::RefreshDir,
        label: "Refresh",
        description: "Reload the current directory listing",
    },
    CommandDef {
        id: CommandId::KeyboardShortcuts,
        label: "Keyboard Shortcuts",
        description: "Show all keyboard shortcuts (F1)",
    },
];

/// Messages emitted by the command palette UI.
#[derive(Debug, Clone)]
pub enum PaletteMessage {
    QueryChanged(String),
    MoveUp,
    MoveDown,
    /// Execute the currently keyboard-selected command.
    Execute,
    /// Execute a specific command (mouse click on a row).
    ExecuteCommand(CommandId),
    Close,
}

/// State for the command palette overlay.
pub struct PaletteState {
    pub query: String,
    pub selected: usize,
}

impl PaletteState {
    pub fn new() -> Self {
        Self {
            query: String::new(),
            selected: 0,
        }
    }

    /// Returns commands filtered by the current query, sorted by match score.
    /// Returns all commands when the query is empty.
    pub fn filtered_commands(&self) -> Vec<&'static CommandDef> {
        if self.query.is_empty() {
            return ALL_COMMANDS.iter().collect();
        }

        let matcher = SkimMatcherV2::default();
        let mut scored: Vec<(i64, &'static CommandDef)> = ALL_COMMANDS
            .iter()
            .filter_map(|cmd| {
                matcher
                    .fuzzy_match(cmd.label, &self.query)
                    .map(|score| (score, cmd))
            })
            .collect();
        scored.sort_by(|a, b| b.0.cmp(&a.0));
        scored.into_iter().map(|(_, cmd)| cmd).collect()
    }

    /// Move keyboard selection up by one (floor at 0).
    pub fn move_up(&mut self) {
        self.selected = self.selected.saturating_sub(1);
    }

    /// Move keyboard selection down by one (ceiling at `max - 1`).
    pub fn move_down(&mut self, max: usize) {
        if max > 0 {
            self.selected = (self.selected + 1).min(max - 1);
        }
    }

    /// Returns the `CommandId` of the currently selected row, if one exists.
    pub fn selected_command(&self) -> Option<CommandId> {
        self.filtered_commands()
            .get(self.selected)
            .map(|cmd| cmd.id)
    }
}

fn backdrop_style(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(Background::Color(Color::from_rgba(0.0, 0.0, 0.0, 0.7))),
        ..Default::default()
    }
}

/// Renders the command palette as a full-screen overlay with a dimmed backdrop.
pub fn command_palette_view(state: &PaletteState) -> Element<'_, PaletteMessage> {
    let filtered = state.filtered_commands();

    let items: Vec<Element<'_, PaletteMessage>> = filtered
        .iter()
        .enumerate()
        .map(|(i, cmd)| {
            let style = if i == state.selected {
                button::primary
            } else {
                button::text
            };
            let id = cmd.id;
            button(
                column![text(cmd.label).size(14), text(cmd.description).size(11)]
                    .spacing(2)
                    .padding([0, 4]),
            )
            .on_press(PaletteMessage::ExecuteCommand(id))
            .style(style)
            .width(Length::Fill)
            .padding([6, 12])
            .into()
        })
        .collect();

    let list = scrollable(column(items).spacing(2).width(Length::Fill))
        .id(Id::new(PALETTE_LIST_ID))
        .height(320);

    let input = text_input("Search commands…", &state.query)
        .on_input(PaletteMessage::QueryChanged)
        .padding(10)
        .size(15);

    let palette_box = container(column![input, list].spacing(8))
        .width(560)
        .padding(12)
        .style(container::rounded_box);

    container(palette_box)
        .center(Length::Fill)
        .style(backdrop_style)
        .into()
}

#[cfg(test)]
mod tests {
    use super::*;

    // --- filtered_commands ---

    #[test]
    fn empty_query_returns_all_commands() {
        let state = PaletteState::new();
        let filtered = state.filtered_commands();
        assert_eq!(filtered.len(), ALL_COMMANDS.len());
    }

    #[test]
    fn query_filters_by_fuzzy_match() {
        let mut state = PaletteState::new();
        state.query = "ren".to_string();
        let filtered = state.filtered_commands();
        assert!(filtered.iter().any(|c| c.id == CommandId::Rename));
    }

    #[test]
    fn no_match_returns_empty_list() {
        let mut state = PaletteState::new();
        state.query = "xyzabc123".to_string();
        assert!(state.filtered_commands().is_empty());
    }

    #[test]
    fn query_back_matches_navigate_back() {
        let mut state = PaletteState::new();
        state.query = "back".to_string();
        let filtered = state.filtered_commands();
        assert!(filtered.iter().any(|c| c.id == CommandId::NavigateBack));
    }

    #[test]
    fn query_new_tab_matches_new_tab() {
        let mut state = PaletteState::new();
        state.query = "new tab".to_string();
        let filtered = state.filtered_commands();
        assert!(filtered.iter().any(|c| c.id == CommandId::NewTab));
    }

    // --- move_up / move_down ---

    #[test]
    fn move_up_saturates_at_zero() {
        let mut state = PaletteState::new();
        state.selected = 0;
        state.move_up();
        assert_eq!(state.selected, 0);
    }

    #[test]
    fn move_down_advances_selection() {
        let mut state = PaletteState::new();
        state.selected = 0;
        state.move_down(3);
        assert_eq!(state.selected, 1);
    }

    #[test]
    fn move_down_clamps_at_last_index() {
        let mut state = PaletteState::new();
        state.selected = 2;
        state.move_down(3); // max = 3, last valid index = 2
        assert_eq!(state.selected, 2);
    }

    #[test]
    fn move_down_with_max_zero_is_noop() {
        let mut state = PaletteState::new();
        state.selected = 0;
        state.move_down(0);
        assert_eq!(state.selected, 0);
    }

    #[test]
    fn move_up_then_down_returns_to_original() {
        let mut state = PaletteState::new();
        state.selected = 2;
        state.move_up();
        state.move_down(5);
        assert_eq!(state.selected, 2);
    }

    // --- selected_command ---

    #[test]
    fn selected_command_returns_first_command_by_default() {
        let state = PaletteState::new();
        let cmd = state.selected_command();
        assert_eq!(cmd, Some(ALL_COMMANDS[0].id));
    }

    #[test]
    fn selected_command_returns_none_when_no_matches() {
        let mut state = PaletteState::new();
        state.query = "xyzabc123".to_string();
        assert!(state.selected_command().is_none());
    }
}
