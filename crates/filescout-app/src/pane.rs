use std::path::PathBuf;

use filescout_core::{FileEntry, History, SortColumn, SortDirection, UndoStack};
use filescout_ui::address_bar::AddressBarMessage;
use filescout_ui::file_list::FileListMessage;
use filescout_ui::tab_bar::TabBarMessage;

/// All state owned by a single tab within a pane.
pub struct TabState {
    pub current_path: PathBuf,
    pub entries: Vec<FileEntry>,
    pub error: Option<String>,
    pub sort_col: SortColumn,
    pub sort_dir: SortDirection,
    pub history: History,
    pub address_text: String,
    /// Currently selected file or directory (single-selection).
    pub selected: Option<PathBuf>,
    /// Inline rename state: `(original_path, current_text_being_edited)`.
    pub renaming: Option<(PathBuf, String)>,
    /// Undo stack for reversible file operations (rename only in this version).
    pub undo_stack: UndoStack,
}

impl TabState {
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
            selected: None,
            renaming: None,
            undo_stack: UndoStack::new(),
        }
    }

    /// Short display title for the tab (last path component, or full path at root).
    pub fn title(&self) -> String {
        self.current_path
            .file_name()
            .map(|n| n.to_string_lossy().into_owned())
            .unwrap_or_else(|| self.current_path.display().to_string())
    }

}

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
    TabBar(TabBarMessage),
    /// Async directory load result. Carries the tab index it was requested for.
    DirectoryLoaded(usize, PathBuf, Result<Vec<FileEntry>, String>),
    // --- File operation messages (emitted from keyboard subscription) ---
    /// Begin renaming the currently selected item.
    StartRename,
    /// Commit the in-progress rename.
    CommitRename,
    /// Cancel the in-progress rename without changing anything.
    CancelRename,
    /// Delete the currently selected item to trash.
    DeleteSelected,
    /// Undo the last reversible operation.
    Undo,
    /// Navigate into the currently selected directory (Enter key).
    NavigateSelected,
    /// Navigate to the parent of `path` and select `path` in the resulting listing.
    /// Used by the search overlay to jump to a result.
    NavigateToFile(PathBuf),
    // --- Async operation results ---
    /// Result of an async rename. Carries tab_idx, refresh_path, original_path, result(new_path).
    RenameDone(usize, PathBuf, PathBuf, Result<PathBuf, String>),
    /// Result of an async delete or undo. Carries tab_idx, refresh_path, result.
    OperationDone(usize, PathBuf, Result<(), String>),
}

/// All state owned by a single file-list pane (one or more tabs).
pub struct PaneState {
    pub tabs: Vec<TabState>,
    pub active_tab: usize,
}

impl PaneState {
    pub fn new(path: PathBuf) -> Self {
        Self {
            tabs: vec![TabState::new(path)],
            active_tab: 0,
        }
    }

    pub fn active(&self) -> &TabState {
        &self.tabs[self.active_tab]
    }

    pub fn active_mut(&mut self) -> &mut TabState {
        &mut self.tabs[self.active_tab]
    }

    /// Open a new tab at `path`, activate it, and return the new tab index.
    pub fn new_tab(&mut self, path: PathBuf) -> usize {
        self.tabs.push(TabState::new(path));
        self.active_tab = self.tabs.len() - 1;
        self.active_tab
    }

    /// Close the tab at `idx`. No-op if it is the only remaining tab.
    /// Adjusts `active_tab` to remain valid after removal.
    pub fn close_tab(&mut self, idx: usize) {
        if self.tabs.len() <= 1 || idx >= self.tabs.len() {
            return;
        }
        self.tabs.remove(idx);
        if self.active_tab > idx {
            self.active_tab -= 1;
        } else if self.active_tab >= self.tabs.len() {
            self.active_tab = self.tabs.len() - 1;
        }
    }

    /// Switch the active tab to `idx`. No-op if `idx` is out of bounds.
    pub fn switch_tab(&mut self, idx: usize) {
        if idx < self.tabs.len() {
            self.active_tab = idx;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn p(s: &str) -> PathBuf {
        PathBuf::from(s)
    }

    // --- TabState ---

    #[test]
    fn tab_state_initializes_correctly() {
        let tab = TabState::new(p("C:\\Users"));
        assert_eq!(tab.current_path, p("C:\\Users"));
        assert_eq!(tab.address_text, "C:\\Users");
        assert!(tab.entries.is_empty());
        assert!(tab.error.is_none());
        assert!(tab.selected.is_none());
        assert!(tab.renaming.is_none());
        assert!(!tab.undo_stack.can_undo());
        assert!(!tab.history.can_go_back());
        assert!(!tab.history.can_go_forward());
    }

    #[test]
    fn tab_state_title_returns_last_component() {
        let tab = TabState::new(p("C:\\Users\\machi"));
        assert_eq!(tab.title(), "machi");
    }

    #[test]
    fn tab_state_title_at_drive_root_returns_full_path() {
        let tab = TabState::new(p("C:\\"));
        assert_eq!(tab.title(), "C:\\");
    }

    // --- PaneState ---

    #[test]
    fn pane_state_new_has_one_tab() {
        let pane = PaneState::new(p("C:\\"));
        assert_eq!(pane.tabs.len(), 1);
        assert_eq!(pane.active_tab, 0);
    }

    #[test]
    fn pane_state_active_returns_first_tab_path() {
        let path = p("C:\\Users");
        let pane = PaneState::new(path.clone());
        assert_eq!(pane.active().current_path, path);
    }

    #[test]
    fn pane_state_new_tab_appends_and_activates() {
        let mut pane = PaneState::new(p("C:\\"));
        let idx = pane.new_tab(p("C:\\Users"));
        assert_eq!(idx, 1);
        assert_eq!(pane.tabs.len(), 2);
        assert_eq!(pane.active_tab, 1);
        assert_eq!(pane.active().current_path, p("C:\\Users"));
    }

    #[test]
    fn pane_state_switch_tab_changes_active() {
        let mut pane = PaneState::new(p("C:\\"));
        pane.new_tab(p("C:\\Users"));
        pane.switch_tab(0);
        assert_eq!(pane.active_tab, 0);
        assert_eq!(pane.active().current_path, p("C:\\"));
    }

    #[test]
    fn pane_state_switch_tab_out_of_bounds_is_noop() {
        let mut pane = PaneState::new(p("C:\\"));
        pane.switch_tab(5);
        assert_eq!(pane.active_tab, 0);
    }

    #[test]
    fn pane_state_close_tab_removes_tab() {
        let mut pane = PaneState::new(p("C:\\"));
        pane.new_tab(p("C:\\Users"));
        pane.close_tab(0);
        assert_eq!(pane.tabs.len(), 1);
        assert_eq!(pane.active().current_path, p("C:\\Users"));
    }

    #[test]
    fn pane_state_close_last_tab_is_noop() {
        let mut pane = PaneState::new(p("C:\\"));
        pane.close_tab(0);
        assert_eq!(pane.tabs.len(), 1);
        assert_eq!(pane.active_tab, 0);
    }

    #[test]
    fn pane_state_close_active_tab_adjusts_to_previous() {
        let mut pane = PaneState::new(p("C:\\"));
        pane.new_tab(p("C:\\Users"));
        pane.close_tab(1);
        assert_eq!(pane.tabs.len(), 1);
        assert_eq!(pane.active_tab, 0);
        assert_eq!(pane.active().current_path, p("C:\\"));
    }

    #[test]
    fn pane_state_close_earlier_tab_adjusts_active_index() {
        let mut pane = PaneState::new(p("C:\\"));
        pane.new_tab(p("C:\\Users"));
        pane.new_tab(p("C:\\Windows"));
        pane.close_tab(0);
        assert_eq!(pane.tabs.len(), 2);
        assert_eq!(pane.active_tab, 1);
        assert_eq!(pane.active().current_path, p("C:\\Windows"));
    }

    #[test]
    fn pane_state_close_out_of_bounds_is_noop() {
        let mut pane = PaneState::new(p("C:\\"));
        pane.new_tab(p("C:\\Users"));
        pane.close_tab(99);
        assert_eq!(pane.tabs.len(), 2);
    }

    // --- PaneId ---

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
