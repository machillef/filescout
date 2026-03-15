use std::path::{Path, PathBuf};
use std::process::Command;

use anyhow::Result;
use walkdir::WalkDir;

/// A reversible file operation that can be undone.
#[derive(Debug, Clone)]
pub enum UndoAction {
    /// A file or directory was renamed. `from` is the old path, `to` is the new path.
    Rename { from: PathBuf, to: PathBuf },
}

/// A bounded LIFO stack of undoable actions (capped at 50 entries).
#[derive(Default)]
pub struct UndoStack {
    actions: Vec<UndoAction>,
}

impl UndoStack {
    pub fn new() -> Self {
        Self::default()
    }

    /// Push a new action. Drops the oldest entry when the stack exceeds 50 items.
    pub fn push(&mut self, action: UndoAction) {
        if self.actions.len() >= 50 {
            self.actions.remove(0);
        }
        self.actions.push(action);
    }

    /// Pop the most recent action, or `None` if the stack is empty.
    pub fn pop(&mut self) -> Option<UndoAction> {
        self.actions.pop()
    }

    pub fn can_undo(&self) -> bool {
        !self.actions.is_empty()
    }
}

/// Open `path` with the system default application.
pub fn open_file(path: &Path) -> Result<()> {
    open::that(path).map_err(|e| anyhow::anyhow!("failed to open '{}': {e}", path.display()))
}

/// Open `path` with a specific program.
pub fn open_with(path: &Path, program: &str) -> Result<()> {
    Command::new(program)
        .arg(path)
        .spawn()
        .map(|_| ())
        .map_err(|e| anyhow::anyhow!("failed to open '{}' with '{program}': {e}", path.display()))
}

/// Move `path` to the system Recycle Bin / Trash.
pub fn delete_to_trash(path: PathBuf) -> Result<()> {
    trash::delete(path).map_err(|e| anyhow::anyhow!("{e}"))
}

/// Rename `old_path` to a new entry in the same directory with `new_name`.
///
/// Returns the full path of the renamed item on success.
pub fn rename_file(old_path: PathBuf, new_name: String) -> Result<PathBuf> {
    let parent = old_path
        .parent()
        .ok_or_else(|| anyhow::anyhow!("cannot rename: no parent directory"))?;
    let new_path = parent.join(&new_name);
    std::fs::rename(&old_path, &new_path)?;
    Ok(new_path)
}

/// Copy `src` into `dest_dir`, preserving the entry name.
///
/// Directories are copied recursively. Returns the full destination path on success.
pub fn copy_item(src: PathBuf, dest_dir: PathBuf) -> Result<PathBuf> {
    let name = src
        .file_name()
        .ok_or_else(|| anyhow::anyhow!("source path has no file name"))?;
    let dest = dest_dir.join(name);
    if src.is_dir() {
        copy_dir_recursive(&src, &dest)?;
    } else {
        if let Some(parent) = dest.parent() {
            std::fs::create_dir_all(parent)?;
        }
        std::fs::copy(&src, &dest)?;
    }
    Ok(dest)
}

/// Recursively copy directory `src` to `dest` (dest must not yet exist).
fn copy_dir_recursive(src: &Path, dest: &Path) -> Result<()> {
    std::fs::create_dir_all(dest)?;
    for entry in WalkDir::new(src).min_depth(1) {
        let entry = entry?;
        let rel = entry.path().strip_prefix(src).map_err(|e| anyhow::anyhow!(e))?;
        let target = dest.join(rel);
        if entry.file_type().is_dir() {
            std::fs::create_dir_all(&target)?;
        } else {
            if let Some(parent) = target.parent() {
                std::fs::create_dir_all(parent)?;
            }
            std::fs::copy(entry.path(), &target)?;
        }
    }
    Ok(())
}

/// Move `src` into `dest_dir`, preserving the entry name.
///
/// Tries `std::fs::rename` first (fast, same filesystem). Falls back to copy-then-delete
/// for cross-device moves. Returns the full destination path on success.
pub fn move_item(src: PathBuf, dest_dir: PathBuf) -> Result<PathBuf> {
    let name = src
        .file_name()
        .ok_or_else(|| anyhow::anyhow!("source path has no file name"))?;
    let dest = dest_dir.join(name);
    // Fast path: same-filesystem rename
    if std::fs::rename(&src, &dest).is_ok() {
        return Ok(dest);
    }
    // Cross-device: copy then delete
    let dest = copy_item(src.clone(), dest_dir)?;
    if src.is_dir() {
        std::fs::remove_dir_all(&src)?;
    } else {
        std::fs::remove_file(&src)?;
    }
    Ok(dest)
}

/// Undo a rename: rename `to` back to the file-name component of `from`.
pub fn undo_rename(from: PathBuf, to: PathBuf) -> Result<()> {
    let original_name = from
        .file_name()
        .ok_or_else(|| anyhow::anyhow!("cannot undo rename: original path has no file name"))?
        .to_string_lossy()
        .into_owned();
    rename_file(to, original_name).map(|_| ())
}

/// Validate that `name` is a legal file name (non-empty, no path separators, no reserved characters).
pub fn validate_file_name(name: &str) -> Result<()> {
    if name.is_empty() {
        return Err(anyhow::anyhow!("file name cannot be empty"));
    }
    // Windows reserved characters
    const INVALID_CHARS: &[char] = &['/', '\\', ':', '*', '?', '"', '<', '>', '|'];
    if let Some(c) = name.chars().find(|c| INVALID_CHARS.contains(c)) {
        return Err(anyhow::anyhow!("file name contains invalid character: '{c}'"));
    }
    // Windows reserved names (CON, PRN, AUX, NUL, COM1-9, LPT1-9)
    let upper = name.to_uppercase();
    let stem = upper.split('.').next().unwrap_or(&upper);
    const RESERVED: &[&str] = &[
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
        "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    ];
    if RESERVED.contains(&stem) {
        return Err(anyhow::anyhow!("'{name}' is a reserved file name on Windows"));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    // --- open_file / open_with ---

    #[test]
    fn open_file_err_for_nonexistent() {
        let result = open_file(Path::new("/nonexistent/path/file.xyz"));
        assert!(result.is_err());
    }

    #[test]
    fn open_with_err_for_invalid_program() {
        let dir = tempfile::tempdir().unwrap();
        let file = dir.path().join("test.txt");
        std::fs::write(&file, b"hello").unwrap();
        let result = open_with(&file, "nonexistent_program_xyz_12345");
        assert!(result.is_err());
    }

    // --- UndoStack ---

    #[test]
    fn undo_stack_starts_empty() {
        let stack = UndoStack::new();
        assert!(!stack.can_undo());
    }

    #[test]
    fn undo_stack_can_undo_after_push() {
        let mut stack = UndoStack::new();
        stack.push(UndoAction::Rename {
            from: PathBuf::from("a"),
            to: PathBuf::from("b"),
        });
        assert!(stack.can_undo());
    }

    #[test]
    fn undo_stack_pop_returns_last_pushed_action() {
        let mut stack = UndoStack::new();
        let from = PathBuf::from("old.txt");
        let to = PathBuf::from("new.txt");
        stack.push(UndoAction::Rename {
            from: from.clone(),
            to: to.clone(),
        });
        match stack.pop().unwrap() {
            UndoAction::Rename { from: f, to: t } => {
                assert_eq!(f, from);
                assert_eq!(t, to);
            }
        }
    }

    #[test]
    fn undo_stack_empty_after_all_popped() {
        let mut stack = UndoStack::new();
        stack.push(UndoAction::Rename {
            from: PathBuf::from("a"),
            to: PathBuf::from("b"),
        });
        stack.pop();
        assert!(!stack.can_undo());
    }

    #[test]
    fn undo_stack_pop_on_empty_returns_none() {
        let mut stack = UndoStack::new();
        assert!(stack.pop().is_none());
    }

    #[test]
    fn undo_stack_respects_lifo_order() {
        let mut stack = UndoStack::new();
        stack.push(UndoAction::Rename {
            from: PathBuf::from("first"),
            to: PathBuf::from("first_new"),
        });
        stack.push(UndoAction::Rename {
            from: PathBuf::from("second"),
            to: PathBuf::from("second_new"),
        });
        match stack.pop().unwrap() {
            UndoAction::Rename { from, .. } => assert_eq!(from, PathBuf::from("second")),
        }
        match stack.pop().unwrap() {
            UndoAction::Rename { from, .. } => assert_eq!(from, PathBuf::from("first")),
        }
    }

    // --- validate_file_name ---

    #[test]
    fn validate_empty_name_fails() {
        assert!(validate_file_name("").is_err());
    }

    #[test]
    fn validate_name_with_backslash_fails() {
        assert!(validate_file_name("foo\\bar").is_err());
    }

    #[test]
    fn validate_name_with_colon_fails() {
        assert!(validate_file_name("foo:bar").is_err());
    }

    #[test]
    fn validate_reserved_name_fails() {
        assert!(validate_file_name("CON").is_err());
        assert!(validate_file_name("nul.txt").is_err());
    }

    #[test]
    fn validate_normal_name_passes() {
        assert!(validate_file_name("hello world.txt").is_ok());
        assert!(validate_file_name("my-file_2024.rs").is_ok());
    }

    // --- copy_item ---

    #[test]
    fn copy_item_copies_file_to_dest_dir() {
        let src_dir = tempfile::tempdir().unwrap();
        let dest_dir = tempfile::tempdir().unwrap();
        let src = src_dir.path().join("hello.txt");
        std::fs::write(&src, b"hello").unwrap();

        let dest = copy_item(src, dest_dir.path().to_path_buf()).unwrap();

        assert_eq!(dest, dest_dir.path().join("hello.txt"));
        assert!(dest.exists());
        assert_eq!(std::fs::read(&dest).unwrap(), b"hello");
    }

    #[test]
    fn copy_item_returns_correct_dest_path() {
        let src_dir = tempfile::tempdir().unwrap();
        let dest_dir = tempfile::tempdir().unwrap();
        let src = src_dir.path().join("data.bin");
        std::fs::write(&src, b"data").unwrap();

        let dest = copy_item(src, dest_dir.path().to_path_buf()).unwrap();

        assert_eq!(dest.file_name().unwrap(), "data.bin");
    }

    #[test]
    fn copy_item_copies_directory_recursively() {
        let tmp = tempfile::tempdir().unwrap();
        let src = tmp.path().join("srcdir");
        std::fs::create_dir_all(src.join("sub")).unwrap();
        std::fs::write(src.join("a.txt"), b"a").unwrap();
        std::fs::write(src.join("sub/b.txt"), b"b").unwrap();

        let dest_dir = tempfile::tempdir().unwrap();
        copy_item(src, dest_dir.path().to_path_buf()).unwrap();

        assert!(dest_dir.path().join("srcdir/a.txt").exists());
        assert!(dest_dir.path().join("srcdir/sub/b.txt").exists());
    }

    #[test]
    fn copy_item_fails_when_source_missing() {
        let dest_dir = tempfile::tempdir().unwrap();
        let src = PathBuf::from("/nonexistent/path/file.txt");
        assert!(copy_item(src, dest_dir.path().to_path_buf()).is_err());
    }

    // --- move_item ---

    #[test]
    fn move_item_moves_file_to_dest_dir() {
        let src_dir = tempfile::tempdir().unwrap();
        let dest_dir = tempfile::tempdir().unwrap();
        let src = src_dir.path().join("move_me.txt");
        std::fs::write(&src, b"content").unwrap();

        let dest = move_item(src.clone(), dest_dir.path().to_path_buf()).unwrap();

        assert_eq!(dest, dest_dir.path().join("move_me.txt"));
        assert!(dest.exists());
        assert!(!src.exists());
    }

    #[test]
    fn move_item_returns_correct_dest_path() {
        let src_dir = tempfile::tempdir().unwrap();
        let dest_dir = tempfile::tempdir().unwrap();
        let src = src_dir.path().join("file.txt");
        std::fs::write(&src, b"x").unwrap();

        let dest = move_item(src, dest_dir.path().to_path_buf()).unwrap();

        assert_eq!(dest.file_name().unwrap(), "file.txt");
    }

    #[test]
    fn move_item_removes_source() {
        let src_dir = tempfile::tempdir().unwrap();
        let dest_dir = tempfile::tempdir().unwrap();
        let src = src_dir.path().join("gone.txt");
        std::fs::write(&src, b"bye").unwrap();

        move_item(src.clone(), dest_dir.path().to_path_buf()).unwrap();

        assert!(!src.exists());
    }

    #[test]
    fn move_item_moves_directory() {
        let tmp = tempfile::tempdir().unwrap();
        let src = tmp.path().join("movedir");
        std::fs::create_dir_all(&src).unwrap();
        std::fs::write(src.join("file.txt"), b"inside").unwrap();

        let dest_dir = tempfile::tempdir().unwrap();
        let dest = move_item(src.clone(), dest_dir.path().to_path_buf()).unwrap();

        assert!(dest.join("file.txt").exists());
        assert!(!src.exists());
    }

    // --- rename_file (integration) ---

    #[test]
    fn rename_file_renames_correctly() {
        let dir = tempfile::tempdir().unwrap();
        let old = dir.path().join("original.txt");
        std::fs::write(&old, b"content").unwrap();

        let new = rename_file(old.clone(), "renamed.txt".into()).unwrap();

        assert_eq!(new, dir.path().join("renamed.txt"));
        assert!(new.exists());
        assert!(!old.exists());
    }

    #[test]
    fn rename_file_fails_when_source_missing() {
        let dir = tempfile::tempdir().unwrap();
        let missing = dir.path().join("ghost.txt");
        assert!(rename_file(missing, "whatever.txt".into()).is_err());
    }
}
