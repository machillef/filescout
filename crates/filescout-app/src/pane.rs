use std::path::PathBuf;

use filescout_core::{FileEntry, History, SortColumn, SortDirection};
use filescout_ui::address_bar::AddressBarMessage;
use filescout_ui::file_list::FileListMessage;

/// Identifies which of the two panes a message belongs to.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum PaneId {
    #[default]
    Left,
    Right,
}

impl PaneId {
    pub fn index(self) -> usize {
        match self {
            PaneId::Left => 0,
            PaneId::Right => 1,
        }
    }
}

/// Messages that belong to a specific pane.
#[derive(Debug, Clone)]
pub enum PaneMessage {
    FileList(FileListMessage),
    AddressBar(AddressBarMessage),
    DirectoryLoaded(PathBuf, Result<Vec<FileEntry>, String>),
}

/// All state owned by a single file-list pane.
pub struct PaneState {
    pub current_path: PathBuf,
    pub entries: Vec<FileEntry>,
    pub error: Option<String>,
    pub sort_col: SortColumn,
    pub sort_dir: SortDirection,
    pub history: History,
    pub address_text: String,
}

impl PaneState {
    pub fn new(path: PathBuf) -> Self {
        let address_text = path.display().to_string();
        Self {
            history: History::new(path.clone()),
            address_text,
            current_path: path,
            entries: Vec::new(),
            error: None,
            sort_col: SortColumn::default(),
            sort_dir: SortDirection::default(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pane_state_initializes_correctly() {
        let path = PathBuf::from("C:\\Users");
        let state = PaneState::new(path.clone());
        assert_eq!(state.current_path, path);
        assert_eq!(state.address_text, "C:\\Users");
        assert!(state.entries.is_empty());
        assert!(state.error.is_none());
        assert!(!state.history.can_go_back());
        assert!(!state.history.can_go_forward());
    }

    #[test]
    fn pane_id_index_left_is_zero() {
        assert_eq!(PaneId::Left.index(), 0);
    }

    #[test]
    fn pane_id_index_right_is_one() {
        assert_eq!(PaneId::Right.index(), 1);
    }

    #[test]
    fn pane_id_default_is_left() {
        assert_eq!(PaneId::default(), PaneId::Left);
    }
}
