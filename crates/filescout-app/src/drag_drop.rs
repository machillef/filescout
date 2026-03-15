//! Internal drag-and-drop state for moving/copying files between panes.
//!
//! Foundation module — not yet wired into the UI. iced 0.14 DnD is experimental,
//! so this is kept as a minimal data model for future integration.

#![allow(dead_code)]

use std::path::PathBuf;

use super::pane::PaneId;

/// Tracks an in-progress drag operation.
#[derive(Debug, Clone)]
pub struct DragState {
    /// The pane the drag started from.
    pub source_pane: PaneId,
    /// The paths being dragged.
    pub paths: Vec<PathBuf>,
}

impl DragState {
    pub fn new(source_pane: PaneId, paths: Vec<PathBuf>) -> Self {
        Self { source_pane, paths }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn drag_state_construction() {
        let state = DragState::new(PaneId::Left, vec![PathBuf::from("a.txt")]);
        assert_eq!(state.source_pane, PaneId::Left);
        assert_eq!(state.paths.len(), 1);
    }

    #[test]
    fn drop_on_same_pane_paths_preserved() {
        let state = DragState::new(
            PaneId::Left,
            vec![PathBuf::from("a.txt"), PathBuf::from("b.txt")],
        );
        assert_eq!(state.paths.len(), 2);
        assert_eq!(state.source_pane, PaneId::Left);
    }
}
