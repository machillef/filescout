use std::path::Path;

use anyhow::Result;
use chrono::{DateTime, Local};

use crate::models::{FileEntry, SortColumn, SortDirection};

/// List all entries in the given directory, sorted dirs-first then by name ascending.
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

    sort_entries(&mut entries, &SortColumn::Name, &SortDirection::Asc);

    Ok(entries)
}

/// Sort entries in-place: directories always first, then by the given column and direction.
pub fn sort_entries(entries: &mut [FileEntry], col: &SortColumn, dir: &SortDirection) {
    entries.sort_by(|a, b| {
        b.is_dir.cmp(&a.is_dir).then_with(|| {
            let ord = match col {
                SortColumn::Name => a.name.to_lowercase().cmp(&b.name.to_lowercase()),
                SortColumn::Size => a.size.cmp(&b.size),
                SortColumn::Modified => a.modified.cmp(&b.modified),
            };
            if *dir == SortDirection::Desc {
                ord.reverse()
            } else {
                ord
            }
        })
    });
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

    #[test]
    fn sort_by_size_desc_keeps_dirs_first() {
        let mut entries = vec![
            FileEntry {
                name: "file_small.txt".into(),
                path: "file_small.txt".into(),
                is_dir: false,
                size: 10,
                modified: None,
            },
            FileEntry {
                name: "dir_a".into(),
                path: "dir_a".into(),
                is_dir: true,
                size: 0,
                modified: None,
            },
            FileEntry {
                name: "file_large.bin".into(),
                path: "file_large.bin".into(),
                is_dir: false,
                size: 9999,
                modified: None,
            },
        ];

        sort_entries(&mut entries, &SortColumn::Size, &SortDirection::Desc);

        assert!(entries[0].is_dir, "directory must remain first");
        assert_eq!(entries[1].name, "file_large.bin");
        assert_eq!(entries[2].name, "file_small.txt");
    }
}
