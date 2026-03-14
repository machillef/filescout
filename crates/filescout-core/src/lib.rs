//! FileScout Core — file system operations, models, and business logic.

pub mod fs;
pub mod models;
pub mod navigation;

pub use models::{FileEntry, SortColumn, SortDirection};
pub use navigation::History;
