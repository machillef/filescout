//! Properties overlay — displays file/directory metadata (Alt+Enter).
//!
//! Mirrors the command-palette pattern: full-screen dimmed backdrop + centered
//! card.  Directory sizes are computed asynchronously after the overlay opens.

use std::time::{SystemTime, UNIX_EPOCH};

use filescout_core::properties::{format_size, FileProperties};
use iced::widget::{button, column, container, row, text};
use iced::{Background, Color, Element, Length, Theme};

/// Messages emitted by the properties overlay.
#[derive(Debug, Clone)]
pub enum PropertiesMessage {
    /// Async directory-size computation completed.
    DirSizeComputed(u64),
    /// Total size of multi-selection computed.
    MultiSizeComputed(u64),
    /// Close the overlay.
    Close,
}

/// UI state held while the overlay is visible.
pub struct PropertiesState {
    /// The metadata snapshot shown in the overlay.
    pub props: FileProperties,
    /// Computed total size of a directory, populated asynchronously.
    pub dir_size: Option<u64>,
    /// When showing multi-selection properties, the count and total size.
    pub multi_info: Option<MultiSelectionInfo>,
}

/// Summary info for a multi-selection properties view.
pub struct MultiSelectionInfo {
    pub file_count: usize,
    pub dir_count: usize,
    pub total_size: Option<u64>,
}

impl PropertiesState {
    pub fn new(props: FileProperties) -> Self {
        Self { props, dir_size: None, multi_info: None }
    }

    pub fn new_multi(file_count: usize, dir_count: usize) -> Self {
        let total = file_count + dir_count;
        let name = format!("{total} items selected");
        let props = FileProperties {
            name,
            path: std::path::PathBuf::new(),
            is_dir: false,
            size_bytes: 0,
            created: None,
            modified: None,
            accessed: None,
            read_only: false,
            hidden: false,
            system: false,
            archive: false,
        };
        Self {
            props,
            dir_size: None,
            multi_info: Some(MultiSelectionInfo {
                file_count,
                dir_count,
                total_size: None,
            }),
        }
    }
}

// ── helpers ──────────────────────────────────────────────────────────────────

fn format_time(t: Option<SystemTime>) -> String {
    let Some(st) = t else {
        return "—".into();
    };
    match st.duration_since(UNIX_EPOCH) {
        Ok(d) => {
            // Convert Unix timestamp to a simple UTC string without extra deps.
            let secs = d.as_secs();
            // Days since epoch
            let days = secs / 86400;
            let rem = secs % 86400;
            let h = rem / 3600;
            let m = (rem % 3600) / 60;
            let s = rem % 60;

            // Gregorian calendar calculation (valid for years 1970–2099).
            let (year, month, day) = days_to_ymd(days);
            format!("{year:04}-{month:02}-{day:02}  {h:02}:{m:02}:{s:02} UTC")
        }
        Err(_) => "—".into(),
    }
}

/// Convert days since Unix epoch (1970-01-01) to (year, month, day).
/// Valid for 1970–2099 (no leap-second or >2100 support needed here).
fn days_to_ymd(days: u64) -> (u64, u64, u64) {
    // 400-year cycle constants
    const DAYS_400: u64 = 146097;
    const DAYS_100: u64 = 36524;
    const DAYS_1: u64 = 365;

    // Shift epoch from 1970-01-01 to 0000-03-01 for easier leap handling.
    let z = days + 719468;
    let era = z / DAYS_400;
    let doe = z % DAYS_400;
    let yoe = (doe - doe / 1460 + doe / DAYS_100 - doe / (DAYS_400 - 1)) / DAYS_1;
    let y = yoe + era * 400;
    let doy = doe - (DAYS_1 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = if m <= 2 { y + 1 } else { y };
    (y, m, d)
}

fn backdrop_style(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(Background::Color(Color::from_rgba(0.0, 0.0, 0.0, 0.7))),
        ..Default::default()
    }
}

/// A single label-value row inside the properties card.
fn prop_row<'a>(label: &'a str, value: String) -> Element<'a, PropertiesMessage> {
    row![
        text(label).size(12).width(110),
        text(value).size(13),
    ]
    .spacing(8)
    .into()
}

fn checkbox_label(checked: bool, label: &str) -> String {
    if checked {
        format!("[✓] {label}")
    } else {
        format!("[ ] {label}")
    }
}

/// Renders the properties overlay.
pub fn properties_view(state: &PropertiesState) -> Element<'_, PropertiesMessage> {
    let rows: Element<'_, PropertiesMessage> = if let Some(ref multi) = state.multi_info {
        // Multi-selection view: summary with total size.
        let total = multi.file_count + multi.dir_count;
        let contains = format!(
            "{} files, {} folders",
            multi.file_count, multi.dir_count,
        );
        let size_str = match multi.total_size {
            Some(n) => format!("{} ({n} bytes)", format_size(n)),
            None => "Computing…".into(),
        };
        column![
            prop_row("Selection:", format!("{total} items")),
            prop_row("Contains:", contains),
            prop_row("Total Size:", size_str),
        ]
        .spacing(6)
        .into()
    } else {
        // Single-item view.
        let p = &state.props;
        let kind = if p.is_dir { "Directory" } else { "File" };
        let size_str = if p.is_dir {
            match state.dir_size {
                Some(n) => format!("{} ({n} bytes)", format_size(n)),
                None => "Computing…".into(),
            }
        } else {
            format!("{} ({} bytes)", format_size(p.size_bytes), p.size_bytes)
        };
        let attrs_str = format!(
            "{}  {}  {}  {}",
            checkbox_label(p.read_only, "Read-Only"),
            checkbox_label(p.hidden, "Hidden"),
            checkbox_label(p.system, "System"),
            checkbox_label(p.archive, "Archive"),
        );
        column![
            prop_row("Name:", p.name.clone()),
            prop_row("Path:", p.path.display().to_string()),
            prop_row("Type:", kind.into()),
            prop_row("Size:", size_str),
            prop_row("Created:", format_time(p.created)),
            prop_row("Modified:", format_time(p.modified)),
            prop_row("Accessed:", format_time(p.accessed)),
            prop_row("Attributes:", attrs_str),
        ]
        .spacing(6)
        .into()
    };

    let header = row![
        text("Properties").size(15).width(Length::Fill),
        button(text("✕").size(12))
            .on_press(PropertiesMessage::Close)
            .style(button::text)
            .padding([2, 8]),
    ]
    .align_y(iced::alignment::Vertical::Center);

    let card = container(column![header, rows].spacing(12))
        .width(560)
        .padding(20)
        .style(container::rounded_box);

    container(card)
        .center(Length::Fill)
        .style(backdrop_style)
        .into()
}
