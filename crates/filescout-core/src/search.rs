//! Recursive fuzzy file search.

use std::path::{Path, PathBuf};

use fuzzy_matcher::{skim::SkimMatcherV2, FuzzyMatcher};
use walkdir::WalkDir;

/// A single file-system entry that matched a search query.
#[derive(Debug, Clone)]
pub struct SearchResult {
    /// Absolute path to the matching entry.
    pub path: PathBuf,
    /// Fuzzy match score (higher = better).
    pub score: i64,
}

/// Search `root` recursively for entries whose file names fuzzy-match `query`.
///
/// - Empty `query` → returns `Vec::new()` immediately.
/// - `ext_filter` restricts results to a given extension (`"rs"` or `".rs"`).
///   Pass `None` to include every entry type.
/// - Results are sorted by descending score (best match first) and capped at `max_results`.
pub fn search_files(
    root: &Path,
    query: &str,
    ext_filter: Option<&str>,
    max_results: usize,
) -> Vec<SearchResult> {
    if query.is_empty() {
        return Vec::new();
    }

    // Normalise extension: strip leading dot and lower-case.
    let ext = ext_filter.map(|e| e.trim_start_matches('.').to_lowercase());

    let matcher = SkimMatcherV2::default();
    let mut results: Vec<SearchResult> = WalkDir::new(root)
        .follow_links(false)
        .into_iter()
        .filter_map(|e| e.ok())
        .filter(|entry| {
            if let Some(ref ext_str) = ext {
                let entry_ext = entry
                    .path()
                    .extension()
                    .map(|e| e.to_string_lossy().to_lowercase())
                    .unwrap_or_default();
                entry_ext == *ext_str
            } else {
                true
            }
        })
        .filter_map(|entry| {
            let name = entry.file_name().to_string_lossy();
            matcher.fuzzy_match(&name, query).map(|score| SearchResult {
                path: entry.path().to_path_buf(),
                score,
            })
        })
        .collect();

    results.sort_by(|a, b| b.score.cmp(&a.score));
    results.truncate(max_results);
    results
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    fn make_temp_tree() -> tempfile::TempDir {
        let dir = tempfile::tempdir().unwrap();
        fs::write(dir.path().join("hello.rs"), "fn main() {}").unwrap();
        fs::write(dir.path().join("world.txt"), "hello world").unwrap();
        let sub = dir.path().join("subdir");
        fs::create_dir(&sub).unwrap();
        fs::write(sub.join("nested.rs"), "// nested").unwrap();
        fs::write(sub.join("readme.md"), "# docs").unwrap();
        dir
    }

    #[test]
    fn empty_query_returns_empty() {
        let dir = make_temp_tree();
        let results = search_files(dir.path(), "", None, 100);
        assert!(results.is_empty());
    }

    #[test]
    fn query_finds_matching_files() {
        let dir = make_temp_tree();
        let results = search_files(dir.path(), "hello", None, 100);
        assert!(results.iter().any(|r| r.path.file_name().unwrap() == "hello.rs"));
    }

    #[test]
    fn recursive_search_finds_nested_files() {
        let dir = make_temp_tree();
        let results = search_files(dir.path(), "nested", None, 100);
        assert!(results.iter().any(|r| r.path.file_name().unwrap() == "nested.rs"));
    }

    #[test]
    fn extension_filter_restricts_results() {
        let dir = make_temp_tree();
        let results = search_files(dir.path(), "e", Some("rs"), 100);
        assert!(!results.is_empty());
        assert!(results
            .iter()
            .all(|r| r.path.extension().map(|e| e == "rs").unwrap_or(false)));
    }

    #[test]
    fn extension_filter_with_dot_prefix_matches_without() {
        let dir = make_temp_tree();
        let with_dot = search_files(dir.path(), "e", Some(".rs"), 100);
        let without_dot = search_files(dir.path(), "e", Some("rs"), 100);
        assert_eq!(with_dot.len(), without_dot.len());
    }

    #[test]
    fn results_sorted_by_score_descending() {
        let dir = make_temp_tree();
        let results = search_files(dir.path(), "hello", None, 100);
        for i in 1..results.len() {
            assert!(
                results[i - 1].score >= results[i].score,
                "results must be sorted descending"
            );
        }
    }

    #[test]
    fn max_results_caps_output() {
        let dir = make_temp_tree();
        let results = search_files(dir.path(), "e", None, 2);
        assert!(results.len() <= 2);
    }

    #[test]
    fn no_match_returns_empty() {
        let dir = make_temp_tree();
        let results = search_files(dir.path(), "zzz_no_match_xyz", None, 100);
        assert!(results.is_empty());
    }
}
