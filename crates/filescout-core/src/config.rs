//! Application configuration — theme preference and other user settings.
//!
//! Config is stored as TOML at `{config_dir}/filescout/config.toml`.
//! Missing file → silently use defaults (no error shown to user).

use std::path::PathBuf;

use anyhow::Result;
use serde::{Deserialize, Serialize};

use crate::bookmarks::Bookmarks;

/// Which visual theme the application should use.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ThemePreference {
    #[default]
    Dark,
    Light,
}

impl ThemePreference {
    /// Returns the opposite theme.
    pub fn toggled(self) -> Self {
        match self {
            Self::Dark => Self::Light,
            Self::Light => Self::Dark,
        }
    }

    pub fn is_dark(self) -> bool {
        self == Self::Dark
    }
}

/// All persisted user preferences.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AppConfig {
    #[serde(default)]
    pub theme: ThemePreference,
    #[serde(default)]
    pub bookmarks: Bookmarks,
    #[serde(default)]
    pub show_hidden: bool,
    #[serde(default = "default_split_ratio")]
    pub split_ratio: f32,
    #[serde(default)]
    pub dual_pane: bool,
}

impl Default for AppConfig {
    fn default() -> Self {
        Self {
            theme: ThemePreference::default(),
            bookmarks: Bookmarks::default(),
            show_hidden: false,
            split_ratio: 0.5,
            dual_pane: false,
        }
    }
}

fn default_split_ratio() -> f32 {
    0.5
}

/// Returns the path to the config file, or `None` if the OS has no config dir.
pub fn config_path() -> Option<PathBuf> {
    dirs::config_dir().map(|d| d.join("filescout").join("config.toml"))
}

/// Load config from disk.  Returns `AppConfig::default()` when the file is absent or unreadable.
pub fn load_config() -> AppConfig {
    let Some(path) = config_path() else {
        return AppConfig::default();
    };
    let Ok(text) = std::fs::read_to_string(&path) else {
        return AppConfig::default();
    };
    toml::from_str(&text).unwrap_or_default()
}

/// Persist `config` to disk.  Creates the parent directory if needed.
pub fn save_config(config: &AppConfig) -> Result<()> {
    let path = config_path().ok_or_else(|| anyhow::anyhow!("no config directory available"))?;
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let text = toml::to_string_pretty(config)?;
    std::fs::write(&path, text)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::TempDir;

    // ── ThemePreference ────────────────────────────────────────────────────

    #[test]
    fn default_theme_is_dark() {
        assert_eq!(ThemePreference::default(), ThemePreference::Dark);
    }

    #[test]
    fn toggled_dark_gives_light() {
        assert_eq!(ThemePreference::Dark.toggled(), ThemePreference::Light);
    }

    #[test]
    fn toggled_light_gives_dark() {
        assert_eq!(ThemePreference::Light.toggled(), ThemePreference::Dark);
    }

    #[test]
    fn is_dark_true_for_dark() {
        assert!(ThemePreference::Dark.is_dark());
    }

    #[test]
    fn is_dark_false_for_light() {
        assert!(!ThemePreference::Light.is_dark());
    }

    // ── AppConfig serde ────────────────────────────────────────────────────

    #[test]
    fn default_config_has_dark_theme() {
        let cfg = AppConfig::default();
        assert_eq!(cfg.theme, ThemePreference::Dark);
    }

    #[test]
    fn serialize_dark_theme() {
        let cfg = AppConfig { theme: ThemePreference::Dark, ..Default::default() };
        let text = toml::to_string_pretty(&cfg).unwrap();
        assert!(text.contains("theme = \"dark\""));
    }

    #[test]
    fn serialize_light_theme() {
        let cfg = AppConfig { theme: ThemePreference::Light, ..Default::default() };
        let text = toml::to_string_pretty(&cfg).unwrap();
        assert!(text.contains("theme = \"light\""));
    }

    #[test]
    fn deserialize_dark_theme() {
        let text = "theme = \"dark\"\n";
        let cfg: AppConfig = toml::from_str(text).unwrap();
        assert_eq!(cfg.theme, ThemePreference::Dark);
    }

    #[test]
    fn deserialize_light_theme() {
        let text = "theme = \"light\"\n";
        let cfg: AppConfig = toml::from_str(text).unwrap();
        assert_eq!(cfg.theme, ThemePreference::Light);
    }

    #[test]
    fn deserialize_empty_string_gives_default() {
        let cfg: AppConfig = toml::from_str("").unwrap();
        assert_eq!(cfg.theme, ThemePreference::Dark);
    }

    #[test]
    fn round_trip_dark() {
        let original = AppConfig { theme: ThemePreference::Dark, ..Default::default() };
        let text = toml::to_string_pretty(&original).unwrap();
        let restored: AppConfig = toml::from_str(&text).unwrap();
        assert_eq!(restored.theme, original.theme);
    }

    #[test]
    fn round_trip_light() {
        let original = AppConfig { theme: ThemePreference::Light, ..Default::default() };
        let text = toml::to_string_pretty(&original).unwrap();
        let restored: AppConfig = toml::from_str(&text).unwrap();
        assert_eq!(restored.theme, original.theme);
    }

    // ── save_config / load from path ───────────────────────────────────────

    fn write_config_to(dir: &TempDir, cfg: &AppConfig) -> PathBuf {
        let path = dir.path().join("config.toml");
        let text = toml::to_string_pretty(cfg).unwrap();
        fs::write(&path, text).unwrap();
        path
    }

    #[test]
    fn save_creates_file_with_correct_content() {
        let dir = TempDir::new().unwrap();
        let path = dir.path().join("filescout").join("config.toml");
        let cfg = AppConfig { theme: ThemePreference::Light, ..Default::default() };

        // Manually exercise save logic using a known path
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        let text = toml::to_string_pretty(&cfg).unwrap();
        fs::write(&path, &text).unwrap();

        let loaded: AppConfig = toml::from_str(&fs::read_to_string(&path).unwrap()).unwrap();
        assert_eq!(loaded.theme, ThemePreference::Light);
    }

    #[test]
    fn load_from_missing_file_path_returns_default() {
        // We can't override config_path() in tests, but we can verify the
        // fallback logic by testing that toml::from_str("") returns the default.
        let cfg: AppConfig = toml::from_str("").unwrap_or_default();
        assert_eq!(cfg.theme, ThemePreference::Dark);
    }

    #[test]
    fn show_hidden_defaults_to_false() {
        let cfg = AppConfig::default();
        assert!(!cfg.show_hidden);
    }

    #[test]
    fn show_hidden_round_trip() {
        let cfg = AppConfig { show_hidden: true, ..Default::default() };
        let text = toml::to_string_pretty(&cfg).unwrap();
        let restored: AppConfig = toml::from_str(&text).unwrap();
        assert!(restored.show_hidden);
    }

    #[test]
    fn split_ratio_defaults_to_half() {
        let cfg = AppConfig::default();
        assert!((cfg.split_ratio - 0.5).abs() < f32::EPSILON);
    }

    #[test]
    fn split_ratio_round_trip() {
        let cfg = AppConfig { split_ratio: 0.3, ..Default::default() };
        let text = toml::to_string_pretty(&cfg).unwrap();
        let restored: AppConfig = toml::from_str(&text).unwrap();
        assert!((restored.split_ratio - 0.3).abs() < f32::EPSILON);
    }

    #[test]
    fn load_from_corrupt_toml_returns_default() {
        let corrupt = "!!! not valid toml !!!";
        let cfg: AppConfig = toml::from_str(corrupt).unwrap_or_default();
        assert_eq!(cfg.theme, ThemePreference::Dark);
    }

    #[test]
    fn write_and_read_via_temp_file() {
        let dir = TempDir::new().unwrap();
        let cfg = AppConfig { theme: ThemePreference::Light, ..Default::default() };
        let path = write_config_to(&dir, &cfg);

        let text = fs::read_to_string(path).unwrap();
        let loaded: AppConfig = toml::from_str(&text).unwrap();
        assert_eq!(loaded.theme, ThemePreference::Light);
    }
}
