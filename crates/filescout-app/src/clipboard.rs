//! Internal clipboard state for copy/cut/paste file operations.

use std::path::PathBuf;

/// Whether the clipboard holds a copy or cut operation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ClipOp {
    Copy,
    Cut,
}

/// Internal clipboard holding paths and the intended operation.
#[derive(Debug, Clone)]
pub struct ClipboardState {
    pub paths: Vec<PathBuf>,
    pub operation: ClipOp,
}

impl ClipboardState {
    pub fn new(paths: Vec<PathBuf>, operation: ClipOp) -> Self {
        Self { paths, operation }
    }
}

/// Write the given paths to the system clipboard as newline-separated text.
/// Silently ignores errors (clipboard is best-effort).
pub fn write_paths_to_system_clipboard(paths: &[PathBuf]) {
    let text: String = paths
        .iter()
        .map(|p| p.display().to_string())
        .collect::<Vec<_>>()
        .join("\n");
    if let Ok(mut clip) = arboard::Clipboard::new() {
        let _ = clip.set_text(text);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn clipboard_state_copy() {
        let paths = vec![PathBuf::from("a.txt"), PathBuf::from("b.txt")];
        let state = ClipboardState::new(paths.clone(), ClipOp::Copy);
        assert_eq!(state.paths, paths);
        assert_eq!(state.operation, ClipOp::Copy);
    }

    #[test]
    fn clipboard_state_cut() {
        let paths = vec![PathBuf::from("a.txt")];
        let state = ClipboardState::new(paths.clone(), ClipOp::Cut);
        assert_eq!(state.operation, ClipOp::Cut);
    }

    #[test]
    fn clipboard_state_empty_paths() {
        let state = ClipboardState::new(vec![], ClipOp::Copy);
        assert!(state.paths.is_empty());
    }
}
