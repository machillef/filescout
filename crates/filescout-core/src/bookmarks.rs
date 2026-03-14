//! Bookmarks — saved directory shortcuts the user can jump to.
//!
//! Bookmarks are persisted as part of `AppConfig` (serialized to TOML).
//! All mutation methods follow the immutable-update pattern: they return a
//! new `Bookmarks` value instead of modifying `self`.

use std::path::PathBuf;

use serde::{Deserialize, Serialize};

/// A single saved directory shortcut.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Bookmark {
    /// Display name shown in the UI.
    pub name: String,
    /// Absolute path to the bookmarked directory.
    pub path: PathBuf,
}

/// The ordered collection of bookmarks.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct Bookmarks {
    #[serde(default)]
    pub items: Vec<Bookmark>,
}

impl Bookmarks {
    /// Returns a new `Bookmarks` with the given entry appended.
    ///
    /// If `path` is already bookmarked the original is returned unchanged.
    pub fn add(&self, name: String, path: PathBuf) -> Self {
        if self.items.iter().any(|b| b.path == path) {
            return self.clone();
        }
        let mut items = self.items.clone();
        items.push(Bookmark { name, path });
        Self { items }
    }

    /// Returns a new `Bookmarks` with the entry at `index` removed.
    ///
    /// If `index` is out of bounds the original is returned unchanged.
    pub fn remove(&self, index: usize) -> Self {
        if index >= self.items.len() {
            return self.clone();
        }
        let mut items = self.items.clone();
        items.remove(index);
        Self { items }
    }

    /// Returns `true` if the given path is already bookmarked.
    pub fn contains_path(&self, path: &PathBuf) -> bool {
        self.items.iter().any(|b| &b.path == path)
    }

    /// Number of bookmarks.
    pub fn len(&self) -> usize {
        self.items.len()
    }

    /// Returns `true` when there are no bookmarks.
    pub fn is_empty(&self) -> bool {
        self.items.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn p(s: &str) -> PathBuf {
        PathBuf::from(s)
    }

    // ── add ────────────────────────────────────────────────────────────────

    #[test]
    fn add_single_bookmark() {
        let bm = Bookmarks::default().add("Home".into(), p("C:\\Users\\test"));
        assert_eq!(bm.len(), 1);
        assert_eq!(bm.items[0].name, "Home");
        assert_eq!(bm.items[0].path, p("C:\\Users\\test"));
    }

    #[test]
    fn add_two_distinct_bookmarks() {
        let bm = Bookmarks::default()
            .add("A".into(), p("C:\\a"))
            .add("B".into(), p("C:\\b"));
        assert_eq!(bm.len(), 2);
    }

    #[test]
    fn add_duplicate_path_is_noop() {
        let bm = Bookmarks::default()
            .add("A".into(), p("C:\\a"))
            .add("A2".into(), p("C:\\a")); // same path, different name
        assert_eq!(bm.len(), 1, "duplicate path should not be added");
    }

    #[test]
    fn add_does_not_mutate_original() {
        let original = Bookmarks::default();
        let _new = original.add("A".into(), p("C:\\a"));
        assert_eq!(original.len(), 0, "original must remain unchanged");
    }

    // ── remove ─────────────────────────────────────────────────────────────

    #[test]
    fn remove_only_item_leaves_empty() {
        let bm = Bookmarks::default().add("A".into(), p("C:\\a")).remove(0);
        assert!(bm.is_empty());
    }

    #[test]
    fn remove_first_of_two() {
        let bm = Bookmarks::default()
            .add("A".into(), p("C:\\a"))
            .add("B".into(), p("C:\\b"))
            .remove(0);
        assert_eq!(bm.len(), 1);
        assert_eq!(bm.items[0].name, "B");
    }

    #[test]
    fn remove_second_of_two() {
        let bm = Bookmarks::default()
            .add("A".into(), p("C:\\a"))
            .add("B".into(), p("C:\\b"))
            .remove(1);
        assert_eq!(bm.len(), 1);
        assert_eq!(bm.items[0].name, "A");
    }

    #[test]
    fn remove_out_of_bounds_is_noop() {
        let bm = Bookmarks::default().add("A".into(), p("C:\\a")).remove(5);
        assert_eq!(bm.len(), 1);
    }

    #[test]
    fn remove_does_not_mutate_original() {
        let original = Bookmarks::default().add("A".into(), p("C:\\a"));
        let _new = original.remove(0);
        assert_eq!(original.len(), 1, "original must remain unchanged");
    }

    // ── contains_path ──────────────────────────────────────────────────────

    #[test]
    fn contains_path_true_when_present() {
        let bm = Bookmarks::default().add("A".into(), p("C:\\a"));
        assert!(bm.contains_path(&p("C:\\a")));
    }

    #[test]
    fn contains_path_false_when_absent() {
        let bm = Bookmarks::default().add("A".into(), p("C:\\a"));
        assert!(!bm.contains_path(&p("C:\\b")));
    }

    // ── serde round-trip ───────────────────────────────────────────────────

    #[test]
    fn round_trip_via_toml() {
        let original = Bookmarks::default()
            .add("Docs".into(), p("C:\\Users\\test\\Documents"))
            .add("Code".into(), p("C:\\code"));
        let serialized = toml::to_string_pretty(&original).unwrap();
        let restored: Bookmarks = toml::from_str(&serialized).unwrap();
        assert_eq!(restored.items.len(), 2);
        assert_eq!(restored.items[0].name, "Docs");
        assert_eq!(restored.items[1].name, "Code");
    }

    #[test]
    fn empty_bookmarks_serializes_to_empty_array() {
        let bm = Bookmarks::default();
        let text = toml::to_string_pretty(&bm).unwrap();
        // Should have an `items` key with an empty array or just omit it
        let restored: Bookmarks = toml::from_str(&text).unwrap();
        assert!(restored.is_empty());
    }

    #[test]
    fn deserialize_missing_items_key_gives_empty() {
        let text = "";
        let bm: Bookmarks = toml::from_str(text).unwrap();
        assert!(bm.is_empty());
    }
}
