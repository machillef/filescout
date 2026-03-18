//! Batch rename overlay — Ctrl+R.
//!
//! Opens over the active pane and lets the user define a rename pattern with live preview.

use std::path::PathBuf;

use filescout_core::FileEntry;
use filescout_core::batch_rename::{BatchRenamePreview, preview_batch_rename};
use iced::widget::{button, column, container, row, scrollable, text, text_input};
use iced::{Background, Color, Element, Length, Theme};

/// Messages emitted by the batch rename overlay.
#[derive(Debug, Clone)]
pub enum BatchRenameMessage {
    PatternChanged(String),
    ExtFilterChanged(String),
    Apply,
    Close,
}

/// Live state for the batch rename overlay.
pub struct BatchRenameState {
    /// Pattern string entered by the user.
    pub pattern: String,
    /// Optional extension filter (empty = all files).
    pub ext_filter: String,
    /// Snapshot of the directory entries at the time the overlay was opened.
    entries: Vec<FileEntry>,
    /// Computed rename previews, updated whenever pattern or ext_filter changes.
    pub previews: Vec<BatchRenamePreview>,
}

impl BatchRenameState {
    pub fn new(entries: &[FileEntry]) -> Self {
        let mut state = Self {
            pattern: "{name}.{ext}".to_string(),
            ext_filter: String::new(),
            entries: entries.to_vec(),
            previews: Vec::new(),
        };
        state.refresh_previews();
        state
    }

    /// Update the rename pattern and recompute previews.
    pub fn set_pattern(&mut self, pattern: String) {
        self.pattern = pattern;
        self.refresh_previews();
    }

    /// Update the extension filter and recompute previews.
    pub fn set_ext_filter(&mut self, ext: String) {
        self.ext_filter = ext;
        self.refresh_previews();
    }

    /// Returns (old_path, new_name) pairs for previews that have no validation error.
    pub fn valid_renames(&self) -> Vec<(PathBuf, String)> {
        self.previews
            .iter()
            .filter(|p| p.error.is_none())
            .map(|p| (p.original.clone(), p.new_name.clone()))
            .collect()
    }

    pub fn valid_count(&self) -> usize {
        self.previews.iter().filter(|p| p.error.is_none()).count()
    }

    pub fn error_count(&self) -> usize {
        self.previews.iter().filter(|p| p.error.is_some()).count()
    }

    fn refresh_previews(&mut self) {
        let ext_filter = {
            let t = self.ext_filter.trim();
            if t.is_empty() { None } else { Some(t) }
        };
        self.previews = preview_batch_rename(&self.entries, &self.pattern, ext_filter);
    }
}

fn backdrop_style(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(Background::Color(Color::from_rgba(0.0, 0.0, 0.0, 0.7))),
        ..Default::default()
    }
}

fn error_text_style(_theme: &Theme) -> iced::widget::text::Style {
    iced::widget::text::Style {
        color: Some(Color::from_rgb(1.0, 0.45, 0.45)),
    }
}

fn muted_text_style(_theme: &Theme) -> iced::widget::text::Style {
    iced::widget::text::Style {
        color: Some(Color::from_rgb(0.6, 0.6, 0.6)),
    }
}

/// Renders the batch rename overlay as a full-screen dimmed backdrop.
pub fn batch_rename_view(state: &BatchRenameState) -> Element<'_, BatchRenameMessage> {
    let pattern_input = text_input("e.g. {name}_{n:03}.{ext}", &state.pattern)
        .on_input(BatchRenameMessage::PatternChanged)
        .padding(10)
        .size(15);

    let ext_input = text_input("ext filter", &state.ext_filter)
        .on_input(BatchRenameMessage::ExtFilterChanged)
        .padding(10)
        .size(14)
        .width(130);

    let inputs = row![pattern_input, ext_input].spacing(8);

    let help = text("Tokens:  {name}  {ext}  {n}  {n:03}")
        .size(11)
        .style(muted_text_style);

    // Column headers
    let col_header = row![
        container(text("Original Name").size(12))
            .width(Length::FillPortion(1))
            .padding([0, 4]),
        container(text("New Name").size(12))
            .width(Length::FillPortion(1))
            .padding([0, 4]),
    ]
    .spacing(4);

    // Preview rows
    let preview_rows: Vec<Element<'_, BatchRenameMessage>> =
        state.previews.iter().map(|p| preview_row(p)).collect();

    let list = scrollable(column(preview_rows).spacing(1).width(Length::Fill)).height(280);

    // Status text
    let status_msg = if state.previews.is_empty() {
        "No files match — check extension filter".to_string()
    } else if state.error_count() > 0 {
        format!(
            "{} file(s) will be renamed  •  {} skipped (invalid names)",
            state.valid_count(),
            state.error_count()
        )
    } else {
        format!("{} file(s) will be renamed", state.valid_count())
    };
    let status = text(status_msg).size(12);

    let can_apply = state.valid_count() > 0;

    let apply_btn: Element<'_, BatchRenameMessage> = if can_apply {
        button(text("Apply").size(14))
            .on_press(BatchRenameMessage::Apply)
            .style(button::primary)
            .padding([8, 20])
            .into()
    } else {
        button(text("Apply").size(14))
            .style(button::primary)
            .padding([8, 20])
            .into()
    };

    let cancel_btn = button(text("Cancel").size(14))
        .on_press(BatchRenameMessage::Close)
        .style(button::text)
        .padding([8, 20]);

    let footer = row![apply_btn, cancel_btn].spacing(8);

    let panel = container(
        column![
            text("Batch Rename").size(16),
            inputs,
            help,
            col_header,
            list,
            status,
            footer,
        ]
        .spacing(8),
    )
    .width(700)
    .padding(16)
    .style(container::rounded_box);

    container(panel)
        .center(Length::Fill)
        .style(backdrop_style)
        .into()
}

fn preview_row(p: &BatchRenamePreview) -> Element<'_, BatchRenameMessage> {
    let orig_name = p
        .original
        .file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_default();

    let new_name_widget: Element<'_, BatchRenameMessage> = if let Some(ref err) = p.error {
        text(format!("⚠ {err}"))
            .size(13)
            .style(error_text_style)
            .into()
    } else {
        text(&p.new_name).size(13).into()
    };

    row![
        container(text(orig_name).size(13))
            .width(Length::FillPortion(1))
            .padding([2, 4]),
        container(new_name_widget)
            .width(Length::FillPortion(1))
            .padding([2, 4]),
    ]
    .spacing(4)
    .into()
}
