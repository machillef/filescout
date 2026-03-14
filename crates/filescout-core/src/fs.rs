use std::path::Path;

use anyhow::Result;
use chrono::{DateTime, Local};

use crate::models::FileEntry;

/// List all entries (files and directories) in the given directory.
pub fn list_directory(path: &Path) -> Result<Vec<FileEntry>> {
    let mut entries = Vec::new();

    for entry in std::fs::read_dir(path)? {
        let entry = entry?;
        let metadata = entry.metadata()?;
        let modified = metadata
            .modified()
            .ok()
            .map(DateTime::<Local>::from);

        entries.push(FileEntry {
            name: entry.file_name().to_string_lossy().into_owned(),
            path: entry.path(),
            is_dir: metadata.is_dir(),
            size: metadata.len(),
            modified,
        });
    }

    entries.sort_by(|a, b| {
        b.is_dir.cmp(&a.is_dir).then(
            a.name
                .to_lowercase()
                .cmp(&b.name.to_lowercase()),
        )
    });

    Ok(entries)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::env;

    #[test]
    fn list_current_directory() {
        let cwd = env::current_dir().unwrap();
        let entries = list_directory(&cwd).unwrap();
        assert!(!entries.is_empty(), "current directory should not be empty");
    }

    #[test]
    fn directories_sorted_first() {
        let cwd = env::current_dir().unwrap();
        let entries = list_directory(&cwd).unwrap();

        let first_file_idx = entries.iter().position(|e| !e.is_dir);
        let last_dir_idx = entries.iter().rposition(|e| e.is_dir);

        if let (Some(first_file), Some(last_dir)) = (first_file_idx, last_dir_idx) {
            assert!(
                last_dir < first_file,
                "all directories should appear before files"
            );
        }
    }
}
