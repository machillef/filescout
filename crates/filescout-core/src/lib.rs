//! FileScout Core — file system operations, models, and business logic.

pub mod archive;
pub mod batch_rename;
pub mod bookmarks;
pub mod config;
pub mod fs;
pub mod models;
pub mod navigation;
pub mod ops;
pub mod preview;
pub mod properties;
pub mod search;

pub use bookmarks::{Bookmark, Bookmarks};
pub use models::{FileEntry, SortColumn, SortDirection, ViewMode};
pub use navigation::History;
pub use ops::{copy_item, move_item, UndoAction, UndoStack};
pub use search::SearchResult;
