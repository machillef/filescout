use std::path::{Path, PathBuf};

use filescout_core::bookmarks::Bookmarks;
use filescout_ui::theme;
use iced::widget::{Space, button, column, container, row, text};
use iced::{Alignment, Element, Length};

/// Sidebar sections that can be independently collapsed.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SidebarSection {
    Places,
    Bookmarks,
    Drives,
}

/// Messages emitted by the sidebar component.
#[derive(Debug, Clone)]
#[allow(dead_code)]
pub enum SidebarMessage {
    /// Navigate the active pane to the given path.
    Navigate(PathBuf),
    /// Remove bookmark at the given index.
    RemoveBookmark(usize),
    /// Toggle a section's collapsed state.
    ToggleSection(SidebarSection),
    /// Toggle sidebar visibility (collapsed/expanded).
    ToggleCollapsed,
}

/// Sidebar UI state.
pub struct SidebarState {
    /// Whether the sidebar is fully collapsed (hidden).
    pub collapsed: bool,
    /// Whether the Places section is expanded.
    pub places_expanded: bool,
    /// Whether the Bookmarks section is expanded.
    pub bookmarks_expanded: bool,
    /// Whether the Drives section is expanded.
    pub drives_expanded: bool,
}

impl Default for SidebarState {
    fn default() -> Self {
        Self {
            collapsed: false,
            places_expanded: true,
            bookmarks_expanded: true,
            drives_expanded: true,
        }
    }
}

impl SidebarState {
    pub fn toggle_section(&mut self, section: SidebarSection) {
        match section {
            SidebarSection::Places => self.places_expanded = !self.places_expanded,
            SidebarSection::Bookmarks => self.bookmarks_expanded = !self.bookmarks_expanded,
            SidebarSection::Drives => self.drives_expanded = !self.drives_expanded,
        }
    }
}

/// Render the sidebar with Places, Bookmarks, and Drives sections.
pub fn sidebar_view(
    bookmarks: &Bookmarks,
    drives: &[PathBuf],
    current_path: &Path,
    state: &SidebarState,
) -> Element<'static, SidebarMessage> {
    let mut sections: Vec<Element<'static, SidebarMessage>> = Vec::new();

    // --- Places section ---
    sections.push(section_header(
        "Places",
        state.places_expanded,
        SidebarSection::Places,
    ));
    if state.places_expanded {
        let places: Vec<(&str, &str, Option<PathBuf>)> = vec![
            ("🏠", "Home", dirs::home_dir()),
            ("🖥️", "Desktop", dirs::desktop_dir()),
            ("📄", "Documents", dirs::document_dir()),
            ("⬇️", "Downloads", dirs::download_dir()),
        ];
        for (icon, label, path) in places {
            if let Some(target) = path {
                let is_active = current_path == target.as_path();
                sections.push(sidebar_item(icon, label, target, is_active));
            }
        }
        sections.push(Space::new().width(Length::Fill).height(8).into());
    }

    // --- Bookmarks section ---
    sections.push(section_header(
        "Bookmarks",
        state.bookmarks_expanded,
        SidebarSection::Bookmarks,
    ));
    if state.bookmarks_expanded {
        for (i, bm) in bookmarks.items.iter().enumerate() {
            let is_active = current_path == bm.path.as_path();
            sections.push(bookmark_item(&bm.name, bm.path.clone(), i, is_active));
        }
        if bookmarks.is_empty() {
            sections.push(
                text("  No bookmarks")
                    .size(11)
                    .color([0.5, 0.5, 0.5])
                    .into(),
            );
        }
        sections.push(Space::new().width(Length::Fill).height(8).into());
    }

    // --- Drives section ---
    sections.push(section_header(
        "Drives",
        state.drives_expanded,
        SidebarSection::Drives,
    ));
    if state.drives_expanded {
        for drive in drives {
            let label = drive
                .components()
                .next()
                .map(|c| c.as_os_str().to_string_lossy().into_owned())
                .unwrap_or_else(|| drive.display().to_string());
            let is_active = current_path.starts_with(drive);
            sections.push(sidebar_item("💾", &label, drive.clone(), is_active));
        }
    }

    container(
        column(sections)
            .spacing(1)
            .padding([8, 4])
            .width(Length::Fill),
    )
    .width(220)
    .height(Length::Fill)
    .style(theme::sidebar_style)
    .into()
}

fn section_header(
    label: &str,
    expanded: bool,
    section: SidebarSection,
) -> Element<'static, SidebarMessage> {
    let arrow = if expanded { "▼" } else { "▶" };
    let display = format!("{arrow} {label}");
    button(row![text(display).size(12)].align_y(Alignment::Center))
        .on_press(SidebarMessage::ToggleSection(section))
        .style(button::text)
        .width(Length::Fill)
        .padding([4, 4])
        .into()
}

fn sidebar_item(
    icon: &str,
    label: &str,
    target: PathBuf,
    is_active: bool,
) -> Element<'static, SidebarMessage> {
    let display = format!("{icon} {label}");
    let style = if is_active {
        button::primary
    } else {
        button::text
    };
    button(
        row![text(display).size(13)]
            .align_y(Alignment::Center)
            .padding([0, 8]),
    )
    .on_press(SidebarMessage::Navigate(target))
    .style(style)
    .width(Length::Fill)
    .padding([3, 4])
    .into()
}

fn bookmark_item(
    name: &str,
    path: PathBuf,
    _index: usize,
    is_active: bool,
) -> Element<'static, SidebarMessage> {
    let display = format!("⭐ {name}");
    let style = if is_active {
        button::primary
    } else {
        button::text
    };
    button(
        row![text(display).size(13)]
            .align_y(Alignment::Center)
            .padding([0, 8]),
    )
    .on_press(SidebarMessage::Navigate(path))
    .style(style)
    .width(Length::Fill)
    .padding([3, 4])
    .into()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sidebar_state_default_is_expanded() {
        let state = SidebarState::default();
        assert!(!state.collapsed);
        assert!(state.places_expanded);
        assert!(state.bookmarks_expanded);
        assert!(state.drives_expanded);
    }

    #[test]
    fn toggle_section_places() {
        let mut state = SidebarState::default();
        state.toggle_section(SidebarSection::Places);
        assert!(!state.places_expanded);
        state.toggle_section(SidebarSection::Places);
        assert!(state.places_expanded);
    }

    #[test]
    fn toggle_section_bookmarks() {
        let mut state = SidebarState::default();
        state.toggle_section(SidebarSection::Bookmarks);
        assert!(!state.bookmarks_expanded);
    }

    #[test]
    fn toggle_section_drives() {
        let mut state = SidebarState::default();
        state.toggle_section(SidebarSection::Drives);
        assert!(!state.drives_expanded);
    }
}
