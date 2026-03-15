use std::os::windows::fs::MetadataExt;
use std::path::Path;

use anyhow::Result;
use chrono::{DateTime, Local};

use crate::models::{FileEntry, SortColumn, SortDirection};

/// Windows file attribute flag for hidden files.
const FILE_ATTRIBUTE_HIDDEN: u32 = 0x2;
/// Windows file attribute flag for system files.
const FILE_ATTRIBUTE_SYSTEM: u32 = 0x4;

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

        let attrs = metadata.file_attributes();
        let is_hidden =
            (attrs & FILE_ATTRIBUTE_HIDDEN != 0) || (attrs & FILE_ATTRIBUTE_SYSTEM != 0);

        entries.push(FileEntry {
            name: entry.file_name().to_string_lossy().into_owned(),
            path: entry.path(),
            is_dir: metadata.is_dir(),
            size: metadata.len(),
            modified,
            is_hidden,
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

/// List available drive roots on Windows by probing A:\ through Z:\.
pub fn list_drives() -> Vec<std::path::PathBuf> {
    ('A'..='Z')
        .map(|c| std::path::PathBuf::from(format!("{}:\\", c)))
        .filter(|p| p.exists())
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::env;

    #[test]
    fn list_drives_returns_at_least_c_drive() {
        let drives = list_drives();
        assert!(
            drives.iter().any(|d| d == &std::path::PathBuf::from("C:\\")),
            "C:\\ should be present on a Windows dev machine"
        );
    }

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
    fn list_directory_populates_is_hidden() {
        // list_directory on cwd should produce entries with is_hidden set.
        let cwd = env::current_dir().unwrap();
        let entries = list_directory(&cwd).unwrap();
        // Just verify the field is populated (all false or some true).
        // We mainly test that it compiles and doesn't panic.
        for entry in &entries {
            let _ = entry.is_hidden;
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
                is_hidden: false,
            },
            FileEntry {
                name: "dir_a".into(),
                path: "dir_a".into(),
                is_dir: true,
                size: 0,
                modified: None,
                is_hidden: false,
            },
            FileEntry {
                name: "file_large.bin".into(),
                path: "file_large.bin".into(),
                is_dir: false,
                size: 9999,
                modified: None,
                is_hidden: false,
            },
        ];

        sort_entries(&mut entries, &SortColumn::Size, &SortDirection::Desc);

        assert!(entries[0].is_dir, "directory must remain first");
        assert_eq!(entries[1].name, "file_large.bin");
        assert_eq!(entries[2].name, "file_small.txt");
    }
}
