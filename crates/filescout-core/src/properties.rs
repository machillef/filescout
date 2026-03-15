//! File and directory properties.
//!
//! Provides a pure data structure (`FileProperties`) and the blocking functions
//! that populate it from the file system.  The UI layer calls these via
//! `tokio::task::spawn_blocking` to avoid stalling the event loop.

use std::path::{Path, PathBuf};
use std::time::SystemTime;

use anyhow::Result;
use walkdir::WalkDir;

// Windows file-attribute flags (from winnt.h).
#[cfg(target_os = "windows")]
const FILE_ATTRIBUTE_READONLY: u32 = 0x0001;
#[cfg(target_os = "windows")]
const FILE_ATTRIBUTE_HIDDEN: u32 = 0x0002;
#[cfg(target_os = "windows")]
const FILE_ATTRIBUTE_SYSTEM: u32 = 0x0004;
#[cfg(target_os = "windows")]
const FILE_ATTRIBUTE_ARCHIVE: u32 = 0x0020;

/// Complete metadata snapshot of a single file or directory.
#[derive(Debug, Clone)]
pub struct FileProperties {
    /// File or directory name (last component of the path).
    pub name: String,
    /// Absolute path.
    pub path: PathBuf,
    /// `true` when the path refers to a directory.
    pub is_dir: bool,
    /// Size in bytes.  For directories this is the shallow size of the
    /// directory entry itself; async [`compute_dir_size`] provides the total.
    pub size_bytes: u64,
    /// Creation time, if the platform reports it.
    pub created: Option<SystemTime>,
    /// Last-modified time.
    pub modified: Option<SystemTime>,
    /// Last-accessed time, if the platform reports it.
    pub accessed: Option<SystemTime>,
    /// `FILE_ATTRIBUTE_READONLY` is set (Windows) or no write permission
    /// for the owner (Unix).  Always `false` on platforms where detection is
    /// unsupported.
    pub read_only: bool,
    /// `FILE_ATTRIBUTE_HIDDEN` flag (Windows).  Always `false` elsewhere.
    pub hidden: bool,
    /// `FILE_ATTRIBUTE_SYSTEM` flag (Windows).  Always `false` elsewhere.
    pub system: bool,
    /// `FILE_ATTRIBUTE_ARCHIVE` flag (Windows).  Always `false` elsewhere.
    pub archive: bool,
}

/// Load the properties of the file or directory at `path`.
///
/// This is a blocking call; wrap it in `tokio::task::spawn_blocking` when
/// calling from an async context.
pub fn load_file_properties(path: &Path) -> Result<FileProperties> {
    let meta = std::fs::metadata(path)?;

    let name = path
        .file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_else(|| path.display().to_string());

    let is_dir = meta.is_dir();
    let size_bytes = if is_dir { 0 } else { meta.len() };

    let created = meta.created().ok();
    let modified = meta.modified().ok();
    let accessed = meta.accessed().ok();

    #[cfg(target_os = "windows")]
    let (read_only, hidden, system, archive) = {
        use std::os::windows::fs::MetadataExt;
        let attrs = meta.file_attributes();
        (
            attrs & FILE_ATTRIBUTE_READONLY != 0,
            attrs & FILE_ATTRIBUTE_HIDDEN != 0,
            attrs & FILE_ATTRIBUTE_SYSTEM != 0,
            attrs & FILE_ATTRIBUTE_ARCHIVE != 0,
        )
    };

    #[cfg(not(target_os = "windows"))]
    let (read_only, hidden, system, archive) = {
        use std::os::unix::fs::PermissionsExt;
        let mode = meta.permissions().mode();
        // On Unix, "read-only" means no write bits for user.
        let ro = mode & 0o200 == 0;
        let hid = name.starts_with('.');
        (ro, hid, false, false)
    };

    Ok(FileProperties {
        name,
        path: path.to_path_buf(),
        is_dir,
        size_bytes,
        created,
        modified,
        accessed,
        read_only,
        hidden,
        system,
        archive,
    })
}

/// Recursively sum the size of all files under `path`.
///
/// This is a blocking call; wrap it in `tokio::task::spawn_blocking`.
/// Errors accessing individual files are silently skipped.
pub fn compute_dir_size(path: &Path) -> u64 {
    WalkDir::new(path)
        .into_iter()
        .filter_map(|e| e.ok())
        .filter(|e| e.file_type().is_file())
        .filter_map(|e| e.metadata().ok())
        .map(|m| m.len())
        .sum()
}

/// Format a byte count as a human-readable string.
///
/// Uses binary prefixes (KiB, MiB, GiB, TiB).  Values below 1 KiB are shown
/// as whole bytes.
pub fn format_size(bytes: u64) -> String {
    const KIB: u64 = 1024;
    const MIB: u64 = KIB * 1024;
    const GIB: u64 = MIB * 1024;
    const TIB: u64 = GIB * 1024;

    if bytes < KIB {
        format!("{bytes} B")
    } else if bytes < MIB {
        format!("{:.1} KiB", bytes as f64 / KIB as f64)
    } else if bytes < GIB {
        format!("{:.1} MiB", bytes as f64 / MIB as f64)
    } else if bytes < TIB {
        format!("{:.1} GiB", bytes as f64 / GIB as f64)
    } else {
        format!("{:.1} TiB", bytes as f64 / TIB as f64)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // ── format_size ──────────────────────────────────────────────────────────

    #[test]
    fn format_size_zero() {
        assert_eq!(format_size(0), "0 B");
    }

    #[test]
    fn format_size_below_kib() {
        assert_eq!(format_size(1023), "1023 B");
    }

    #[test]
    fn format_size_exactly_kib() {
        assert_eq!(format_size(1024), "1.0 KiB");
    }

    #[test]
    fn format_size_one_and_half_kib() {
        assert_eq!(format_size(1536), "1.5 KiB");
    }

    #[test]
    fn format_size_exactly_mib() {
        assert_eq!(format_size(1024 * 1024), "1.0 MiB");
    }

    #[test]
    fn format_size_exactly_gib() {
        assert_eq!(format_size(1024 * 1024 * 1024), "1.0 GiB");
    }

    #[test]
    fn format_size_exactly_tib() {
        assert_eq!(format_size(1024u64.pow(4)), "1.0 TiB");
    }

    #[test]
    fn format_size_large_value() {
        // 2.5 GiB
        assert_eq!(format_size(1024 * 1024 * 1024 * 2 + 1024 * 1024 * 512), "2.5 GiB");
    }

    // ── load_file_properties ─────────────────────────────────────────────────

    #[test]
    fn load_properties_on_nonexistent_path_returns_error() {
        let result = load_file_properties(Path::new("C:\\does_not_exist_xyzzy_12345"));
        assert!(result.is_err());
    }

    #[test]
    fn load_properties_on_temp_dir_is_dir() {
        let dir = std::env::temp_dir();
        let props = load_file_properties(&dir).expect("temp_dir should be readable");
        assert!(props.is_dir);
        assert_eq!(props.size_bytes, 0, "dirs start with zero size in FileProperties");
    }

    // ── compute_dir_size ─────────────────────────────────────────────────────

    #[test]
    fn compute_dir_size_on_empty_temp_subdir() {
        let base = std::env::temp_dir().join("filescout_test_empty_dir_xyz");
        std::fs::create_dir_all(&base).unwrap();
        let size = compute_dir_size(&base);
        // An empty directory contains no files, so the sum must be 0.
        assert_eq!(size, 0);
        let _ = std::fs::remove_dir(&base);
    }
}
