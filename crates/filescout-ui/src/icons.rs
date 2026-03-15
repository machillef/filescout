//! File type icon mapping — returns a text icon string based on file extension.

use filescout_core::FileEntry;

/// Icon categories for file types.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum IconCategory {
    Folder,
    Code,
    Image,
    Archive,
    Executable,
    Audio,
    Video,
    Document,
    Generic,
}

/// Determine the icon category for a file entry.
pub fn icon_category(entry: &FileEntry) -> IconCategory {
    if entry.is_dir {
        return IconCategory::Folder;
    }
    let ext = entry
        .name
        .rsplit('.')
        .next()
        .map(|e| e.to_lowercase())
        .unwrap_or_default();
    match ext.as_str() {
        // Code
        "rs" | "py" | "js" | "ts" | "tsx" | "jsx" | "c" | "cpp" | "h" | "hpp" | "cs" | "java"
        | "go" | "rb" | "php" | "swift" | "kt" | "scala" | "lua" | "sh" | "ps1" | "psm1"
        | "bat" | "cmd" | "html" | "htm" | "css" | "scss" | "sass" | "json" | "xml" | "yaml"
        | "yml" | "toml" | "md" | "sql" | "r" | "pl" | "zig" | "v" | "sv" | "vhd" | "asm"
        | "wasm" => IconCategory::Code,
        // Image
        "png" | "jpg" | "jpeg" | "gif" | "bmp" | "svg" | "ico" | "webp" | "tiff" | "tif"
        | "psd" | "raw" | "heic" | "avif" => IconCategory::Image,
        // Archive
        "zip" | "tar" | "gz" | "bz2" | "xz" | "7z" | "rar" | "zst" | "lz4" | "cab" | "iso"
        | "dmg" => IconCategory::Archive,
        // Executable
        "exe" | "msi" | "dll" | "sys" | "com" | "scr" => IconCategory::Executable,
        // Audio
        "mp3" | "wav" | "flac" | "aac" | "ogg" | "wma" | "m4a" | "opus" => IconCategory::Audio,
        // Video
        "mp4" | "avi" | "mkv" | "mov" | "wmv" | "flv" | "webm" | "m4v" | "mpg" | "mpeg" => {
            IconCategory::Video
        }
        // Document
        "pdf" | "doc" | "docx" | "xls" | "xlsx" | "ppt" | "pptx" | "odt" | "ods" | "odp"
        | "txt" | "rtf" | "csv" | "log" => IconCategory::Document,
        _ => IconCategory::Generic,
    }
}

/// Return a text icon string for the given entry.
pub fn file_icon_text(entry: &FileEntry) -> &'static str {
    match icon_category(entry) {
        IconCategory::Folder => "\u{1F4C1}",     // 📁
        IconCategory::Code => "\u{1F4DD}",        // 📝
        IconCategory::Image => "\u{1F5BC}",       // 🖼
        IconCategory::Archive => "\u{1F4E6}",     // 📦
        IconCategory::Executable => "\u{2699}",   // ⚙
        IconCategory::Audio => "\u{1F3B5}",       // 🎵
        IconCategory::Video => "\u{1F3AC}",       // 🎬
        IconCategory::Document => "\u{1F4C4}",    // 📄
        IconCategory::Generic => "\u{1F4CB}",     // 📋
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    fn make_entry(name: &str, is_dir: bool) -> FileEntry {
        FileEntry {
            name: name.to_string(),
            path: PathBuf::from(name),
            is_dir,
            size: 0,
            modified: None,
            is_hidden: false,
        }
    }

    #[test]
    fn directory_is_folder() {
        assert_eq!(icon_category(&make_entry("src", true)), IconCategory::Folder);
    }

    #[test]
    fn rs_is_code() {
        assert_eq!(icon_category(&make_entry("main.rs", false)), IconCategory::Code);
    }

    #[test]
    fn zip_is_archive() {
        assert_eq!(icon_category(&make_entry("data.zip", false)), IconCategory::Archive);
    }

    #[test]
    fn png_is_image() {
        assert_eq!(icon_category(&make_entry("photo.png", false)), IconCategory::Image);
    }

    #[test]
    fn exe_is_executable() {
        assert_eq!(icon_category(&make_entry("app.exe", false)), IconCategory::Executable);
    }

    #[test]
    fn mp3_is_audio() {
        assert_eq!(icon_category(&make_entry("song.mp3", false)), IconCategory::Audio);
    }

    #[test]
    fn mp4_is_video() {
        assert_eq!(icon_category(&make_entry("movie.mp4", false)), IconCategory::Video);
    }

    #[test]
    fn pdf_is_document() {
        assert_eq!(icon_category(&make_entry("report.pdf", false)), IconCategory::Document);
    }

    #[test]
    fn unknown_is_generic() {
        assert_eq!(icon_category(&make_entry("data.xyz", false)), IconCategory::Generic);
    }

    #[test]
    fn case_insensitive() {
        assert_eq!(icon_category(&make_entry("IMAGE.PNG", false)), IconCategory::Image);
    }

    #[test]
    fn no_extension_is_generic() {
        assert_eq!(icon_category(&make_entry("Makefile", false)), IconCategory::Generic);
    }
}
