//! Native Windows file type icon extraction via `SHGetFileInfoW`.
//!
//! Extracts small (16×16) shell icons for file extensions and folders,
//! returning raw RGBA pixel data suitable for rendering in iced.

use std::mem;
use std::sync::Mutex;

use windows::core::PCWSTR;
use windows::Win32::Graphics::Gdi::{
    CreateCompatibleDC, DeleteDC, DeleteObject, GetDIBits, GetObjectW, BITMAP, BITMAPINFO,
    BITMAPINFOHEADER, DIB_RGB_COLORS,
};
use windows::Win32::Storage::FileSystem::FILE_ATTRIBUTE_NORMAL;
use windows::Win32::UI::Shell::{
    SHGetFileInfoW, SHFILEINFOW, SHGFI_FLAGS, SHGFI_ICON, SHGFI_SMALLICON,
    SHGFI_USEFILEATTRIBUTES,
};
use windows::Win32::System::Com::{CoInitializeEx, COINIT_APARTMENTTHREADED};
use windows::Win32::UI::WindowsAndMessaging::{DestroyIcon, GetIconInfo, HICON, ICONINFO};

/// Raw RGBA icon data extracted from the Windows shell.
#[derive(Debug, Clone)]
pub struct IconData {
    pub width: u32,
    pub height: u32,
    pub rgba: Vec<u8>,
}

/// Cache key for icon lookups.
pub const DIR_ICON_KEY: &str = "__dir__";

/// Global mutex to serialize `SHGetFileInfoW` calls — the function is not thread-safe.
static SHELL_ICON_LOCK: Mutex<()> = Mutex::new(());

/// Extract the shell icon for a file extension (e.g. "rs", "txt").
///
/// Uses `SHGFI_USEFILEATTRIBUTES` so the file doesn't need to exist.
/// Returns RGBA pixel data or `None` on failure.
pub fn get_icon_for_extension(extension: &str) -> Option<IconData> {
    ensure_com_initialized();
    let fake_name = if extension.is_empty() {
        "file".to_string()
    } else {
        format!("file.{extension}")
    };
    get_shell_icon(&fake_name, FILE_ATTRIBUTE_NORMAL)
}

/// Extract the shell icon for a directory.
pub fn get_folder_icon() -> Option<IconData> {
    ensure_com_initialized();
    let fake_name = "directory";
    get_shell_icon(
        fake_name,
        windows::Win32::Storage::FileSystem::FILE_ATTRIBUTE_DIRECTORY,
    )
}

/// Initialize COM on the current thread (STA).
///
/// `SHGetFileInfoW` with `SHGFI_ICON` requires COM. Call this once per
/// thread before any icon extraction. Safe to call multiple times — the
/// second call returns `S_FALSE` which we ignore.
fn ensure_com_initialized() {
    unsafe {
        let _ = CoInitializeEx(None, COINIT_APARTMENTTHREADED);
    }
}

/// Load icons for a batch of extensions (plus folder if needed).
///
/// Initializes COM on the calling thread automatically.
/// Returns `(key, IconData)` pairs. The key is the extension string
/// or `DIR_ICON_KEY` for the folder icon.
pub fn load_icons_batch(extensions: Vec<String>, include_folder: bool) -> Vec<(String, IconData)> {
    ensure_com_initialized();
    let mut results = Vec::new();

    if include_folder
        && let Some(data) = get_folder_icon()
    {
        results.push((DIR_ICON_KEY.to_string(), data));
    }

    for ext in extensions {
        if let Some(data) = get_icon_for_extension(&ext) {
            results.push((ext, data));
        }
    }

    results
}

fn get_shell_icon(
    name: &str,
    file_attributes: windows::Win32::Storage::FileSystem::FILE_FLAGS_AND_ATTRIBUTES,
) -> Option<IconData> {
    let _guard = SHELL_ICON_LOCK.lock().ok()?;
    let wide: Vec<u16> = name.encode_utf16().chain(std::iter::once(0)).collect();

    unsafe {
        let mut shfi = SHFILEINFOW::default();
        let flags: SHGFI_FLAGS = SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES;

        let result = SHGetFileInfoW(
            PCWSTR(wide.as_ptr()),
            file_attributes,
            Some(&mut shfi),
            mem::size_of::<SHFILEINFOW>() as u32,
            flags,
        );

        if result == 0 {
            return None;
        }

        let hicon = shfi.hIcon;
        if hicon.0.is_null() {
            return None;
        }

        let icon_data = hicon_to_rgba(hicon);
        let _ = DestroyIcon(hicon);
        icon_data
    }
}

/// Convert an HICON to RGBA pixel data.
///
/// # Safety
/// `hicon` must be a valid icon handle obtained from `SHGetFileInfoW`.
unsafe fn hicon_to_rgba(hicon: HICON) -> Option<IconData> {
    let mut icon_info = ICONINFO::default();
    if unsafe { GetIconInfo(hicon, &mut icon_info) }.is_err() {
        return None;
    }

    // Get bitmap dimensions from the color bitmap
    let mut bm = BITMAP::default();
    let obj_size = unsafe {
        GetObjectW(
            icon_info.hbmColor.into(),
            mem::size_of::<BITMAP>() as i32,
            Some(&mut bm as *mut BITMAP as *mut _),
        )
    };

    if obj_size == 0 {
        unsafe { cleanup_bitmaps(&icon_info) };
        return None;
    }

    let width = bm.bmWidth as u32;
    let height = bm.bmHeight as u32;

    if width == 0 || height == 0 {
        unsafe { cleanup_bitmaps(&icon_info) };
        return None;
    }

    // Create a memory DC for GetDIBits
    let hdc = unsafe { CreateCompatibleDC(None) };
    if hdc.is_invalid() {
        unsafe { cleanup_bitmaps(&icon_info) };
        return None;
    }

    // Setup BITMAPINFO for 32-bit top-down BGRA
    let mut bmi = BITMAPINFO::default();
    bmi.bmiHeader.biSize = mem::size_of::<BITMAPINFOHEADER>() as u32;
    bmi.bmiHeader.biWidth = width as i32;
    bmi.bmiHeader.biHeight = -(height as i32); // negative = top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = 0; // BI_RGB

    let pixel_count = (width * height * 4) as usize;
    let mut pixels = vec![0u8; pixel_count];

    let rows = unsafe {
        GetDIBits(
            hdc,
            icon_info.hbmColor,
            0,
            height,
            Some(pixels.as_mut_ptr() as *mut _),
            &mut bmi,
            DIB_RGB_COLORS,
        )
    };

    unsafe {
        let _ = DeleteDC(hdc);
        cleanup_bitmaps(&icon_info);
    }

    if rows == 0 {
        return None;
    }

    // Check if any pixel has non-zero alpha.
    // Legacy icons have all-zero alpha — treat as fully opaque.
    let has_alpha = pixels.chunks_exact(4).any(|px| px[3] != 0);

    // Convert BGRA → RGBA and fix alpha if needed
    for pixel in pixels.chunks_exact_mut(4) {
        pixel.swap(0, 2); // B↔R
        if !has_alpha {
            pixel[3] = 255;
        }
    }

    Some(IconData {
        width,
        height,
        rgba: pixels,
    })
}

/// # Safety
/// The bitmaps in `info` must be valid GDI handles or null.
unsafe fn cleanup_bitmaps(info: &ICONINFO) {
    if !info.hbmColor.is_invalid() {
        unsafe { let _ = DeleteObject(info.hbmColor.into()); }
    }
    if !info.hbmMask.is_invalid() {
        unsafe { let _ = DeleteObject(info.hbmMask.into()); }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn get_folder_icon_returns_some() {
        let icon = get_folder_icon();
        assert!(icon.is_some(), "should extract folder icon on Windows");
        let data = icon.unwrap();
        assert!(data.width > 0);
        assert!(data.height > 0);
        assert_eq!(data.rgba.len(), (data.width * data.height * 4) as usize);
    }

    #[test]
    fn get_icon_for_txt_returns_some() {
        let icon = get_icon_for_extension("txt");
        assert!(icon.is_some(), "should extract .txt icon on Windows");
    }

    #[test]
    fn get_icon_for_exe_returns_some() {
        let icon = get_icon_for_extension("exe");
        assert!(icon.is_some());
    }

    #[test]
    fn get_icon_for_empty_extension_returns_some() {
        let icon = get_icon_for_extension("");
        assert!(icon.is_some(), "should return generic file icon");
    }

    #[test]
    fn load_batch_returns_expected_keys() {
        let exts = vec!["rs".to_string(), "txt".to_string()];
        let results = load_icons_batch(exts, true);
        let keys: Vec<&str> = results.iter().map(|(k, _)| k.as_str()).collect();
        assert!(keys.contains(&DIR_ICON_KEY));
        assert!(keys.contains(&"rs"));
        assert!(keys.contains(&"txt"));
    }

    #[test]
    fn icon_data_is_valid_rgba() {
        let icon = get_icon_for_extension("png").unwrap();
        // Every 4th byte (alpha) should be non-zero after our fixup
        let all_alpha_valid = icon.rgba.chunks_exact(4).all(|px| px[3] > 0 || px[..3] == [0, 0, 0]);
        assert!(all_alpha_valid, "RGBA data should have valid alpha channel");
    }
}
