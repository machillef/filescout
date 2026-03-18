//! Native Windows shell operations for context menu enhancement.
//!
//! Provides direct shell verbs like "Open With..." and Properties, plus a
//! hosted Explorer-style `IContextMenu` path for filesystem selections.

use std::ffi::c_void;
use std::mem::size_of;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result, anyhow, bail};
use windows::Win32::Foundation::{HWND, LPARAM, LRESULT, POINT, WPARAM};
use windows::Win32::System::Com::CoTaskMemFree;
use windows::Win32::UI::Shell::Common::ITEMIDLIST;
use windows::Win32::UI::Shell::{
    CMF_EXPLORE, CMF_INCLUDESTATIC, CMF_NORMAL, CMIC_MASK_PTINVOKE, CMINVOKECOMMANDINFO,
    CMINVOKECOMMANDINFOEX, DefSubclassProc, GCS_VERB, IContextMenu, IContextMenu2, IContextMenu3,
    IShellFolder, RemoveWindowSubclass, SHBindToParent, SHParseDisplayName, SetWindowSubclass,
    ShellExecuteW,
};
use windows::Win32::UI::WindowsAndMessaging::{
    CreatePopupMenu, DeleteMenu, DestroyMenu, GetMenuItemCount, GetMenuItemID, GetMenuItemInfoW,
    GetMenuStringW, HMENU, MENUITEMINFOW, MF_BYPOSITION, MFS_DISABLED, MFS_GRAYED, MFT_SEPARATOR,
    MIIM_FTYPE, MIIM_ID, MIIM_STATE, MIIM_SUBMENU, SW_SHOWNORMAL, TPM_LEFTALIGN, TPM_RETURNCMD,
    TPM_RIGHTBUTTON, TrackPopupMenu, WM_DRAWITEM, WM_INITMENUPOPUP, WM_MEASUREITEM, WM_MENUCHAR,
};
use windows::core::{Interface, PCSTR, PCWSTR, PSTR};

const FIRST_COMMAND_ID: u32 = 1;
const LAST_COMMAND_ID: u32 = 0x7FFF;
const MENU_SUBCLASS_ID: usize = 0x4653_434D;
const BLOCKED_CANONICAL_VERBS: &[&str] = &["copy", "cut", "paste"];
const HIDDEN_INLINE_CANONICAL_VERBS: &[&str] = &[
    "copy",
    "cut",
    "paste",
    "open",
    "openas",
    "properties",
    "rename",
    "delete",
];
const HIDDEN_INLINE_LABELS: &[&str] = &[
    "open",
    "open with...",
    "properties",
    "windows properties",
    "rename",
    "delete",
    "copy",
    "cut",
    "paste",
];

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NativeMenuHost {
    pub owner_hwnd: isize,
    pub screen_x: i32,
    pub screen_y: i32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NativeContextMenuOutcome {
    Dismissed,
    Invoked,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ShellContextCommand {
    pub label: String,
    pub canonical_verb: String,
}

/// Encode a Rust string as a null-terminated wide string.
fn to_wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

fn hwnd_from_host(host: NativeMenuHost) -> Result<HWND> {
    if host.owner_hwnd == 0 {
        bail!("owner window handle was null");
    }

    Ok(HWND(host.owner_hwnd as *mut c_void))
}

fn shared_parent_dir(paths: &[PathBuf]) -> Result<PathBuf> {
    let first = paths
        .first()
        .context("native shell menu requires at least one path")?;
    let parent = first
        .parent()
        .with_context(|| format!("path '{}' has no parent folder", first.display()))?
        .to_path_buf();

    for path in paths.iter().skip(1) {
        if path.parent() != Some(parent.as_path()) {
            bail!("native shell menu currently requires all paths to share a parent folder");
        }
    }

    Ok(parent)
}

fn is_blocked_canonical_verb(verb: &str) -> bool {
    BLOCKED_CANONICAL_VERBS
        .iter()
        .any(|blocked| verb.eq_ignore_ascii_case(blocked))
}

fn should_hide_inline_command(label: &str, verb: &str) -> bool {
    HIDDEN_INLINE_CANONICAL_VERBS
        .iter()
        .any(|hidden| hidden.eq_ignore_ascii_case(verb))
        || HIDDEN_INLINE_LABELS
            .iter()
            .any(|hidden| hidden.eq_ignore_ascii_case(label))
}

struct OwnedPidl(*mut ITEMIDLIST);

impl OwnedPidl {
    fn from_path(path: &Path) -> Result<Self> {
        let wide = to_wide(&path.as_os_str().to_string_lossy());
        let mut pidl: *mut ITEMIDLIST = std::ptr::null_mut();

        unsafe {
            SHParseDisplayName(PCWSTR(wide.as_ptr()), None, &mut pidl, 0, None)?;
        }

        if pidl.is_null() {
            bail!(
                "SHParseDisplayName returned a null PIDL for '{}'",
                path.display()
            );
        }

        Ok(Self(pidl))
    }

    fn as_ptr(&self) -> *const ITEMIDLIST {
        self.0 as *const ITEMIDLIST
    }
}

impl Drop for OwnedPidl {
    fn drop(&mut self) {
        if !self.0.is_null() {
            unsafe {
                CoTaskMemFree(Some(self.0 as *const c_void));
            }
        }
    }
}

struct OwnedMenu(HMENU);

impl Drop for OwnedMenu {
    fn drop(&mut self) {
        if !self.0.0.is_null() {
            unsafe {
                let _ = DestroyMenu(self.0);
            }
        }
    }
}

struct HostedShellMenu {
    _selection: ShellSelection,
    context_menu: IContextMenu,
    popup_menu: OwnedMenu,
}

struct ShellSelection {
    _absolute_pidls: Vec<OwnedPidl>,
    child_pidls: Vec<*const ITEMIDLIST>,
    parent_folder: IShellFolder,
}

impl ShellSelection {
    fn from_paths(paths: &[PathBuf]) -> Result<Self> {
        let _ = shared_parent_dir(paths)?;

        let mut absolute_pidls = Vec::with_capacity(paths.len());
        let mut child_pidls = Vec::with_capacity(paths.len());
        let mut parent_folder: Option<IShellFolder> = None;

        for path in paths {
            let absolute_pidl = OwnedPidl::from_path(path)?;
            let mut child_pidl: *mut ITEMIDLIST = std::ptr::null_mut();
            let folder: IShellFolder =
                unsafe { SHBindToParent(absolute_pidl.as_ptr(), Some(&mut child_pidl))? };

            if child_pidl.is_null() {
                bail!(
                    "SHBindToParent returned a null child PIDL for '{}'",
                    path.display()
                );
            }

            if parent_folder.is_none() {
                parent_folder = Some(folder);
            }

            child_pidls.push(child_pidl as *const ITEMIDLIST);
            absolute_pidls.push(absolute_pidl);
        }

        Ok(Self {
            _absolute_pidls: absolute_pidls,
            child_pidls,
            parent_folder: parent_folder
                .context("could not determine parent shell folder for selection")?,
        })
    }
}

struct MenuMessageState {
    context_menu2: Option<IContextMenu2>,
    context_menu3: Option<IContextMenu3>,
}

struct MenuMessageSubclass {
    owner_hwnd: HWND,
    state: *mut MenuMessageState,
}

impl MenuMessageSubclass {
    fn install(owner_hwnd: HWND, context_menu: &IContextMenu) -> Result<Self> {
        let state = Box::new(MenuMessageState {
            context_menu2: context_menu.cast().ok(),
            context_menu3: context_menu.cast().ok(),
        });
        let state_ptr = Box::into_raw(state);

        let installed = unsafe {
            SetWindowSubclass(
                owner_hwnd,
                Some(shell_context_menu_subclass_proc),
                MENU_SUBCLASS_ID,
                state_ptr as usize,
            )
            .as_bool()
        };

        if !installed {
            unsafe {
                drop(Box::from_raw(state_ptr));
            }
            bail!("SetWindowSubclass failed while hosting shell context menu");
        }

        Ok(Self {
            owner_hwnd,
            state: state_ptr,
        })
    }
}

impl Drop for MenuMessageSubclass {
    fn drop(&mut self) {
        unsafe {
            let _ = RemoveWindowSubclass(
                self.owner_hwnd,
                Some(shell_context_menu_subclass_proc),
                MENU_SUBCLASS_ID,
            );

            if !self.state.is_null() {
                drop(Box::from_raw(self.state));
                self.state = std::ptr::null_mut();
            }
        }
    }
}

unsafe extern "system" fn shell_context_menu_subclass_proc(
    hwnd: HWND,
    umsg: u32,
    wparam: WPARAM,
    lparam: LPARAM,
    _uidsubclass: usize,
    dwrefdata: usize,
) -> LRESULT {
    let state_ptr = dwrefdata as *const MenuMessageState;
    if state_ptr.is_null() {
        return unsafe { DefSubclassProc(hwnd, umsg, wparam, lparam) };
    }

    let state = unsafe { &*state_ptr };

    match umsg {
        WM_INITMENUPOPUP | WM_DRAWITEM | WM_MEASUREITEM | WM_MENUCHAR => {
            if let Some(context_menu3) = &state.context_menu3 {
                let mut result = LRESULT(0);
                if unsafe {
                    context_menu3
                        .HandleMenuMsg2(umsg, wparam, lparam, Some(&mut result))
                        .is_ok()
                } {
                    return result;
                }
            }

            if let Some(context_menu2) = &state.context_menu2
                && unsafe { context_menu2.HandleMenuMsg(umsg, wparam, lparam).is_ok() }
            {
                return LRESULT(0);
            }
        }
        _ => {}
    }

    unsafe { DefSubclassProc(hwnd, umsg, wparam, lparam) }
}

fn command_verb(context_menu: &IContextMenu, command_offset: usize) -> Option<String> {
    let mut buffer = [0u8; 256];
    let result = unsafe {
        context_menu.GetCommandString(
            command_offset,
            GCS_VERB,
            None,
            PSTR(buffer.as_mut_ptr()),
            buffer.len() as u32,
        )
    };

    if result.is_err() {
        return None;
    }

    let length = buffer
        .iter()
        .position(|&byte| byte == 0)
        .unwrap_or(buffer.len());
    if length == 0 {
        return None;
    }

    Some(String::from_utf8_lossy(&buffer[..length]).into_owned())
}

fn filter_blocked_verbs(menu: HMENU, context_menu: &IContextMenu) -> Result<()> {
    let item_count = unsafe { GetMenuItemCount(Some(menu)) };
    if item_count <= 0 {
        return Ok(());
    }

    for position in (0..item_count).rev() {
        let menu_id = unsafe { GetMenuItemID(menu, position) };
        if !(FIRST_COMMAND_ID..=LAST_COMMAND_ID).contains(&menu_id) {
            continue;
        }

        let command_offset = (menu_id - FIRST_COMMAND_ID) as usize;
        if command_verb(context_menu, command_offset)
            .is_some_and(|verb| is_blocked_canonical_verb(&verb))
        {
            unsafe {
                DeleteMenu(menu, position as u32, MF_BYPOSITION)?;
            }
        }
    }

    Ok(())
}

fn build_shell_context_menu(owner_hwnd: HWND, paths: &[PathBuf]) -> Result<HostedShellMenu> {
    if paths.is_empty() {
        bail!("native shell menu requires at least one selected path");
    }

    let selection = ShellSelection::from_paths(paths)?;
    let context_menu = unsafe {
        selection.parent_folder.GetUIObjectOf::<IContextMenu>(
            owner_hwnd,
            &selection.child_pidls,
            None,
        )?
    };

    let popup_menu = unsafe { CreatePopupMenu()? };
    let popup_menu = OwnedMenu(popup_menu);

    unsafe {
        context_menu
            .QueryContextMenu(
                popup_menu.0,
                0,
                FIRST_COMMAND_ID,
                LAST_COMMAND_ID,
                CMF_NORMAL | CMF_EXPLORE | CMF_INCLUDESTATIC,
            )
            .ok()?;
    }

    filter_blocked_verbs(popup_menu.0, &context_menu)?;

    Ok(HostedShellMenu {
        _selection: selection,
        context_menu,
        popup_menu,
    })
}

fn menu_item_info(menu: HMENU, position: u32) -> Result<MENUITEMINFOW> {
    let mut info = MENUITEMINFOW {
        cbSize: size_of::<MENUITEMINFOW>() as u32,
        fMask: MIIM_FTYPE | MIIM_ID | MIIM_STATE | MIIM_SUBMENU,
        ..Default::default()
    };

    unsafe {
        GetMenuItemInfoW(menu, position, true, &mut info)?;
    }

    Ok(info)
}

fn menu_item_label(menu: HMENU, position: u32) -> Option<String> {
    let mut buffer = [0u16; 260];
    let copied =
        unsafe { GetMenuStringW(menu, position, Some(buffer.as_mut_slice()), MF_BYPOSITION) };

    if copied == 0 {
        return None;
    }

    sanitize_menu_label(&String::from_utf16_lossy(&buffer[..copied as usize]))
}

fn sanitize_menu_label(raw: &str) -> Option<String> {
    let without_accelerator = raw.split('\t').next().unwrap_or(raw).replace('&', "");
    let label = without_accelerator.trim();
    if label.is_empty() {
        None
    } else {
        Some(label.to_string())
    }
}

/// List shell commands that can be inlined into the FileScout-owned context menu.
///
/// This only includes leaf commands with canonical verbs that FileScout can
/// invoke directly. Cascading and owner-drawn menus stay behind the explicit
/// Windows fallback.
pub fn list_shell_item_context_commands(
    paths: &[PathBuf],
    max_commands: usize,
) -> Result<Vec<ShellContextCommand>> {
    if max_commands == 0 {
        return Ok(Vec::new());
    }

    let hosted_menu = build_shell_context_menu(HWND::default(), paths)?;
    let item_count = unsafe { GetMenuItemCount(Some(hosted_menu.popup_menu.0)) };
    if item_count <= 0 {
        return Ok(Vec::new());
    }

    let mut commands = Vec::new();
    for position in 0..(item_count as u32) {
        let info = menu_item_info(hosted_menu.popup_menu.0, position)?;
        if info.fType.0 & MFT_SEPARATOR.0 != 0 {
            continue;
        }
        if !info.hSubMenu.0.is_null() {
            continue;
        }
        if info.fState.0 & (MFS_DISABLED.0 | MFS_GRAYED.0) != 0 {
            continue;
        }
        if !(FIRST_COMMAND_ID..=LAST_COMMAND_ID).contains(&info.wID) {
            continue;
        }

        let Some(label) = menu_item_label(hosted_menu.popup_menu.0, position) else {
            continue;
        };
        let command_offset = (info.wID - FIRST_COMMAND_ID) as usize;
        let Some(canonical_verb) = command_verb(&hosted_menu.context_menu, command_offset) else {
            continue;
        };
        if should_hide_inline_command(&label, &canonical_verb) {
            continue;
        }

        commands.push(ShellContextCommand {
            label,
            canonical_verb,
        });

        if commands.len() >= max_commands {
            break;
        }
    }

    Ok(commands)
}

enum ShellCommandInvocation<'a> {
    Offset(u32),
    CanonicalVerb(&'a str),
}

fn invoke_shell_command(
    context_menu: &IContextMenu,
    owner_hwnd: HWND,
    command: ShellCommandInvocation<'_>,
    point: POINT,
) -> Result<()> {
    let owned_verb = match command {
        ShellCommandInvocation::Offset(_) => None,
        ShellCommandInvocation::CanonicalVerb(verb) => Some(
            verb.as_bytes()
                .iter()
                .copied()
                .chain(std::iter::once(0))
                .collect::<Vec<u8>>(),
        ),
    };
    let lp_verb = match command {
        ShellCommandInvocation::Offset(command_offset) => {
            PCSTR(command_offset as usize as *const u8)
        }
        ShellCommandInvocation::CanonicalVerb(_) => {
            PCSTR(owned_verb.as_ref().expect("verb bytes").as_ptr())
        }
    };
    let invoke = CMINVOKECOMMANDINFOEX {
        cbSize: size_of::<CMINVOKECOMMANDINFOEX>() as u32,
        fMask: CMIC_MASK_PTINVOKE,
        hwnd: owner_hwnd,
        lpVerb: lp_verb,
        nShow: SW_SHOWNORMAL.0,
        ptInvoke: point,
        ..Default::default()
    };

    let _owned_verb = owned_verb;

    unsafe {
        context_menu.InvokeCommand(
            (&invoke as *const CMINVOKECOMMANDINFOEX).cast::<CMINVOKECOMMANDINFO>(),
        )?;
    }

    Ok(())
}

/// Show the native shell context menu for one or more filesystem paths.
///
/// The current implementation intentionally filters shell clipboard verbs to
/// avoid conflicting with FileScout's internal clipboard model.
pub fn show_shell_item_context_menu(
    host: NativeMenuHost,
    paths: &[PathBuf],
) -> Result<NativeContextMenuOutcome> {
    let owner_hwnd = hwnd_from_host(host)?;
    let hosted_menu = build_shell_context_menu(owner_hwnd, paths)?;
    let _subclass = MenuMessageSubclass::install(owner_hwnd, &hosted_menu.context_menu)?;
    let command_id = unsafe {
        TrackPopupMenu(
            hosted_menu.popup_menu.0,
            TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
            host.screen_x,
            host.screen_y,
            None,
            owner_hwnd,
            None,
        )
    }
    .0 as u32;

    if command_id == 0 {
        return Ok(NativeContextMenuOutcome::Dismissed);
    }

    invoke_shell_command(
        &hosted_menu.context_menu,
        owner_hwnd,
        ShellCommandInvocation::Offset(command_id - FIRST_COMMAND_ID),
        POINT {
            x: host.screen_x,
            y: host.screen_y,
        },
    )?;

    Ok(NativeContextMenuOutcome::Invoked)
}

/// Invoke a shell command by canonical verb for the given filesystem selection.
pub fn invoke_shell_item_context_command(
    host: Option<NativeMenuHost>,
    paths: &[PathBuf],
    canonical_verb: &str,
) -> Result<()> {
    let owner_hwnd = host.map(hwnd_from_host).transpose()?.unwrap_or_default();
    let point = host.map_or(POINT::default(), |host| POINT {
        x: host.screen_x,
        y: host.screen_y,
    });
    let hosted_menu = build_shell_context_menu(owner_hwnd, paths)?;

    invoke_shell_command(
        &hosted_menu.context_menu,
        owner_hwnd,
        ShellCommandInvocation::CanonicalVerb(canonical_verb),
        point,
    )
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
        Err(anyhow!(
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
        Err(anyhow!(
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
        Err(anyhow!("Failed to open terminal at '{}'", dir.display()))
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

    #[test]
    fn blocked_canonical_verbs_are_detected_case_insensitively() {
        assert!(is_blocked_canonical_verb("copy"));
        assert!(is_blocked_canonical_verb("Cut"));
        assert!(is_blocked_canonical_verb("PASTE"));
        assert!(!is_blocked_canonical_verb("delete"));
    }

    #[test]
    fn sanitize_menu_label_strips_mnemonics_and_shortcuts() {
        assert_eq!(
            sanitize_menu_label("&Scan with Example\tShift+Alt+S"),
            Some("Scan with Example".to_string())
        );
    }

    #[test]
    fn inline_hidden_commands_filter_primary_entries() {
        assert!(should_hide_inline_command("Open", "open"));
        assert!(should_hide_inline_command(
            "Windows Properties",
            "properties"
        ));
        assert!(!should_hide_inline_command(
            "Scan with Example AV",
            "example.scan"
        ));
    }

    #[test]
    fn shared_parent_dir_requires_one_folder() {
        let paths = vec![
            PathBuf::from("C:\\Users\\machi\\alpha.txt"),
            PathBuf::from("C:\\Users\\machi\\beta.txt"),
        ];

        assert_eq!(
            shared_parent_dir(&paths).unwrap(),
            PathBuf::from("C:\\Users\\machi")
        );
    }

    #[test]
    fn shared_parent_dir_rejects_mixed_folders() {
        let paths = vec![
            PathBuf::from("C:\\Users\\machi\\alpha.txt"),
            PathBuf::from("C:\\Windows\\beta.txt"),
        ];

        assert!(shared_parent_dir(&paths).is_err());
    }

    // Note: show_shell_item_context_menu, shell_open_with, shell_properties, and
    // shell_open_terminal are not tested automatically because they open interactive
    // Windows UI. They are verified manually during visual testing.
}
