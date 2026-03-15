//! Native Windows shell operations for context menu enhancement.
//!
//! Provides "Open With..." dialog, Windows Properties dialog,
//! and "Open in Terminal" functionality via `ShellExecuteW`.

use std::path::Path;

use anyhow::Result;
use windows::core::PCWSTR;
use windows::Win32::UI::Shell::ShellExecuteW;
use windows::Win32::UI::WindowsAndMessaging::SW_SHOWNORMAL;

/// Encode a Rust string as a null-terminated wide string.
fn to_wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

/// Open the Windows "Open With..." dialog for the given file.
///
/// This shows the system app picker, allowing the user to choose
/// which application to open the file with.
pub fn shell_open_with(path: &Path) -> Result<()> {
    let verb = to_wide("openas");
    let file = to_wide(&path.display().to_string());
    let result = unsafe {
        ShellExecuteW(
            None,
            PCWSTR(verb.as_ptr()),
            PCWSTR(file.as_ptr()),
            PCWSTR::null(),
            PCWSTR::null(),
            SW_SHOWNORMAL,
        )
    };
    // ShellExecuteW returns an HINSTANCE; values > 32 indicate success
    if result.0 as usize > 32 {
        Ok(())
    } else {
        Err(anyhow::anyhow!(
            "ShellExecuteW(openas) failed for '{}'",
            path.display()
        ))
    }
}

/// Open the Windows Properties dialog for the given path.
pub fn shell_properties(path: &Path) -> Result<()> {
    let verb = to_wide("properties");
    let file = to_wide(&path.display().to_string());
    let result = unsafe {
        ShellExecuteW(
            None,
            PCWSTR(verb.as_ptr()),
            PCWSTR(file.as_ptr()),
            PCWSTR::null(),
            PCWSTR::null(),
            SW_SHOWNORMAL,
        )
    };
    if result.0 as usize > 32 {
        Ok(())
    } else {
        Err(anyhow::anyhow!(
            "ShellExecuteW(properties) failed for '{}'",
            path.display()
        ))
    }
}

/// Open a terminal (Windows Terminal or cmd) at the given directory.
pub fn shell_open_terminal(dir: &Path) -> Result<()> {
    // Try Windows Terminal first, fall back to cmd
    let wt = to_wide("wt");
    let args = to_wide(&format!("-d \"{}\"", dir.display()));
    let result = unsafe {
        ShellExecuteW(
            None,
            PCWSTR::null(),
            PCWSTR(wt.as_ptr()),
            PCWSTR(args.as_ptr()),
            PCWSTR::null(),
            SW_SHOWNORMAL,
        )
    };
    if result.0 as usize > 32 {
        return Ok(());
    }

    // Fallback: cmd.exe /K cd /d "path"
    let cmd = to_wide("cmd.exe");
    let cmd_args = to_wide(&format!("/K cd /d \"{}\"", dir.display()));
    let result = unsafe {
        ShellExecuteW(
            None,
            PCWSTR::null(),
            PCWSTR(cmd.as_ptr()),
            PCWSTR(cmd_args.as_ptr()),
            PCWSTR::null(),
            SW_SHOWNORMAL,
        )
    };
    if result.0 as usize > 32 {
        Ok(())
    } else {
        Err(anyhow::anyhow!(
            "Failed to open terminal at '{}'",
            dir.display()
        ))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn to_wide_encodes_correctly() {
        let wide = to_wide("hello");
        assert_eq!(wide, vec![104, 101, 108, 108, 111, 0]);
    }

    #[test]
    fn to_wide_empty_string() {
        let wide = to_wide("");
        assert_eq!(wide, vec![0]);
    }

    // Note: shell_open_with, shell_properties, shell_open_terminal
    // are not tested automatically because they open interactive Windows dialogs.
    // They are verified manually during visual testing.
}
