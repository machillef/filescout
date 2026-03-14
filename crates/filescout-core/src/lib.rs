//! FileScout Core — file system operations, models, and business logic.

pub mod fs;
pub mod models;
pub mod navigation;
pub mod ops;

pub use models::{FileEntry, SortColumn, SortDirection};
pub use navigation::History;
pub use ops::{UndoAction, UndoStack};
