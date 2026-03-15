//! ZIP archive browsing and extraction.

use std::collections::HashSet;
use std::fs::{self, File};
use std::io;
use std::path::Path;

use anyhow::{Context, Result};
use chrono::{Local, TimeZone};

use crate::models::FileEntry;

/// Returns `true` if `path` has a `.zip` extension (case-insensitive).
pub fn is_zip(path: &Path) -> bool {
    path.extension()
        .and_then(|e| e.to_str())
        .map(|e| e.eq_ignore_ascii_case("zip"))
        .unwrap_or(false)
}

/// List the immediate children of `inner_dir` within the ZIP at `zip_path`.
///
/// `inner_dir` is forward-slash separated (e.g., `"docs"` or `"a/b"`). Use `""` for the
/// archive root. Returns `FileEntry` values where each `path` is a virtual path constructed
/// as `zip_path.join(inner_path_with_os_separators)` — these paths do not exist on disk.
pub fn list_archive_dir(zip_path: &Path, inner_dir: &str) -> Result<Vec<FileEntry>> {
    let file = File::open(zip_path).context("opening zip file")?;
    let mut archive = zip::ZipArchive::new(file).context("parsing zip archive")?;

    // Prefix to strip when looking at entry names.
    let prefix = if inner_dir.is_empty() {
        String::new()
    } else {
        format!("{}/", inner_dir.trim_end_matches('/'))
    };

    let mut seen: HashSet<String> = HashSet::new();
    let mut entries: Vec<FileEntry> = Vec::new();

    for i in 0..archive.len() {
        let zip_entry = archive.by_index(i)?;
        let full_name = zip_entry.name().to_owned();

        // Skip entries not under our prefix.
        if !full_name.starts_with(&prefix) {
            continue;
        }

        // Part after the prefix.
        let relative = &full_name[prefix.len()..];
        if relative.is_empty() {
            // This is the directory entry itself.
            continue;
        }

        // First path component after the prefix.
        let slash_pos = relative.find('/');
        let (first_component, is_dir) = if let Some(pos) = slash_pos {
            // If there's content after the slash, this component is a directory.
            // If it's empty (e.g., "dir/"), this entry IS the dir — also treat as dir.
            (&relative[..pos], true)
        } else {
            (relative, false)
        };

        if first_component.is_empty() {
            continue;
        }

        let name = first_component.to_owned();
        if seen.contains(&name) {
            continue;
        }
        seen.insert(name.clone());

        // Build the inner_path for this entry (forward-slash separated).
        let inner_path = if prefix.is_empty() {
            name.clone()
        } else {
            format!("{}{}", prefix, name)
        };

        // Virtual path: zip_path joined with the inner_path converted to OS separators.
        let os_inner = inner_path.replace('/', std::path::MAIN_SEPARATOR_STR);
        let virtual_path = zip_path.join(os_inner);

        let (size, modified) = if is_dir {
            (0u64, None)
        } else {
            let size = zip_entry.size();
            let modified = zip_entry.last_modified().and_then(zip_dt_to_local);
            (size, modified)
        };

        entries.push(FileEntry {
            name,
            path: virtual_path,
            is_dir,
            size,
            modified,
        });
    }

    Ok(entries)
}

/// Extract an entire ZIP archive to `dest_dir`.
pub fn extract_archive(zip_path: &Path, dest_dir: &Path) -> Result<()> {
    let file = File::open(zip_path).context("opening zip file")?;
    let mut archive = zip::ZipArchive::new(file).context("parsing zip archive")?;
    archive.extract(dest_dir).context("extracting archive")?;
    Ok(())
}

/// Extract a single entry (file or directory subtree identified by `inner_path`) from a
/// ZIP archive to `dest_dir`.
///
/// `inner_path` uses forward slashes (e.g., `"docs/guide.pdf"` or `"docs"`).
/// Directories are extracted recursively. The extracted content is placed relative to
/// `dest_dir` preserving only the structure *within* the entry (the parent components of
/// `inner_path` are stripped).
pub fn extract_entry(zip_path: &Path, inner_path: &str, dest_dir: &Path) -> Result<()> {
    let file = File::open(zip_path).context("opening zip file")?;
    let mut archive = zip::ZipArchive::new(file).context("parsing zip archive")?;

    // Prefix to match directory contents.
    let dir_prefix = format!("{}/", inner_path.trim_end_matches('/'));

    // The parent portion to strip so that extracted paths are relative.
    let strip_prefix: &str = inner_path
        .trim_end_matches('/')
        .rfind('/')
        .map(|i| &inner_path[..=i])
        .unwrap_or("");

    for i in 0..archive.len() {
        let mut zip_entry = archive.by_index(i)?;
        let name = zip_entry.name().to_owned();

        // Match the exact file or any entry under the directory prefix.
        if name != inner_path && !name.starts_with(&dir_prefix) {
            continue;
        }

        // Strip the parent prefix to get the relative path within dest_dir.
        let relative = if let Some(stripped) = name.strip_prefix(strip_prefix) {
            stripped.trim_end_matches('/')
        } else {
            name.trim_end_matches('/')
        };

        if relative.is_empty() {
            // Skip directory entries that map to dest_dir itself.
            continue;
        }

        let out_path = dest_dir.join(relative.replace('/', std::path::MAIN_SEPARATOR_STR));

        if zip_entry.is_dir() {
            fs::create_dir_all(&out_path)
                .with_context(|| format!("creating directory {}", out_path.display()))?;
        } else {
            if let Some(parent) = out_path.parent() {
                fs::create_dir_all(parent)
                    .with_context(|| format!("creating parent dir {}", parent.display()))?;
            }
            let mut out_file = File::create(&out_path)
                .with_context(|| format!("creating {}", out_path.display()))?;
            io::copy(&mut zip_entry, &mut out_file)
                .with_context(|| format!("writing {}", out_path.display()))?;
        }
    }

    Ok(())
}

fn zip_dt_to_local(dt: zip::DateTime) -> Option<chrono::DateTime<Local>> {
    Local
        .with_ymd_and_hms(
            dt.year().into(),
            dt.month().into(),
            dt.day().into(),
            dt.hour().into(),
            dt.minute().into(),
            dt.second().into(),
        )
        .single()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use std::path::PathBuf;
    use tempfile::TempDir;
    use zip::write::SimpleFileOptions;

    /// Create a test ZIP with the following structure:
    /// ```
    /// readme.txt
    /// docs/
    ///   guide.pdf
    ///   images/
    ///     photo.jpg
    /// ```
    fn make_test_zip(dir: &Path) -> PathBuf {
        let zip_path = dir.join("test.zip");
        let file = File::create(&zip_path).unwrap();
        let mut zw = zip::ZipWriter::new(file);
        let opts = SimpleFileOptions::default();

        zw.start_file("readme.txt", opts).unwrap();
        zw.write_all(b"hello world").unwrap();

        zw.add_directory("docs/", opts).unwrap();

        zw.start_file("docs/guide.pdf", opts).unwrap();
        zw.write_all(b"guide content").unwrap();

        zw.start_file("docs/images/photo.jpg", opts).unwrap();
        zw.write_all(b"image data").unwrap();

        zw.finish().unwrap();
        zip_path
    }

    // ── is_zip ──────────────────────────────────────────────────────────────

    #[test]
    fn is_zip_detects_zip_extension() {
        assert!(is_zip(Path::new("archive.zip")));
        assert!(is_zip(Path::new("ARCHIVE.ZIP")));
        assert!(is_zip(Path::new("Archive.Zip")));
    }

    #[test]
    fn is_zip_rejects_non_zip() {
        assert!(!is_zip(Path::new("file.txt")));
        assert!(!is_zip(Path::new("file.tar.gz")));
        assert!(!is_zip(Path::new("noextension")));
    }

    // ── list_archive_dir ────────────────────────────────────────────────────

    #[test]
    fn list_archive_root_returns_top_level_entries() {
        let tmp = TempDir::new().unwrap();
        let zip_path = make_test_zip(tmp.path());
        let entries = list_archive_dir(&zip_path, "").unwrap();

        let names: Vec<&str> = entries.iter().map(|e| e.name.as_str()).collect();
        assert!(names.contains(&"readme.txt"), "root should have readme.txt");
        assert!(names.contains(&"docs"), "root should have docs dir");
        assert_eq!(entries.len(), 2);
    }

    #[test]
    fn list_archive_root_entry_types_are_correct() {
        let tmp = TempDir::new().unwrap();
        let zip_path = make_test_zip(tmp.path());
        let entries = list_archive_dir(&zip_path, "").unwrap();

        let docs = entries.iter().find(|e| e.name == "docs").unwrap();
        assert!(docs.is_dir, "docs should be a directory");

        let readme = entries.iter().find(|e| e.name == "readme.txt").unwrap();
        assert!(!readme.is_dir, "readme.txt should be a file");
        assert_eq!(readme.size, b"hello world".len() as u64);
    }

    #[test]
    fn list_archive_subdir_returns_correct_entries() {
        let tmp = TempDir::new().unwrap();
        let zip_path = make_test_zip(tmp.path());
        let entries = list_archive_dir(&zip_path, "docs").unwrap();

        let names: Vec<&str> = entries.iter().map(|e| e.name.as_str()).collect();
        assert!(names.contains(&"guide.pdf"));
        assert!(names.contains(&"images"));
        assert_eq!(entries.len(), 2);

        let images = entries.iter().find(|e| e.name == "images").unwrap();
        assert!(images.is_dir);
    }

    #[test]
    fn list_archive_nested_subdir_returns_correct_entries() {
        let tmp = TempDir::new().unwrap();
        let zip_path = make_test_zip(tmp.path());
        let entries = list_archive_dir(&zip_path, "docs/images").unwrap();

        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].name, "photo.jpg");
        assert!(!entries[0].is_dir);
    }

    #[test]
    fn list_archive_deduplicates_subdirs_without_explicit_dir_entry() {
        let tmp = TempDir::new().unwrap();
        let zip_path = tmp.path().join("flat.zip");
        let file = File::create(&zip_path).unwrap();
        let mut zw = zip::ZipWriter::new(file);
        let opts = SimpleFileOptions::default();

        // Two files in the same subdir, no explicit dir entry
        zw.start_file("subdir/file1.txt", opts).unwrap();
        zw.write_all(b"a").unwrap();
        zw.start_file("subdir/file2.txt", opts).unwrap();
        zw.write_all(b"b").unwrap();
        zw.finish().unwrap();

        let entries = list_archive_dir(&zip_path, "").unwrap();
        assert_eq!(entries.len(), 1, "subdir should appear only once");
        assert_eq!(entries[0].name, "subdir");
        assert!(entries[0].is_dir);
    }

    #[test]
    fn list_archive_virtual_paths_start_with_zip_path() {
        let tmp = TempDir::new().unwrap();
        let zip_path = make_test_zip(tmp.path());
        let entries = list_archive_dir(&zip_path, "").unwrap();
        for entry in &entries {
            assert!(
                entry.path.starts_with(&zip_path),
                "{:?} should start with {:?}",
                entry.path,
                zip_path
            );
        }
    }

    // ── extract_archive ─────────────────────────────────────────────────────

    #[test]
    fn extract_archive_creates_all_files() {
        let tmp = TempDir::new().unwrap();
        let zip_path = make_test_zip(tmp.path());
        let dest = tmp.path().join("extracted");
        fs::create_dir(&dest).unwrap();
        extract_archive(&zip_path, &dest).unwrap();

        assert!(dest.join("readme.txt").exists());
        assert!(dest.join("docs").join("guide.pdf").exists());
        assert!(dest.join("docs").join("images").join("photo.jpg").exists());
    }

    // ── extract_entry ────────────────────────────────────────────────────────

    #[test]
    fn extract_single_file_entry_places_file_in_dest() {
        let tmp = TempDir::new().unwrap();
        let zip_path = make_test_zip(tmp.path());
        let dest = tmp.path().join("out");
        fs::create_dir(&dest).unwrap();
        extract_entry(&zip_path, "readme.txt", &dest).unwrap();

        assert!(dest.join("readme.txt").exists());
        assert!(!dest.join("docs").exists(), "docs should not be extracted");
    }

    #[test]
    fn extract_directory_entry_preserves_subtree() {
        let tmp = TempDir::new().unwrap();
        let zip_path = make_test_zip(tmp.path());
        let dest = tmp.path().join("out");
        fs::create_dir(&dest).unwrap();
        extract_entry(&zip_path, "docs", &dest).unwrap();

        assert!(dest.join("docs").join("guide.pdf").exists());
        assert!(dest.join("docs").join("images").join("photo.jpg").exists());
        assert!(!dest.join("readme.txt").exists());
    }

    #[test]
    fn extract_nested_file_strips_parent_prefix() {
        let tmp = TempDir::new().unwrap();
        let zip_path = make_test_zip(tmp.path());
        let dest = tmp.path().join("out");
        fs::create_dir(&dest).unwrap();
        extract_entry(&zip_path, "docs/guide.pdf", &dest).unwrap();

        assert!(dest.join("guide.pdf").exists(), "guide.pdf should be in dest root");
        assert!(!dest.join("docs").exists());
    }
}
