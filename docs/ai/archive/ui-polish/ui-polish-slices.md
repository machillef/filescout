# UI Polish — Slices

---

## Slice 1: Sidebar Icon Priority Fix
Goal: Known folders display distinct Segoe Fluent Icons glyphs instead of generic folder bitmaps; drives show type-specific icons.
User stories: 1, 2, 3, 4
Touched area: `MainWindow.xaml.cpp` — `BuildSidebarGlyph()`, `BuildSidebarDriveGlyph()`, `KnownFolderGlyph()`
Tests: Unit test for `KnownFolderGlyph()` returning correct codepoints; unit test mapping `GetDriveTypeW` results to expected glyphs. Manual: launch app, verify each Quick Access item has a unique icon, verify drives show HDD/USB/network icons.
Risk: Glyph swap could break user-pinned bookmarks that happen to match a known folder name.
Rollback: Revert `BuildSidebarGlyph()` priority order (two-line change).
Done when: Every known folder in Quick Access sidebar shows a distinct glyph; drives under This PC show drive-type icons; user-pinned bookmarks still show shell-extracted bitmaps.

---

## Slice 2: ThemeDefinition + ThemeService (Domain Layer)
Goal: Pure C++ theme model with 3 built-in palettes and file-based persistence — no UI yet.
User stories: 6, 8
Touched area: New `Domain/ThemeDefinition.h`, `Domain/ThemeService.h`, `Domain/ThemeService.cpp`
Tests: Unit tests — `DefaultDark()`/`TokyoNight()`/`CatppuccinMocha()` return 10 non-zero tokens each; `SaveThemeId()` + `LoadSavedThemeId()` round-trip; missing file → `"default-dark"`; unknown ID → `"default-dark"`; first-run directory creation.
Risk: File I/O path assumptions (LOCALAPPDATA missing). Mitigated by fallback to current directory.
Rollback: Delete 3 new files; no existing code modified.
Done when: All ThemeService unit tests pass; `theme.txt` is created/read correctly.

---

## Slice 3: Runtime Theme Application
Goal: Theme dictionary merges into `App.Resources()` at startup and swaps live at runtime — all `{ThemeResource}` bindings update.
User stories: 7, 8
Touched area: `App.xaml.cpp` (or `MainWindow.xaml.cpp`) — new `ApplyTheme()` function; references `ThemeDefinition` + `ThemeService`
Tests: Manual — launch with `theme.txt` set to `tokyo-night`, verify colors change; switch back to `default-dark`, verify pixel-identical to pre-change state. Unit test — `ApplyTheme()` builds a `ResourceDictionary` with correct key count.
Risk: WinUI resource dictionary swap may not cascade to all existing elements (e.g., already-rendered controls). Mitigated by testing all major UI areas.
Rollback: Remove `ApplyTheme()` call from startup; app reverts to built-in WinUI dark theme.
Done when: App starts with saved theme applied; changing theme in code triggers instant UI update across sidebar, file list, tabs, and title bar.

---

## Slice 4: Title Bar Theme Dropdown
Goal: User-facing theme switcher — dropdown in title bar with 3 theme options.
User stories: 5, 6, 7
Touched area: `MainWindow.xaml` (title bar Grid row 0), `MainWindow.xaml.cpp` (dropdown setup + click handlers)
Tests: Manual — dropdown appears in title bar, lists 3 themes with accent swatches, clicking switches theme instantly, current theme indicated, selection persists across restart.
Risk: Drag region conflict — dropdown in title bar might not be clickable if drag rect covers it. Mitigated by excluding dropdown bounds via `SetDragRectangles()`.
Rollback: Remove dropdown from title bar XAML + code-behind; theme still works but is only changeable via `theme.txt`.
Done when: Theme dropdown is visible in title bar, all 3 themes selectable, instant switching works, persists across restart.
