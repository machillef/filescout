use std::path::PathBuf;

use chrono::{DateTime, Local};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FileEntry {
    pub name: String,
    pub path: PathBuf,
    pub is_dir: bool,
    pub size: u64,
    pub modified: Option<DateTime<Local>>,
    /// Whether the file has the Windows hidden or system attribute.
    #[serde(default)]
    pub is_hidden: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub enum SortColumn {
    #[default]
    Name,
    Size,
    Modified,
}

/// How to display file entries in the list.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum ViewMode {
    /// Compact list with Name, Size, Modified columns.
    #[default]
    List,
    /// Detailed view with Extension and Type columns added.
    Details,
}

#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub enum SortDirection {
    #[default]
    Asc,
    Desc,
}
