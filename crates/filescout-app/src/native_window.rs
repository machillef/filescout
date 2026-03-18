#[cfg(windows)]
use filescout_core::win32_context_menu::NativeMenuHost;
#[cfg(windows)]
use windows::Win32::Foundation::POINT;
#[cfg(windows)]
use windows::Win32::UI::WindowsAndMessaging::{GetCursorPos, GetForegroundWindow};

#[cfg(windows)]
pub fn context_menu_host() -> Option<NativeMenuHost> {
    unsafe {
        let hwnd = GetForegroundWindow();
        if hwnd.0.is_null() {
            return None;
        }

        let mut cursor = POINT::default();
        GetCursorPos(&mut cursor).ok()?;

        Some(NativeMenuHost {
            owner_hwnd: hwnd.0 as isize,
            screen_x: cursor.x,
            screen_y: cursor.y,
        })
    }
}

#[cfg(not(windows))]
pub fn context_menu_host() -> Option<filescout_core::win32_context_menu::NativeMenuHost> {
    None
}
