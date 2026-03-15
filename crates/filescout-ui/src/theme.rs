//! Centralized theme module for FileScout.
//!
//! Provides color palette constants and reusable style functions so that
//! all view code uses consistent visual language instead of scattered
//! inline styles.

use iced::{Background, Border, Color, Theme};
use iced::widget::{button, container};

/// Color palette for the dark theme.
pub mod dark {
    use super::Color;

    pub const SURFACE: Color = Color { r: 0.15, g: 0.15, b: 0.17, a: 1.0 };
    pub const SURFACE_HOVER: Color = Color { r: 0.20, g: 0.20, b: 0.22, a: 1.0 };
    pub const SURFACE_RAISED: Color = Color { r: 0.18, g: 0.18, b: 0.20, a: 1.0 };
    pub const TEXT_PRIMARY: Color = Color { r: 0.90, g: 0.90, b: 0.92, a: 1.0 };
    pub const TEXT_SECONDARY: Color = Color { r: 0.55, g: 0.55, b: 0.58, a: 1.0 };
    pub const ACCENT: Color = Color { r: 0.30, g: 0.55, b: 0.90, a: 1.0 };
    pub const ACCENT_HOVER: Color = Color { r: 0.35, g: 0.60, b: 0.95, a: 1.0 };
    pub const BORDER: Color = Color { r: 0.25, g: 0.25, b: 0.28, a: 1.0 };
    pub const DIVIDER: Color = Color { r: 0.22, g: 0.22, b: 0.25, a: 1.0 };
    pub const ROW_ALT: Color = Color { r: 0.14, g: 0.14, b: 0.16, a: 1.0 };
    pub const SELECTION: Color = Color { r: 0.30, g: 0.55, b: 0.90, a: 0.3 };
}

/// Color palette for the light theme.
pub mod light {
    use super::Color;

    pub const SURFACE: Color = Color { r: 0.97, g: 0.97, b: 0.98, a: 1.0 };
    pub const SURFACE_HOVER: Color = Color { r: 0.92, g: 0.92, b: 0.94, a: 1.0 };
    pub const SURFACE_RAISED: Color = Color { r: 1.0, g: 1.0, b: 1.0, a: 1.0 };
    pub const TEXT_PRIMARY: Color = Color { r: 0.12, g: 0.12, b: 0.14, a: 1.0 };
    pub const TEXT_SECONDARY: Color = Color { r: 0.45, g: 0.45, b: 0.48, a: 1.0 };
    pub const ACCENT: Color = Color { r: 0.20, g: 0.45, b: 0.80, a: 1.0 };
    pub const ACCENT_HOVER: Color = Color { r: 0.25, g: 0.50, b: 0.85, a: 1.0 };
    pub const BORDER: Color = Color { r: 0.82, g: 0.82, b: 0.85, a: 1.0 };
    pub const DIVIDER: Color = Color { r: 0.88, g: 0.88, b: 0.90, a: 1.0 };
    pub const ROW_ALT: Color = Color { r: 0.95, g: 0.95, b: 0.96, a: 1.0 };
    pub const SELECTION: Color = Color { r: 0.20, g: 0.45, b: 0.80, a: 0.2 };
}

/// Returns whether the given theme is dark.
pub fn is_dark_theme(theme: &Theme) -> bool {
    matches!(
        theme,
        Theme::Dark
            | Theme::Dracula
            | Theme::Nord
            | Theme::SolarizedDark
            | Theme::GruvboxDark
            | Theme::CatppuccinMocha
            | Theme::CatppuccinMacchiato
            | Theme::CatppuccinFrappe
            | Theme::TokyoNight
            | Theme::TokyoNightStorm
            | Theme::KanagawaWave
            | Theme::KanagawaDragon
            | Theme::Moonfly
            | Theme::Nightfly
            | Theme::Oxocarbon
    )
}

// --- Reusable style functions ---

/// Style for the thin divider between sidebar/panes.
pub fn divider_style(theme: &Theme) -> container::Style {
    let color = if is_dark_theme(theme) { dark::DIVIDER } else { light::DIVIDER };
    container::Style {
        background: Some(Background::Color(color)),
        ..Default::default()
    }
}

/// Style for a file list row (normal, not selected).
pub fn file_row_style(theme: &Theme) -> button::Style {
    let palette = if is_dark_theme(theme) { dark::TEXT_PRIMARY } else { light::TEXT_PRIMARY };
    button::Style {
        text_color: palette,
        ..Default::default()
    }
}

/// Style for a selected file list row.
pub fn file_row_selected_style(theme: &Theme) -> button::Style {
    let (accent, text) = if is_dark_theme(theme) {
        (dark::SELECTION, dark::TEXT_PRIMARY)
    } else {
        (light::SELECTION, light::TEXT_PRIMARY)
    };
    button::Style {
        background: Some(Background::Color(accent)),
        text_color: text,
        border: Border {
            radius: 2.0.into(),
            ..Default::default()
        },
        ..Default::default()
    }
}

/// Style for alternating row background (zebra striping).
pub fn file_row_alt_style(theme: &Theme) -> container::Style {
    let bg = if is_dark_theme(theme) { dark::ROW_ALT } else { light::ROW_ALT };
    container::Style {
        background: Some(Background::Color(bg)),
        ..Default::default()
    }
}

/// Style for the file list header row.
pub fn file_header_style(theme: &Theme) -> container::Style {
    let (bg, border) = if is_dark_theme(theme) {
        (dark::SURFACE_RAISED, dark::BORDER)
    } else {
        (light::SURFACE_RAISED, light::BORDER)
    };
    container::Style {
        background: Some(Background::Color(bg)),
        border: Border {
            color: border,
            width: 0.0,
            radius: 0.0.into(),
        },
        ..Default::default()
    }
}

/// Semi-transparent backdrop for overlays (context menu, modals, etc.).
pub fn overlay_backdrop_style(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(Background::Color(Color { r: 0.0, g: 0.0, b: 0.0, a: 0.3 })),
        ..Default::default()
    }
}

/// Card/panel style for overlay content.
pub fn overlay_card_style(theme: &Theme) -> container::Style {
    let (bg, border) = if is_dark_theme(theme) {
        (dark::SURFACE_RAISED, dark::BORDER)
    } else {
        (light::SURFACE_RAISED, light::BORDER)
    };
    container::Style {
        background: Some(Background::Color(bg)),
        border: Border {
            color: border,
            width: 1.0,
            radius: 8.0.into(),
        },
        ..Default::default()
    }
}

/// Sidebar container style.
pub fn sidebar_style(theme: &Theme) -> container::Style {
    let bg = if is_dark_theme(theme) { dark::SURFACE } else { light::SURFACE };
    container::Style {
        background: Some(Background::Color(bg)),
        ..Default::default()
    }
}

/// Toolbar button style (subtle, no background until hover).
pub fn toolbar_button_style(theme: &Theme, _status: button::Status) -> button::Style {
    let text = if is_dark_theme(theme) { dark::TEXT_PRIMARY } else { light::TEXT_PRIMARY };
    button::Style {
        text_color: text,
        ..Default::default()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn dark_theme_detected() {
        assert!(is_dark_theme(&Theme::Dark));
    }

    #[test]
    fn light_theme_detected() {
        assert!(!is_dark_theme(&Theme::Light));
    }

    #[test]
    fn dark_colors_are_opaque() {
        assert_eq!(dark::SURFACE.a, 1.0);
        assert_eq!(dark::TEXT_PRIMARY.a, 1.0);
        assert_eq!(dark::ACCENT.a, 1.0);
    }

    #[test]
    fn light_colors_are_opaque() {
        assert_eq!(light::SURFACE.a, 1.0);
        assert_eq!(light::TEXT_PRIMARY.a, 1.0);
        assert_eq!(light::ACCENT.a, 1.0);
    }

    #[test]
    fn selection_colors_are_translucent() {
        assert!(dark::SELECTION.a < 1.0);
        assert!(light::SELECTION.a < 1.0);
    }
}
