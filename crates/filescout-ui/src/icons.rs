//! File type icon mapping — returns a text icon string based on file extension.

use filescout_core::FileEntry;

/// Icon categories for file types.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum IconCategory {
    Folder,
    Code,
    Markup,
    Config,
    Script,
    Image,
    Archive,
    Executable,
    Library,
    Audio,
    Video,
    Pdf,
    Spreadsheet,
    Presentation,
    WordDoc,
    TextLog,
    Database,
    Font,
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
        // Code (programming languages)
        "rs" | "py" | "js" | "ts" | "tsx" | "jsx" | "c" | "cpp" | "h" | "hpp" | "cs" | "java"
        | "go" | "rb" | "php" | "swift" | "kt" | "scala" | "lua" | "r" | "pl" | "zig" | "v"
        | "sv" | "vhd" | "asm" | "wasm" => IconCategory::Code,
        // Markup & templates
        "html" | "htm" | "css" | "scss" | "sass" | "xml" | "svg" | "xsl" | "xslt" => {
            IconCategory::Markup
        }
        // Config & data files
        "json" | "yaml" | "yml" | "toml" | "ini" | "cfg" | "conf" | "env" | "properties"
        | "lock" => IconCategory::Config,
        // Scripts & shell
        "sh" | "ps1" | "psm1" | "bat" | "cmd" | "zsh" | "fish" | "bash" => IconCategory::Script,
        // Markdown & documentation
        "md" | "rst" | "tex" | "adoc" => IconCategory::Markup,
        // SQL / database
        "sql" | "db" | "sqlite" | "sqlite3" | "mdb" => IconCategory::Database,
        // Image
        "png" | "jpg" | "jpeg" | "gif" | "bmp" | "ico" | "webp" | "tiff" | "tif" | "psd"
        | "raw" | "heic" | "avif" => IconCategory::Image,
        // Archive
        "zip" | "tar" | "gz" | "bz2" | "xz" | "7z" | "rar" | "zst" | "lz4" | "cab" | "iso"
        | "dmg" => IconCategory::Archive,
        // Executable
        "exe" | "msi" | "com" | "scr" | "appimage" => IconCategory::Executable,
        // Libraries / system
        "dll" | "sys" | "so" | "dylib" | "lib" | "a" | "o" => IconCategory::Library,
        // Audio
        "mp3" | "wav" | "flac" | "aac" | "ogg" | "wma" | "m4a" | "opus" | "mid" | "midi" => {
            IconCategory::Audio
        }
        // Video
        "mp4" | "avi" | "mkv" | "mov" | "wmv" | "flv" | "webm" | "m4v" | "mpg" | "mpeg" => {
            IconCategory::Video
        }
        // PDF
        "pdf" => IconCategory::Pdf,
        // Spreadsheets
        "xls" | "xlsx" | "ods" | "csv" => IconCategory::Spreadsheet,
        // Presentations
        "ppt" | "pptx" | "odp" => IconCategory::Presentation,
        // Word documents
        "doc" | "docx" | "odt" | "rtf" => IconCategory::WordDoc,
        // Text & logs
        "txt" | "log" | "out" | "err" => IconCategory::TextLog,
        // Fonts
        "ttf" | "otf" | "woff" | "woff2" | "eot" => IconCategory::Font,
        _ => IconCategory::Generic,
    }
}

/// Return a text icon string for the given entry.
pub fn file_icon_text(entry: &FileEntry) -> &'static str {
    match icon_category(entry) {
        IconCategory::Folder => "\u{1F4C1}",        // 📁
        IconCategory::Code => "\u{1F4BB}",           // 💻
        IconCategory::Markup => "\u{1F310}",         // 🌐
        IconCategory::Config => "\u{2699}\u{FE0F}",  // ⚙️
        IconCategory::Script => "\u{1F4DC}",         // 📜
        IconCategory::Image => "\u{1F5BC}\u{FE0F}",  // 🖼️
        IconCategory::Archive => "\u{1F4E6}",        // 📦
        IconCategory::Executable => "\u{25B6}\u{FE0F}", // ▶️
        IconCategory::Library => "\u{1F9E9}",        // 🧩
        IconCategory::Audio => "\u{1F3B5}",          // 🎵
        IconCategory::Video => "\u{1F3AC}",          // 🎬
        IconCategory::Pdf => "\u{1F4D5}",            // 📕
        IconCategory::Spreadsheet => "\u{1F4CA}",    // 📊
        IconCategory::Presentation => "\u{1F4FD}\u{FE0F}", // 📽️
        IconCategory::WordDoc => "\u{1F4C3}",        // 📃
        IconCategory::TextLog => "\u{1F4C4}",        // 📄
        IconCategory::Database => "\u{1F5C3}\u{FE0F}", // 🗃️
        IconCategory::Font => "\u{1F524}",           // 🔤
        IconCategory::Generic => "\u{1F4CB}",        // 📋
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
    fn pdf_is_distinct() {
        assert_eq!(icon_category(&make_entry("report.pdf", false)), IconCategory::Pdf);
    }

    #[test]
    fn log_is_text() {
        assert_eq!(icon_category(&make_entry("debug.log", false)), IconCategory::TextLog);
    }

    #[test]
    fn csv_is_spreadsheet() {
        assert_eq!(icon_category(&make_entry("data.csv", false)), IconCategory::Spreadsheet);
    }

    #[test]
    fn docx_is_word_doc() {
        assert_eq!(icon_category(&make_entry("thesis.docx", false)), IconCategory::WordDoc);
    }

    #[test]
    fn json_is_config() {
        assert_eq!(icon_category(&make_entry("package.json", false)), IconCategory::Config);
    }

    #[test]
    fn sh_is_script() {
        assert_eq!(icon_category(&make_entry("deploy.sh", false)), IconCategory::Script);
    }

    #[test]
    fn dll_is_library() {
        assert_eq!(icon_category(&make_entry("kernel32.dll", false)), IconCategory::Library);
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

    #[test]
    fn sql_is_database() {
        assert_eq!(icon_category(&make_entry("schema.sql", false)), IconCategory::Database);
    }

    #[test]
    fn ttf_is_font() {
        assert_eq!(icon_category(&make_entry("arial.ttf", false)), IconCategory::Font);
    }

    #[test]
    fn html_is_markup() {
        assert_eq!(icon_category(&make_entry("index.html", false)), IconCategory::Markup);
    }

    #[test]
    fn pptx_is_presentation() {
        assert_eq!(
            icon_category(&make_entry("slides.pptx", false)),
            IconCategory::Presentation
        );
    }
}
