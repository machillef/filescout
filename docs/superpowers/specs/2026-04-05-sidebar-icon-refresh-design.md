---
title: Sidebar Icon Refresh
date: 2026-04-05
status: approved
---

# Sidebar Icon Refresh

## Goal

Replace the flat, monochrome Segoe Fluent Icons glyphs in the sidebar with colorful, "alive" icons:

- **Quick Access / Pinned folders** → real Windows shell bitmap icons (same as Explorer uses)
- **This PC drives** → custom XAML-shape drive icons, color-coded by drive type
- **Usage bar** → turns orange/red when a drive is >85% full, with matching detail text color

## Scope

Three self-contained changes, all within `MainWindow.xaml.cpp` and `SidebarIconInfo.cpp/.h`. No new dependencies, no asset files.

---

## 1. Quick Access / Pinned Folder Icons

### Problem

`BuildSidebarGlyph` (line 1010) checks `KnownFolderGlyph(displayName)` first. For known names (Desktop, Documents, Downloads, Pictures, Music, Videos, Home, This PC, Recycle Bin) it returns a monochrome Segoe Fluent glyph and ignores the shell bitmap that is already fetched on line 6712 of `BuildSidebarFavorites`.

### Fix

Remove the `KnownFolderGlyph` priority branch from `BuildSidebarGlyph`. The new priority order is:

1. Shell bitmap (`icon.has_value()`) → show as 16×16 `Image`
2. No icon available → show text abbreviation fallback

The shell bitmap is already fetched via `m_shellInterop.GetItemIcon(entry.path, true)` before `BuildSidebarGlyph` is called, so no new API calls are needed.

### Cleanup

`KnownFolderGlyph` and `DriveTypeGlyph` in `SidebarIconInfo.cpp/.h` become dead code once the glyph branch is removed from `BuildSidebarGlyph` and `BuildSidebarDriveGlyph`. Both functions and their declaration file are deleted.

`BuildSidebarFontIcon` remains (still used for toolbar buttons, unpin ×, status icon, etc.) but loses its call sites in the sidebar item path.

---

## 2. Drive Icons (This PC)

### Problem

`BuildSidebarDriveGlyph` returns a `FontIcon` using `DriveTypeGlyph` — a monochrome Segoe Fluent glyph. All fixed drives show the same barrel shape; there is no visual distinction between drive types beyond shape, and no color.

### Fix

Replace `BuildSidebarDriveGlyph` with `BuildDriveTypeIcon(uint32_t driveType)` returning a `FrameworkElement` built entirely from WinUI 3 XAML shapes (`Border`, `Rectangle`, `Ellipse`, `Grid`). No PNG assets, no Win2D.

**Per drive type:**

| `driveType` constant | Shape design | Accent color |
|---|---|---|
| `DRIVE_FIXED`, `DRIVE_RAMDISK` | Layered rectangles (upper panel + lower body + highlight circle + data stripe) — HDD silhouette | Blue (#2563eb / #93c5fd) |
| `DRIVE_REMOVABLE` | Shorter body with plug-top rectangle — USB stick silhouette | Green (#16a34a / #86efac) |
| `DRIVE_REMOTE` | Rectangle body with arc/circle top — network/NAS silhouette | Indigo (#7c3aed / #c4b5fd) |
| `DRIVE_CDROM` | Same as `DRIVE_FIXED` (rare device, low priority) | Blue |
| default | Same as `DRIVE_FIXED` | Blue |

Each icon is a 18×18 `Grid` to match the existing `BuildSidebarTile` call at line 7000.

---

## 3. Usage Bar Warning Color

### Problem

The `ProgressBar` (line 7027) always uses the WinUI default accent foreground. A drive at 97% capacity looks identical to one at 20%.

### Fix

After computing `drive.used_ratio`, choose the bar foreground brush conditionally:

- `used_ratio <= 0.85` → default (leave `Foreground` unset, WinUI uses accent)
- `used_ratio > 0.85` → set `Foreground` to `SystemFillColorCriticalBrush` (WinUI semantic orange/red)

Apply the same conditional to the detail `TextBlock` foreground (line 7010–7013): when critical, set its foreground to `SystemFillColorCriticalBrush` instead of the default dimmed opacity.

No new state or fields needed — `drive.used_ratio` is already computed.

---

## Files Changed

| File | Change |
|---|---|
| `MainWindow.xaml.cpp` | Remove glyph branch in `BuildSidebarGlyph`; replace `BuildSidebarDriveGlyph` with `BuildDriveTypeIcon`; add warning color logic to drive bar and detail text |
| `Domain/SidebarIconInfo.cpp` | Delete entirely |
| `Domain/SidebarIconInfo.h` | Delete entirely |
| `FileScout.Native.vcxproj` | Remove `SidebarIconInfo.cpp` and `.h` from compile/include lists |

---

## Non-Goals

- Changing toolbar button icons (Back, Forward, Refresh, etc.) — those stay as Segoe Fluent glyphs
- Changing file list row icons — already use shell bitmaps, untouched
- Adding animation or hover effects to sidebar icons
- Light theme adjustments — drive icon colors are chosen to work on dark background; light theme is out of scope here
