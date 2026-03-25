# UI Polish — Scope Map

Initiative: **ui-polish**
Size: **Medium**
Objective: Fix sidebar icon resolution (Quick Access + This PC drives) and add a theme system with 2-3 curated color palettes.

---

## In-Scope

| Area | Files | What changes |
|---|---|---|
| Sidebar icon resolution | `MainWindow.xaml.cpp` (lines 477-568, 627-646) | Prefer Segoe Fluent glyphs for known folders; add drive-type glyphs |
| Theme infrastructure | New: `Domain/ThemeDefinition.h`, `Domain/ThemeService.h/.cpp` | Pure C++ theme model + persistence |
| Theme resource dictionaries | Built programmatically in C++ at runtime | Custom color overrides merged into `App.Resources().MergedDictionaries()` |
| Theme switching UI | `MainWindow.xaml` + `MainWindow.xaml.cpp` | Settings flyout or sidebar control to switch themes |
| App.xaml theme loading | `App.xaml`, `App.xaml.cpp` | Merge custom resource dictionary at startup |
| Hardcoded colors | `MainWindow.xaml.cpp` — `SolidBrush()` calls, `LookupBrush()` keys | Route through theme-aware resource keys |

## Adjacent (understand, don't touch)

| Area | Files | Contract |
|---|---|---|
| Shell icon extraction | `ShellInteropService.h/.cpp` | `GetItemIcon(path, is_folder) → optional<IconBitmap>` — bitmap extraction is correct; sidebar just needs better selection logic |
| Favorites data | `FavoritesService.h/.cpp` | `GetSidebarFavorites() → vector<FavoriteEntry>` — data source unchanged |
| File list icons | `BackfillIconsAsync()`, `FileRowViewModel` | Extension-based icon cache for file list pane — separate from sidebar |
| Breadcrumb / title bar | `MainWindow.xaml` rows 0 | Title bar layout — theme dropdown is added to the right side, but breadcrumb/nav buttons are unchanged |

## Out-of-Scope

- File list pane icons (working correctly via extension cache)
- Navigation logic (tab switching, pane routing, breadcrumb)
- Shell operations (copy, move, delete, rename)
- Archive support
- Status bar
- Keyboard shortcuts
- Performance / large-directory handling

## Dependents on Target Area

| Dependent | Risk |
|---|---|
| `BuildSidebarFavorites()` | Calls `BuildSidebarGlyph()` — icon order change affects all sidebar entries |
| `UpdateThisPCSection()` | Calls `BuildSidebarDriveGlyph()` — drive icon changes visible immediately |
| `UpdateBookmarksSection()` | Calls `BuildSidebarGlyph()` — same icon pipeline |
| All UI elements using `{ThemeResource}` | Theme dictionary swap affects every themed element |

## Root Cause Analysis — Sidebar Icon Bug

In `BuildSidebarGlyph()` (line 535-568), icon priority is:
1. Shell bitmap (if `icon.has_value()`) → **always wins for folders**
2. Known folder glyph (Segoe Fluent Icons)
3. Fallback text

For known folders (Desktop, Documents, etc.), `GetItemIcon()` returns a valid but **generic folder bitmap**. This takes priority over the distinctive Segoe Fluent glyphs. All known folders therefore display the same generic folder icon.

**Fix:** Swap priority — check `KnownFolderGlyph()` first, fall back to shell bitmap only when no glyph match exists.

For drives, `BuildSidebarDriveGlyph()` has the same issue — shell returns folder-like bitmaps for drive roots instead of drive icons. Adding explicit drive-type glyphs (HDD `\uEDA2`, USB `\uE88E`, etc.) or using `GetDriveTypeW()` to select glyphs fixes this.

---

## Verification Commands

```bash
# Build (NuGet restore + Debug x64)
powershell -Command "cmd /c 'call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal'"

# Run tests
native/bin/x64/Debug/Tests/FileScout.Tests.exe

# Launch app for manual verification
native/bin/x64/Debug/FileScout.Native/FileScout.Native.exe
```

---

## Definition of Done

1. **Quick Access sidebar** shows distinct Segoe Fluent Icons for each known folder (Home, Desktop, Documents, Downloads, Pictures, Music, Videos) — no two known folders share the same icon.
2. **This PC section** shows drive-appropriate icons (HDD/SSD glyph) — not generic folder icons.
3. **At least 2 additional themes** are available beyond the current dark mode (e.g., Tokyo Night, a second curated palette), selectable from the UI.
4. **Theme selection persists** across app restarts.
