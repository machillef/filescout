# UI Polish — Design

Initiative: **ui-polish**
Status: **Approved**
Date: 2026-03-25

---

## Part 1 — Requirements

### Problem Statement

FileScout's sidebar shows identical generic folder icons for all known folders (Home, Desktop, Documents, Downloads, Pictures, Music, Videos) and drives under This PC. The shell bitmap extraction returns a valid but generic folder icon that takes priority over the distinctive Segoe Fluent Icons glyphs already defined in `KnownFolderGlyph()`. The app is also locked to a single dark theme with no customization.

### User Stories

1. As a user, I want each known folder in Quick Access (Home, Desktop, Documents, Downloads, Pictures, Music, Videos) to display a distinct icon, so that I can visually identify folders at a glance without reading labels.
2. As a user, I want the Recycle Bin sidebar entry to show a trash can icon, so that it's visually distinct from folders.
3. As a user, I want drives under This PC to show drive-type-appropriate icons (HDD/SSD, USB, network, optical), so that I can distinguish between drive types.
4. As a user, I want user-pinned bookmarks to show their actual shell-extracted icon, so that custom folders retain their native appearance.
5. As a user, I want to switch between themes (Default Dark, Tokyo Night, Catppuccin Mocha) from a dropdown in the title bar, so that I can customize the app's appearance.
6. As a user, I want my theme selection to persist across app restarts, so that I don't have to re-select it every time.
7. As a user, I want theme changes to apply instantly without restarting the app, so that I can preview and compare themes quickly.
8. As a user, I want the Default Dark theme to look exactly like the current app, so that the update doesn't change anything for users who prefer the existing look.

### Out of Scope

- Light mode / high-contrast themes
- File list pane icons (working correctly via extension cache)
- Settings page beyond theme switching
- Custom user-defined themes
- Navigation logic, shell operations, keyboard shortcuts, performance

---

## Part 2 — Design

### Architecture & Components

Three new files in Domain (pure C++, no WinRT dependencies):

| File | Responsibility |
|---|---|
| `Domain/ThemeDefinition.h` | Immutable struct with ~10 color tokens. Three static factory methods: `DefaultDark()`, `TokyoNight()`, `CatppuccinMocha()`. |
| `Domain/ThemeService.h` | `LoadSavedThemeId()`, `SaveThemeId(id)`, `GetAllThemes()`, `GetTheme(id)`. |
| `Domain/ThemeService.cpp` | File I/O for `%LOCALAPPDATA%\FileScoutNative\theme.txt`. |

### ThemeDefinition Color Tokens

```cpp
struct ThemeDefinition
{
    std::wstring id;          // e.g. "tokyo-night"
    std::wstring display_name; // e.g. "Tokyo Night"
    uint32_t base;            // App background
    uint32_t surface;         // Card/panel background
    uint32_t surface_alt;     // Alternate surface (layers)
    uint32_t accent;          // Primary accent
    uint32_t accent_secondary; // Secondary accent / highlight
    uint32_t text_primary;    // Primary text
    uint32_t text_secondary;  // Secondary / muted text
    uint32_t border;          // Card / panel borders
    uint32_t border_subtle;   // Dividers
    uint32_t hover;           // Hover / subtle fill

    static ThemeDefinition DefaultDark();
    static ThemeDefinition TokyoNight();
    static ThemeDefinition CatppuccinMocha();
};
```

### Theme Palettes

**Default Dark** (matches current WinUI 3 dark theme):
- base: `#1c1c1c`, surface: `#2d2d2d`, surface_alt: `#252525`, accent: `#0078d4`, accent_secondary: `#005a9e`
- text_primary: `#ffffff`, text_secondary: `#9a9a9a`, border: `#383838`, border_subtle: `#2d2d2d`, hover: `#ffffff14`

**Tokyo Night**:
- base: `#1a1b26`, surface: `#24283b`, surface_alt: `#1f2335`, accent: `#7aa2f7`, accent_secondary: `#bb9af7`
- text_primary: `#c0caf5`, text_secondary: `#565f89`, border: `#29293f`, border_subtle: `#222436`, hover: `#c0caf514`

**Catppuccin Mocha**:
- base: `#1e1e2e`, surface: `#313244`, surface_alt: `#181825`, accent: `#cba6f7`, accent_secondary: `#94e2d5`
- text_primary: `#cdd6f4`, text_secondary: `#6c7086`, border: `#45475a`, border_subtle: `#313244`, hover: `#cdd6f414`

### Theme Application Flow

```
App startup
  → ThemeService::LoadSavedThemeId()       // reads theme.txt
  → ThemeDefinition factory method          // builds color struct
  → Build ResourceDictionary from tokens    // in MainWindow or App
  → Merge into App.Resources().MergedDictionaries()
  → All {ThemeResource} bindings update

User clicks dropdown
  → ThemeService::SaveThemeId(newId)        // persist
  → Clear old custom dictionary
  → Build + merge new dictionary
  → UI updates instantly
```

### Color Token → WinUI Resource Key Mapping

| Token | WinUI Resource Key |
|---|---|
| `base` | `ApplicationPageBackgroundThemeBrush` |
| `surface` | `CardBackgroundFillColorDefaultBrush` |
| `surface_alt` | `LayerFillColorDefaultBrush` |
| `accent` | `AccentFillColorDefaultBrush` |
| `accent_secondary` | `AccentFillColorSecondaryBrush` |
| `text_primary` | `TextFillColorPrimaryBrush` |
| `text_secondary` | `TextFillColorSecondaryBrush` |
| `border` | `CardStrokeColorDefaultBrush` |
| `border_subtle` | `DividerStrokeColorDefaultBrush` |
| `hover` | `SubtleFillColorSecondaryBrush` |

### Additional Resource Keys (WinUI Defaults)

The codebase also uses these keys that are **not** overridden by the theme dictionary:

| Key | Used For | Rationale |
|---|---|---|
| `SystemFillColorCriticalBrush` | Delete action highlighting | System semantic color — should stay red across all themes |
| `LayerFillColorAltBrush` | Alternate layer backgrounds | Close enough to `surface_alt` mapping; WinUI default acceptable |
| `CardBackgroundFillColorSecondaryBrush` | Secondary card backgrounds | WinUI default follows primary card override adequately |

These are intentionally left to WinUI system defaults. If visual testing reveals clashes, they can be added to the token map in a follow-up slice.

### Color Encoding

Colors in `ThemeDefinition` are stored as `uint32_t` in `0xAARRGGBB` format. Most tokens use `0xFF` alpha (fully opaque). The `hover` token uses a low alpha (e.g., `0x14FFFFFF` for `#ffffff14`) to create translucent hover fills. CSS hex notation in this doc maps directly: `#RRGGBB` → `0xFFRRGGBB`, `#RRGGBBAA` → `0xAARRGGBB`.

### Theme Persistence Format

`theme.txt` contains a single line: the bare theme ID string (e.g., `tokyo-night`), UTF-8 encoded, no trailing newline. `LoadSavedThemeId()` trims whitespace and trailing `\r`/`\n` to handle any line-ending variation.

### Sidebar Icon Fix

**Root cause:** In `BuildSidebarGlyph()` (MainWindow.xaml.cpp:535-568), icon priority is shell bitmap → glyph → fallback text. Shell always returns a valid generic folder bitmap for known folders, so glyph is never reached.

**Fix 1 — Known folder glyphs (Quick Access):**
Swap priority in `BuildSidebarGlyph()`: check `KnownFolderGlyph(displayName)` first. If a glyph match exists, use it. Fall back to shell bitmap only for non-known-folder items. Note: Recycle Bin (User Story 2) is already mapped in `KnownFolderGlyph()` to `\uE74D` — this fix ensures it renders correctly by the same priority swap. User-pinned bookmarks (User Story 4) are not matched by `KnownFolderGlyph()`, so they fall through to the shell bitmap path — no change needed for bookmarks.

**Fix 2 — Drive type glyphs (This PC):**
In `BuildSidebarDriveGlyph()`, replace current logic with `GetDriveTypeW()` lookup:

| `GetDriveTypeW` result | Glyph | Codepoint |
|---|---|---|
| `DRIVE_FIXED` | HDD/SSD | `\uEDA2` |
| `DRIVE_REMOVABLE` | USB | `\uE88E` |
| `DRIVE_REMOTE` | Network | `\uE8CE` |
| `DRIVE_CDROM` | Optical | `\uE958` |
| Default | HDD fallback | `\uEDA2` |

### Title Bar Theme Dropdown

- `DropDownButton` added to the title bar Grid row 0, in a new column after the search box (right-aligned)
- Must be excluded from the title bar drag region via `SetDragRectangles()` so it remains clickable
- Shows current theme name + small colored accent circle
- Flyout contains 3 `MenuFlyoutItem` entries, each with theme name + accent swatch
- Click applies theme immediately via `ThemeService::SaveThemeId()` + dictionary swap

### Hardcoded Color Migration

The existing `SolidBrush()` calls in sidebar helpers (lines 589, 600, 611) use transparent black (`0x00, 0x00, 0x00, 0x00`) and do not need migration. However, any future sidebar code that introduces visible hardcoded colors should use `LookupBrush()` with theme-aware resource keys. The main migration target is `BuildSidebarFontIcon()` (line 450), which uses `LookupBrush(L"TextFillColorSecondaryBrush")` — this already routes through theme resources and will respond to theme changes automatically.

### Error Handling

- Missing/corrupt `theme.txt` → fall back to Default Dark silently
- Unknown theme ID in file → fall back to Default Dark silently
- `ResourceDictionary` merge failure → log warning, continue with built-in theme
- First run (no `FileScoutNative` directory) → `SaveThemeId()` creates the directory via `std::filesystem::create_directories()` before writing

### Testing Approach

- **Unit tests (ThemeService):** load/save round-trip, missing file fallback, unknown ID fallback, first-run directory creation
- **Unit tests (ThemeDefinition):** each factory returns correct token count, non-empty values, valid ARGB
- **Unit tests (drive glyphs):** `GetDriveTypeW` result → correct glyph codepoint
- **Manual verification:** launch app, confirm sidebar icons are distinct per folder, switch all 3 themes, restart and confirm persistence
