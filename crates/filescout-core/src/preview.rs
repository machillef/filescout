//! File preview loading — classifies entries and reads content for the preview panel.

use std::io::Read;
use std::path::{Path, PathBuf};

/// Maximum bytes read from a text file for the preview panel.
const MAX_TEXT_BYTES: usize = 16_384; // 16 KB

/// Classified content for the preview panel.
#[derive(Debug, Clone)]
pub enum PreviewContent {
    /// No file is selected; the panel is blank.
    None,
    /// A preview load is in progress.
    Loading,
    /// Text content (first `MAX_TEXT_BYTES` bytes decoded as UTF-8).
    Text { content: String, truncated: bool },
    /// An image file: the path is passed directly to iced's image widget.
    Image(PathBuf),
    /// Non-text, non-image file: show basic metadata.
    Metadata { kind: String, size: u64 },
    /// An error occurred while reading.
    Error(String),
}

/// Returns `true` for extensions that are rendered as plain text in the preview.
pub fn is_text_extension(ext: &str) -> bool {
    matches!(
        ext.to_lowercase().as_str(),
        "txt"
            | "rs"
            | "toml"
            | "md"
            | "json"
            | "xml"
            | "yaml"
            | "yml"
            | "log"
            | "csv"
            | "ini"
            | "cfg"
            | "conf"
            | "html"
            | "htm"
            | "css"
            | "js"
            | "ts"
            | "py"
            | "sh"
            | "bat"
            | "ps1"
            | "c"
            | "h"
            | "cpp"
            | "cs"
            | "java"
            | "go"
            | "rb"
            | "php"
            | "sql"
            | "gitignore"
            | "env"
            | "lock"
    )
}

/// Returns `true` for extensions that iced can render as images.
pub fn is_image_extension(ext: &str) -> bool {
    matches!(
        ext.to_lowercase().as_str(),
        "png" | "jpg" | "jpeg" | "gif" | "bmp" | "ico" | "webp" | "tiff" | "tif"
    )
}

/// Load preview content for `path`.
///
/// - Directories → `Metadata` with kind "Directory".
/// - Recognised image extensions → `Image(path)` (iced loads from path at render time).
/// - Recognised text extensions → reads up to `MAX_TEXT_BYTES` bytes → `Text`.
/// - Everything else → `Metadata` showing file size.
///
/// Call this inside `tokio::task::spawn_blocking` — file I/O is synchronous.
pub fn load_preview(path: &Path) -> PreviewContent {
    let meta = match path.metadata() {
        Ok(m) => m,
        Err(e) => return PreviewContent::Error(e.to_string()),
    };

    if meta.is_dir() {
        return PreviewContent::Metadata { kind: "Directory".to_owned(), size: 0 };
    }

    let ext = path.extension().map(|e| e.to_string_lossy().to_lowercase()).unwrap_or_default();

    if is_image_extension(&ext) {
        return PreviewContent::Image(path.to_path_buf());
    }

    if is_text_extension(&ext) {
        return load_text_preview(path);
    }

    PreviewContent::Metadata {
        kind: if ext.is_empty() {
            "File".to_owned()
        } else {
            ext.to_uppercase()
        },
        size: meta.len(),
    }
}

fn load_text_preview(path: &Path) -> PreviewContent {
    let mut file = match std::fs::File::open(path) {
        Ok(f) => f,
        Err(e) => return PreviewContent::Error(e.to_string()),
    };

    // Read one extra byte to detect truncation.
    let mut buf = vec![0u8; MAX_TEXT_BYTES + 1];
    let n = match file.read(&mut buf) {
        Ok(n) => n,
        Err(e) => return PreviewContent::Error(e.to_string()),
    };

    let truncated = n > MAX_TEXT_BYTES;
    buf.truncate(n.min(MAX_TEXT_BYTES));

    // Decode as UTF-8, replacing invalid sequences.
    let content = String::from_utf8(buf)
        .unwrap_or_else(|e| String::from_utf8_lossy(&e.into_bytes()).into_owned());

    PreviewContent::Text { content, truncated }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    #[test]
    fn is_text_extension_recognises_rust() {
        assert!(is_text_extension("rs"));
    }

    #[test]
    fn is_text_extension_case_insensitive() {
        assert!(is_text_extension("RS"));
        assert!(is_text_extension("Toml"));
    }

    #[test]
    fn is_text_extension_rejects_binary() {
        assert!(!is_text_extension("exe"));
        assert!(!is_text_extension("dll"));
        assert!(!is_text_extension("pdf"));
    }

    #[test]
    fn is_image_extension_recognises_png_jpg() {
        assert!(is_image_extension("png"));
        assert!(is_image_extension("jpg"));
        assert!(is_image_extension("JPEG"));
    }

    #[test]
    fn is_image_extension_rejects_non_image() {
        assert!(!is_image_extension("rs"));
        assert!(!is_image_extension("pdf"));
    }

    #[test]
    fn load_preview_text_file_returns_text_content() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("hello.rs");
        fs::write(&path, "fn main() {}\n").unwrap();

        match load_preview(&path) {
            PreviewContent::Text { content, truncated } => {
                assert!(content.contains("fn main"));
                assert!(!truncated);
            }
            other => panic!("expected Text, got {:?}", other),
        }
    }

    #[test]
    fn load_preview_image_file_returns_image_path() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("photo.png");
        fs::write(&path, b"not a real png but extension matches").unwrap();

        match load_preview(&path) {
            PreviewContent::Image(p) => assert_eq!(p, path),
            other => panic!("expected Image, got {:?}", other),
        }
    }

    #[test]
    fn load_preview_unknown_extension_returns_metadata() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("archive.zip");
        fs::write(&path, b"PK\x03\x04fake zip").unwrap();

        match load_preview(&path) {
            PreviewContent::Metadata { kind, .. } => assert_eq!(kind, "ZIP"),
            other => panic!("expected Metadata, got {:?}", other),
        }
    }

    #[test]
    fn load_preview_nonexistent_file_returns_error() {
        let path = std::path::PathBuf::from("C:\\does_not_exist_filescout_test.txt");
        match load_preview(&path) {
            PreviewContent::Error(_) => {}
            other => panic!("expected Error, got {:?}", other),
        }
    }

    #[test]
    fn load_preview_large_text_file_is_truncated() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("large.txt");
        // Write more than MAX_TEXT_BYTES
        let big = "x".repeat(MAX_TEXT_BYTES + 100);
        fs::write(&path, big.as_bytes()).unwrap();

        match load_preview(&path) {
            PreviewContent::Text { truncated, content } => {
                assert!(truncated, "large file should be truncated");
                assert_eq!(content.len(), MAX_TEXT_BYTES);
            }
            other => panic!("expected Text, got {:?}", other),
        }
    }

    #[test]
    fn load_preview_directory_returns_metadata() {
        let dir = tempfile::tempdir().unwrap();
        match load_preview(dir.path()) {
            PreviewContent::Metadata { kind, .. } => assert_eq!(kind, "Directory"),
            other => panic!("expected Metadata, got {:?}", other),
        }
    }
}
