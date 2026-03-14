use std::collections::HashSet;
use std::path::{Path, PathBuf};

use filescout_core::preview::PreviewContent;
use filescout_core::{FileEntry, History, SortColumn, SortDirection, UndoStack};
use filescout_ui::address_bar::AddressBarMessage;
use filescout_ui::file_list::FileListMessage;
use filescout_ui::tab_bar::TabBarMessage;

/// Multi-selection state for a single tab.
///
/// - `items` — all selected paths (for batch operations).
/// - `focus` — the "primary" item: used for rename, preview, and Enter-to-navigate.
/// - `anchor` — the base of the last Shift+click range.
#[derive(Default)]
pub struct SelectionState {
    items: HashSet<PathBuf>,
    focus: Option<PathBuf>,
    anchor: Option<PathBuf>,
}

impl SelectionState {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn is_empty(&self) -> bool {
        self.items.is_empty()
    }

    pub fn len(&self) -> usize {
        self.items.len()
    }

    /// All selected paths (for batch operations).
    pub fn items(&self) -> &HashSet<PathBuf> {
        &self.items
    }

    /// The primary selected item (rename, preview, Enter-to-navigate).
    pub fn focus(&self) -> Option<&Path> {
        self.focus.as_deref()
    }

    /// Single click — replace the entire selection with just this item.
    pub fn single_select(&mut self, path: PathBuf) {
        self.items.clear();
        self.items.insert(path.clone());
        self.focus = Some(path.clone());
        self.anchor = Some(path);
    }

    /// Ctrl+click — toggle this item; update focus and anchor.
    pub fn ctrl_click(&mut self, path: PathBuf) {
        if self.items.contains(&path) {
            self.items.remove(&path);
            if self.focus.as_ref() == Some(&path) {
                self.focus = self.items.iter().next().cloned();
            }
        } else {
            self.items.insert(path.clone());
            self.focus = Some(path.clone());
        }
        self.anchor = Some(path);
    }

    /// Shift+click — range-select from anchor to `path` using the given ordered entry list.
    ///
    /// The anchor does NOT change on shift-click.
    pub fn shift_click(&mut self, path: PathBuf, ordered_entries: &[FileEntry]) {
        let anchor = self.anchor.clone().unwrap_or_else(|| path.clone());
        let anchor_idx = ordered_entries.iter().position(|e| e.path == anchor);
        let target_idx = ordered_entries.iter().position(|e| e.path == path);
        match (anchor_idx, target_idx) {
            (Some(a), Some(t)) => {
                let (lo, hi) = if a <= t { (a, t) } else { (t, a) };
                for entry in &ordered_entries[lo..=hi] {
                    self.items.insert(entry.path.clone());
                }
            }
            _ => {
                self.items.insert(path.clone());
            }
        }
        self.focus = Some(path);
    }

    /// Ctrl+A — select all entries. Does not change the anchor.
    pub fn select_all(&mut self, entries: &[FileEntry]) {
        self.items.clear();
        for e in entries {
            self.items.insert(e.path.clone());
        }
        self.focus = entries.last().map(|e| e.path.clone());
    }

    /// Shift+arrow — extend or shrink the selection to `new_focus_idx` in the entry list.
    ///
    /// Selects the range from anchor to the new focus index.
    pub fn extend_to(&mut self, entries: &[FileEntry], new_focus_idx: usize) {
        let anchor = self.anchor.clone();
        let anchor_idx = anchor
            .as_ref()
            .and_then(|a| entries.iter().position(|e| &e.path == a));

        if let Some(new_entry) = entries.get(new_focus_idx) {
            if let Some(a) = anchor_idx {
                let (lo, hi) = if a <= new_focus_idx { (a, new_focus_idx) } else { (new_focus_idx, a) };
                for entry in &entries[lo..=hi] {
                    self.items.insert(entry.path.clone());
                }
            } else {
                self.items.insert(new_entry.path.clone());
            }
            self.focus = Some(new_entry.path.clone());
        }
    }

    /// Clear the entire selection (items, focus, and anchor).
    pub fn clear(&mut self) {
        self.items.clear();
        self.focus = None;
        self.anchor = None;
    }
}

/// All state owned by a single tab within a pane.
pub struct TabState {
    pub current_path: PathBuf,
    pub entries: Vec<FileEntry>,
    pub error: Option<String>,
    pub sort_col: SortColumn,
    pub sort_dir: SortDirection,
    pub history: History,
    pub address_text: String,
    /// Multi-selection state (replaces single `selected: Option<PathBuf>`).
    pub selection: SelectionState,
    /// Inline rename state: `(original_path, current_text_being_edited)`.
    pub renaming: Option<(PathBuf, String)>,
    /// Undo stack for reversible file operations (rename only in this version).
    pub undo_stack: UndoStack,
    /// Preview content for the currently selected file.
    pub preview: PreviewContent,
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
            selection: SelectionState::new(),
            renaming: None,
            undo_stack: UndoStack::new(),
            preview: PreviewContent::None,
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

    /// Returns the opposite pane id.
    pub fn other(self) -> Self {
        match self {
            PaneId::Left => PaneId::Right,
            PaneId::Right => PaneId::Left,
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
    /// Toggle the preview panel on/off for this pane.
    TogglePreview,
    /// Async preview load result. Carries the tab index it was loaded for.
    PreviewLoaded(usize, PreviewContent),
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
    /// Select all entries in the active tab (Ctrl+A).
    SelectAll,
    /// Extend selection one row down (Shift+Down).
    ExtendSelectionDown,
    /// Extend selection one row up (Shift+Up).
    ExtendSelectionUp,
    /// Navigate to the parent of `path` and select `path` in the resulting listing.
    /// Used by the search overlay to jump to a result.
    NavigateToFile(PathBuf),
    // --- Async operation results ---
    /// Result of an async rename. Carries tab_idx, refresh_path, original_path, result(new_path).
    RenameDone(usize, PathBuf, PathBuf, Result<PathBuf, String>),
    /// Result of an async delete or undo. Carries tab_idx, refresh_path, result.
    OperationDone(usize, PathBuf, Result<(), String>),
    /// Result of a batch rename. Carries tab_idx, refresh_path, and one result per rename attempt.
    BatchRenameDone(usize, PathBuf, Vec<(PathBuf, Result<PathBuf, String>)>),
}

/// All state owned by a single file-list pane (one or more tabs).
pub struct PaneState {
    pub tabs: Vec<TabState>,
    pub active_tab: usize,
    /// Whether the preview panel is visible for this pane.
    pub show_preview: bool,
}

impl PaneState {
    pub fn new(path: PathBuf) -> Self {
        Self {
            tabs: vec![TabState::new(path)],
            active_tab: 0,
            show_preview: false,
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
        assert!(tab.selection.is_empty());
        assert!(tab.renaming.is_none());
        assert!(!tab.undo_stack.can_undo());
        assert!(!tab.history.can_go_back());
        assert!(!tab.history.can_go_forward());
        assert!(matches!(tab.preview, PreviewContent::None));
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

    #[test]
    fn pane_id_other_left_is_right() {
        assert_eq!(PaneId::Left.other(), PaneId::Right);
    }

    #[test]
    fn pane_id_other_right_is_left() {
        assert_eq!(PaneId::Right.other(), PaneId::Left);
    }

    // --- SelectionState helpers ---

    fn entry(name: &str) -> FileEntry {
        FileEntry {
            name: name.to_string(),
            path: PathBuf::from(name),
            is_dir: false,
            size: 0,
            modified: None,
        }
    }

    fn entries(names: &[&str]) -> Vec<FileEntry> {
        names.iter().map(|n| entry(n)).collect()
    }

    // --- SelectionState ---

    #[test]
    fn selection_new_is_empty() {
        let s = SelectionState::new();
        assert!(s.is_empty());
        assert_eq!(s.len(), 0);
        assert!(s.focus().is_none());
    }

    #[test]
    fn single_select_sets_item_and_focus() {
        let mut s = SelectionState::new();
        s.single_select(p("a.txt"));
        assert!(!s.is_empty());
        assert_eq!(s.len(), 1);
        assert!(s.items().contains(Path::new("a.txt")));
        assert_eq!(s.focus(), Some(Path::new("a.txt")));
    }

    #[test]
    fn single_select_replaces_previous_selection() {
        let mut s = SelectionState::new();
        s.single_select(p("a.txt"));
        s.single_select(p("b.txt"));
        assert_eq!(s.len(), 1);
        assert!(!s.items().contains(Path::new("a.txt")));
        assert!(s.items().contains(Path::new("b.txt")));
    }

    #[test]
    fn ctrl_click_adds_second_item() {
        let mut s = SelectionState::new();
        s.single_select(p("a.txt"));
        s.ctrl_click(p("b.txt"));
        assert_eq!(s.len(), 2);
        assert!(s.items().contains(Path::new("a.txt")));
        assert!(s.items().contains(Path::new("b.txt")));
        assert_eq!(s.focus(), Some(Path::new("b.txt")));
    }

    #[test]
    fn ctrl_click_toggles_item_off() {
        let mut s = SelectionState::new();
        s.single_select(p("a.txt"));
        s.ctrl_click(p("a.txt"));
        assert!(s.is_empty());
    }

    #[test]
    fn ctrl_click_focus_falls_back_when_toggled_off() {
        let mut s = SelectionState::new();
        s.single_select(p("a.txt"));
        s.ctrl_click(p("b.txt"));
        s.ctrl_click(p("b.txt")); // toggle off focus
        // focus should fall back to some remaining item
        assert_eq!(s.len(), 1);
        assert!(s.items().contains(Path::new("a.txt")));
    }

    #[test]
    fn shift_click_selects_forward_range() {
        let list = entries(&["a", "b", "c", "d"]);
        let mut s = SelectionState::new();
        s.single_select(p("a"));
        s.shift_click(p("c"), &list);
        assert_eq!(s.len(), 3);
        assert!(s.items().contains(Path::new("a")));
        assert!(s.items().contains(Path::new("b")));
        assert!(s.items().contains(Path::new("c")));
        assert!(!s.items().contains(Path::new("d")));
    }

    #[test]
    fn shift_click_selects_backward_range() {
        let list = entries(&["a", "b", "c", "d"]);
        let mut s = SelectionState::new();
        s.single_select(p("c"));
        s.shift_click(p("a"), &list);
        assert_eq!(s.len(), 3);
        assert!(s.items().contains(Path::new("a")));
        assert!(s.items().contains(Path::new("b")));
        assert!(s.items().contains(Path::new("c")));
    }

    #[test]
    fn shift_click_does_not_move_anchor() {
        let list = entries(&["a", "b", "c"]);
        let mut s = SelectionState::new();
        s.single_select(p("a")); // anchor = a
        s.shift_click(p("c"), &list); // anchor stays a
        s.shift_click(p("b"), &list); // should select a..b
        assert!(s.items().contains(Path::new("a")));
        assert!(s.items().contains(Path::new("b")));
        assert!(s.items().contains(Path::new("c"))); // c was added in first shift_click, stays
    }

    #[test]
    fn select_all_selects_every_entry() {
        let list = entries(&["a", "b", "c"]);
        let mut s = SelectionState::new();
        s.select_all(&list);
        assert_eq!(s.len(), 3);
        assert!(s.items().contains(Path::new("a")));
        assert!(s.items().contains(Path::new("b")));
        assert!(s.items().contains(Path::new("c")));
    }

    #[test]
    fn select_all_empty_entries_clears_selection() {
        let mut s = SelectionState::new();
        s.single_select(p("a.txt"));
        s.select_all(&[]);
        assert!(s.is_empty());
        assert!(s.focus().is_none());
    }

    #[test]
    fn extend_to_selects_range_from_anchor() {
        let list = entries(&["a", "b", "c", "d"]);
        let mut s = SelectionState::new();
        s.single_select(p("b")); // anchor = b, idx=1
        s.extend_to(&list, 3); // target idx=3 (d)
        assert!(s.items().contains(Path::new("b")));
        assert!(s.items().contains(Path::new("c")));
        assert!(s.items().contains(Path::new("d")));
        assert_eq!(s.focus(), Some(Path::new("d")));
    }

    #[test]
    fn clear_empties_selection() {
        let mut s = SelectionState::new();
        s.single_select(p("a.txt"));
        s.clear();
        assert!(s.is_empty());
        assert!(s.focus().is_none());
    }
}
