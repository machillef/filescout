//! Batch rename logic — pattern-based file renaming with live preview.
//!
//! # Pattern tokens
//!
//! | Token     | Expands to                                                |
//! |-----------|-----------------------------------------------------------|
//! | `{name}`  | Original file stem (no extension)                         |
//! | `{ext}`   | Original extension (no leading dot)                       |
//! | `{n}`     | 1-based sequence number                                   |
//! | `{n:NN}`  | 1-based sequence number, zero-padded to NN digits         |
//!
//! Unknown tokens are emitted verbatim so the user can spot typos.

use std::path::PathBuf;

use crate::models::FileEntry;
use crate::ops::validate_file_name;

/// Result of applying a rename pattern to one file entry.
#[derive(Debug, Clone)]
pub struct BatchRenamePreview {
    /// Full path of the original file.
    pub original: PathBuf,
    /// Proposed new file name (leaf only, not a full path).
    pub new_name: String,
    /// Validation error, if any. When `Some`, this rename will be skipped on apply.
    pub error: Option<String>,
}

/// Apply `pattern` to a single file, substituting the pattern tokens.
///
/// `name` is the file stem (no extension), `ext` is the extension without a leading dot,
/// and `index` is the zero-based position in the rename sequence (displayed as 1-based).
pub fn apply_pattern(pattern: &str, name: &str, ext: &str, index: usize) -> String {
    let mut result = String::with_capacity(pattern.len());
    let mut chars = pattern.chars().peekable();

    while let Some(c) = chars.next() {
        if c != '{' {
            result.push(c);
            continue;
        }

        // Collect everything inside the braces
        let mut token = String::new();
        for inner in chars.by_ref() {
            if inner == '}' {
                break;
            }
            token.push(inner);
        }

        match token.as_str() {
            "name" => result.push_str(name),
            "ext" => result.push_str(ext),
            "n" => result.push_str(&(index + 1).to_string()),
            t if t.starts_with("n:") => {
                let width_str = &t[2..];
                if let Ok(width) = width_str.parse::<usize>() {
                    result.push_str(&format!("{:0>width$}", index + 1));
                } else {
                    // Unrecognised padding spec — emit verbatim
                    result.push('{');
                    result.push_str(t);
                    result.push('}');
                }
            }
            _ => {
                // Unknown token — emit verbatim so the user can see the typo
                result.push('{');
                result.push_str(&token);
                result.push('}');
            }
        }
    }

    result
}

/// Build a preview of the renames that would result from applying `pattern` to `entries`.
///
/// Only file entries (non-directories) are included. If `ext_filter` is `Some`, only
/// files whose extension matches case-insensitively are included. The sequence index
/// (`{n}` / `{n:NN}`) is relative to the filtered subset.
pub fn preview_batch_rename(
    entries: &[FileEntry],
    pattern: &str,
    ext_filter: Option<&str>,
) -> Vec<BatchRenamePreview> {
    let files: Vec<&FileEntry> = entries
        .iter()
        .filter(|e| !e.is_dir)
        .filter(|e| {
            ext_filter.is_none_or(|f| {
                e.path
                    .extension()
                    .map(|x| x.to_string_lossy().eq_ignore_ascii_case(f))
                    .unwrap_or(false)
            })
        })
        .collect();

    files
        .into_iter()
        .enumerate()
        .map(|(i, entry)| {
            let stem = entry
                .path
                .file_stem()
                .map(|s| s.to_string_lossy().into_owned())
                .unwrap_or_default();
            let ext = entry
                .path
                .extension()
                .map(|s| s.to_string_lossy().into_owned())
                .unwrap_or_default();

            let new_name = apply_pattern(pattern, &stem, &ext, i);
            let error = validate_file_name(&new_name).err().map(|e| e.to_string());

            BatchRenamePreview { original: entry.path.clone(), new_name, error }
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn file(path: &str) -> FileEntry {
        let p = PathBuf::from(path);
        FileEntry {
            name: p.file_name().unwrap().to_string_lossy().into_owned(),
            path: p,
            is_dir: false,
            size: 0,
            modified: None,
        }
    }

    fn dir(path: &str) -> FileEntry {
        let p = PathBuf::from(path);
        FileEntry {
            name: p.file_name().unwrap().to_string_lossy().into_owned(),
            path: p,
            is_dir: true,
            size: 0,
            modified: None,
        }
    }

    // ── apply_pattern ──────────────────────────────────────────────────────

    #[test]
    fn apply_name_placeholder() {
        assert_eq!(apply_pattern("{name}", "document", "txt", 0), "document");
    }

    #[test]
    fn apply_ext_placeholder() {
        assert_eq!(apply_pattern("{ext}", "document", "txt", 0), "txt");
    }

    #[test]
    fn apply_name_dot_ext() {
        assert_eq!(apply_pattern("{name}.{ext}", "photo", "jpg", 0), "photo.jpg");
    }

    #[test]
    fn apply_n_is_one_based() {
        assert_eq!(apply_pattern("{n}", "file", "txt", 0), "1");
        assert_eq!(apply_pattern("{n}", "file", "txt", 4), "5");
    }

    #[test]
    fn apply_n_padded_two_digits() {
        assert_eq!(apply_pattern("{n:02}", "file", "txt", 0), "01");
        assert_eq!(apply_pattern("{n:02}", "file", "txt", 9), "10");
    }

    #[test]
    fn apply_n_padded_three_digits() {
        assert_eq!(apply_pattern("{n:03}", "file", "txt", 0), "001");
        assert_eq!(apply_pattern("{n:03}", "file", "txt", 99), "100");
    }

    #[test]
    fn apply_literal_only() {
        assert_eq!(apply_pattern("fixed_name.txt", "doc", "txt", 0), "fixed_name.txt");
    }

    #[test]
    fn apply_mixed_pattern() {
        assert_eq!(apply_pattern("photo_{n:03}.{ext}", "photo", "jpg", 0), "photo_001.jpg");
        assert_eq!(apply_pattern("photo_{n:03}.{ext}", "photo", "jpg", 2), "photo_003.jpg");
    }

    #[test]
    fn apply_unknown_token_emitted_verbatim() {
        assert_eq!(apply_pattern("{unknown}", "file", "txt", 0), "{unknown}");
    }

    #[test]
    fn apply_invalid_padding_spec_emitted_verbatim() {
        assert_eq!(apply_pattern("{n:abc}", "file", "txt", 0), "{n:abc}");
    }

    #[test]
    fn apply_empty_ext_produces_trailing_dot() {
        // Files with no extension have ext = ""; the dot in the pattern is literal
        assert_eq!(apply_pattern("{name}.{ext}", "README", "", 0), "README.");
    }

    #[test]
    fn apply_empty_pattern_produces_empty_string() {
        assert_eq!(apply_pattern("", "file", "txt", 0), "");
    }

    // ── preview_batch_rename ───────────────────────────────────────────────

    #[test]
    fn preview_excludes_directories() {
        let entries = vec![
            file("C:\\dir\\a.txt"),
            dir("C:\\dir\\subdir"),
            file("C:\\dir\\b.txt"),
        ];
        let previews = preview_batch_rename(&entries, "{name}.{ext}", None);
        assert_eq!(previews.len(), 2);
        assert!(previews.iter().all(|p| !p.original.to_str().unwrap().contains("subdir")));
    }

    #[test]
    fn preview_applies_pattern_in_order() {
        let entries = vec![
            file("C:\\dir\\a.txt"),
            file("C:\\dir\\b.txt"),
            file("C:\\dir\\c.txt"),
        ];
        let previews = preview_batch_rename(&entries, "{n:02}_{name}.{ext}", None);
        assert_eq!(previews[0].new_name, "01_a.txt");
        assert_eq!(previews[1].new_name, "02_b.txt");
        assert_eq!(previews[2].new_name, "03_c.txt");
    }

    #[test]
    fn preview_ext_filter_case_insensitive() {
        let entries = vec![
            file("C:\\dir\\photo.jpg"),
            file("C:\\dir\\doc.txt"),
            file("C:\\dir\\image.JPG"),
        ];
        let previews = preview_batch_rename(&entries, "{name}_copy.{ext}", Some("jpg"));
        assert_eq!(previews.len(), 2);
        assert!(previews.iter().any(|p| p.original.ends_with("photo.jpg")));
        assert!(previews.iter().any(|p| p.original.ends_with("image.JPG")));
    }

    #[test]
    fn preview_empty_pattern_flags_validation_error() {
        let entries = vec![file("C:\\dir\\a.txt")];
        let previews = preview_batch_rename(&entries, "", None);
        assert!(previews[0].error.is_some());
    }

    #[test]
    fn preview_valid_pattern_has_no_error() {
        let entries = vec![file("C:\\dir\\a.txt")];
        let previews = preview_batch_rename(&entries, "{name}.{ext}", None);
        assert!(previews[0].error.is_none());
        assert_eq!(previews[0].new_name, "a.txt");
    }

    #[test]
    fn preview_empty_entries_returns_empty() {
        let previews = preview_batch_rename(&[], "{n}.txt", None);
        assert!(previews.is_empty());
    }

    #[test]
    fn preview_counter_is_relative_to_filtered_subset() {
        let entries = vec![
            file("C:\\dir\\a.jpg"),
            file("C:\\dir\\b.txt"), // excluded by filter
            file("C:\\dir\\c.jpg"),
        ];
        let previews = preview_batch_rename(&entries, "{n}", Some("jpg"));
        assert_eq!(previews.len(), 2);
        assert_eq!(previews[0].new_name, "1");
        assert_eq!(previews[1].new_name, "2");
    }

    #[test]
    fn preview_reserved_name_flags_error() {
        let entries = vec![file("C:\\dir\\a.txt")];
        // Pattern produces "CON" which is reserved on Windows
        let previews = preview_batch_rename(&entries, "CON", None);
        assert!(previews[0].error.is_some());
    }
}
