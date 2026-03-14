//! FileScout Core — file system operations, models, and business logic.

pub mod batch_rename;
pub mod config;
pub mod fs;
pub mod models;
pub mod navigation;
pub mod ops;
pub mod preview;
pub mod search;

pub use models::{FileEntry, SortColumn, SortDirection};
pub use navigation::History;
pub use ops::{copy_item, move_item, UndoAction, UndoStack};
pub use search::SearchResult;
