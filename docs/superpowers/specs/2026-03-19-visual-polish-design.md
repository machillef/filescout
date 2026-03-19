# FileScout Visual Polish — Design Spec

**Date:** 2026-03-19
**Scope:** Visual treatment only — colors, contrast, iconography, chrome layout. No structural feature changes.
**Baseline:** Existing WinUI 3 / C++/WinRT app in `native/FileScout.Native/`
**Target OS:** Windows 11 x64 (minimum, unpackaged)

---

## Summary of Decisions

| Area | Decision |
|---|---|
| Chrome layout | Remove hero header card; replace with single merged toolbar card |
| Sidebar background | Fix faint `#F7FAFDCC` → opaque dark WinUI surface (`#20203C`) |
| Navigation icons | Segoe Fluent Icons glyphs via `FontIcon`, constructed in C++/WinRT |
| File list icons | Shell-extracted at runtime via `IShellItemImageFactory` (no change) |
| Active pane indicator | Active pane: 2px `#0078D4` border; inactive: 1px neutral `CardStrokeColorDefaultBrush` |
| Inspector | Remains at bottom of sidebar — no change |
| Drive usage cards | Already implemented (`GetDiskFreeSpaceExW` + `ProgressBar`) — visual tweaks only |

---

## 1. Chrome — Merged Toolbar Card

### Current state
- **Row 0** (Grid): large hero card — "NATIVE SHELL EXPLORER" label, 32px "FileScout" title, description, tech chips, ALPHA badge. ~140px tall.
- **Row 1** (Grid): separate toolbar card — Back, Forward, address bar, Refresh.
- **Row 2**: sidebar + dual pane content.
- **Row 3**: (status/empty).
- `Grid.RowDefinitions` has 4 entries.

### Target state
**Remove Row 0 entirely.** The new Row 0 is the merged toolbar card. After the change, the `Grid` has **3 `RowDefinition` entries**: toolbar (Auto), content (*), footer (Auto).

```
Row 0 (Auto): [ FileScout ] [ALPHA] │ [← Back] [→ Fwd] [ address bar __ ] [↺ Refresh]
Row 1 (*):    sidebar + left pane + right pane
Row 2 (Auto): (footer/status if needed)
```

**XAML changes:**
- Delete the `<Grid Grid.Row="0" Padding="20,16,20,10">` hero card block entirely.
- Move the existing toolbar `<Grid Grid.Row="1" ...>` to `Grid.Row="0"`.
- Move the content `<Grid Grid.Row="2" ...>` to `Grid.Row="1"`.
- Remove `Grid.RowDefinitions` entry count from 4 → 3.
- Inside the merged toolbar card `Border`, add before the back/forward `StackPanel`:
  - `TextBlock` "FileScout" at `FontSize="13"` `FontWeight="SemiBold"` `VerticalAlignment="Center"`
  - ALPHA badge `Border` with `Background="#E1513A"` `CornerRadius="999"` `Padding="6,2"` (6px horizontal, 2px vertical — note: horizontal first in WinUI `Padding`)

**Corner radius:** The toolbar container `Border` changes from `CornerRadius="20"` (current Row 1) to `CornerRadius="14"`. This aligns with button corner radii (14px) for a more unified look.

**Back/Forward sizing:** The existing `ChromeToolbarButtonStyle` sets `HorizontalAlignment="Stretch"`, which expands buttons to fill their parent column. For the merged card these buttons should auto-size to content. Override per-button with `HorizontalAlignment="Left"` (not `MinWidth="Auto"` — that is an invalid XAML value for `double` properties).

---

## 2. Sidebar — Contrast Fix

### Current state
`Background="{ThemeResource LayerFillColorAltBrush}"` on the sidebar `Border` renders as approximately `#F7FAFDCC` in dark theme — semi-transparent and nearly indistinguishable from the Mica backdrop.

### Target state
Override to a theme-aware opaque surface. **Do not use `AcrylicBackgroundFillColorDefaultBrush`** — Acrylic nested inside a Mica window is a Fluent design anti-pattern and produces unwanted visual noise.

Use a theme-aware resource instead:

```xml
Background="{ThemeResource CardBackgroundFillColorDefaultBrush}"
```

`CardBackgroundFillColorDefaultBrush` is the semantically correct token for elevated surfaces in a Mica window. It resolves to an opaque surface in both light and dark themes, ensuring the sidebar reads clearly regardless of the system theme.

If additional visual differentiation is needed in dark theme, a local override can be applied:

```xml
<!-- In page/window resources -->
<SolidColorBrush x:Key="SidebarBackgroundOverrideBrush"
                 Color="#20203C"
                 x:Load="{Binding ActualTheme, RelativeSource={RelativeSource Self}, Converter={StaticResource IsDarkThemeConverter}}" />
```

But `CardBackgroundFillColorDefaultBrush` is the preferred single-resource solution.

**Sidebar width:** Stays at 260px. No structural change.

**Section labels:** The existing programmatically-built section headers use the labels **`QUICK ACCESS`**, **`PINNED`**, and **`STORAGE`** (matching `BuildSidebarFavorites()` in `MainWindow.xaml.cpp`). These label names are **not changing**. Do not introduce "PLACES" or "DRIVES" as replacements.

---

## 3. Navigation Icons — Segoe Fluent Icons

### Background
The entire sidebar is built programmatically in C++/WinRT (`BuildSidebarFavorites()`, `BuildSidebarDriveUsage()`, etc.) — there is no XAML markup to edit for sidebar icons. Icon changes must be made in the C++ construction code, not in XAML.

The current sidebar drive icon is a text monogram (letter "C", "D") rendered as a `TextBlock`. The target replaces this with a `FontIcon` constructed in C++/WinRT.

### Glyph mapping (Windows 11, Segoe Fluent Icons)

| Location | Glyph | Codepoint | Notes |
|---|---|---|---|
| Back button | Back | `E76B` | MDL2 + Fluent |
| Forward button | Forward | `E76C` | MDL2 + Fluent |
| Refresh button | Refresh | `E72C` | MDL2 + Fluent |
| Home / Quick Access | Home | `E80F` | MDL2 + Fluent |
| Desktop folder | Desktop | `EC64` | Fluent only (Win11) |
| Documents folder | Document | `E8A5` | MDL2 + Fluent |
| Downloads folder | Download | `E896` | MDL2 + Fluent |
| Pictures folder | Photo2 | `EB9F` | MDL2 + Fluent |
| Local disk (C:, D:) | HardDrive | `EDA2` | Fluent only (Win11) |
| Folders in sidebar | Folder | `E8B7` | MDL2 + Fluent — color `#FFB700` |
| Window minimize | ChromeMinimize | `E921` | MDL2 + Fluent |
| Window maximize | ChromeMaximize | `E922` | MDL2 + Fluent |
| Window close | ChromeClose | `E8BB` | MDL2 + Fluent |

**Note on Win11-only glyphs:** `EDA2` (HardDrive) and `EC64` (Desktop) exist in Segoe Fluent Icons but are not in Segoe MDL2 Assets. Since the target OS is Windows 11 only, this is acceptable. A code comment should document this constraint.

### C++/WinRT construction pattern

FontIcon must be constructed in code. The `SymbolThemeFontFamily` theme resource must be resolved manually (same pattern as the existing `LookupBrush` helper):

```cpp
// Resolve SymbolThemeFontFamily from app resources (once, cache the result)
auto fontFamilyBox = Application::Current().Resources()
    .Lookup(box_value(L"SymbolThemeFontFamily"));
auto fontFamily = fontFamilyBox.as<winrt::Microsoft::UI::Xaml::Media::FontFamily>();

// Construct a FontIcon for a navigation glyph
FontIcon icon;
icon.Glyph(L"\uEDA2");          // HardDrive
icon.FontFamily(fontFamily);
icon.FontSize(16.0);
icon.Foreground(LookupBrush(L"TextFillColorPrimaryBrush"));
```

Alternatively, since the app targets Windows 11 exclusively, hard-coding `L"Segoe Fluent Icons"` avoids the resource lookup and is acceptable with a comment:

```cpp
icon.FontFamily(FontFamily(L"Segoe Fluent Icons")); // Win11 only, per CLAUDE.md target
```

**Affected functions to update:**
- `BuildSidebarGlyph()` — currently uses bitmap icons for places; switch to `FontIcon` for standard shell folders (Home, Desktop, Documents, Downloads, Pictures)
- `BuildSidebarDriveGlyph()` — currently uses text monogram; switch to `FontIcon` with `EDA2`
- Toolbar Back/Forward/Refresh `SymbolIcon` — switch to `FontIcon` with `E76B`/`E76C`/`E72C`

### File list icons
**No change.** The existing `IShellItemImageFactory`-based extraction in `ShellInteropService.cpp` already provides correct Windows shell icons for all file types at runtime.

---

## 4. Drive Usage Cards — Existing Implementation

Drive usage cards (`GetDiskFreeSpaceExW`, `ProgressBar`, `BuildDriveUsageText()`) are **already fully implemented** in `BuildSidebarDriveUsage()`. No new data-layer work is required.

**Visual tweaks only (if desired):**
- Progress bar height: currently default `ProgressBar` — optionally replace with a custom 3px `Rectangle` fill for a more compact look
- Free space label: `FontSize="10"` with `Opacity="0.6"` for reduced visual weight

---

## 5. Active Pane Indicator

### Current state
- `LeftPaneBorder` has `BorderBrush="#FF0078D4"` and `BorderThickness="2"` in XAML.
- `RightPaneBorder` has `BorderBrush="Transparent"` — no visible border when inactive.
- Pane activation code in `MainWindow.xaml.cpp` (the `SetActivePaneIndex` function or equivalent click handler) swaps `BorderBrush` and `BorderThickness` between panes at runtime.

### Target state
Both panes always show a border. Active: `2px #0078D4`. Inactive: `1px CardStrokeColorDefaultBrush`.

**XAML changes:**
- `RightPaneBorder`: change `BorderBrush="Transparent"` → `BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}"` and `BorderThickness="1"`

**C++ changes (required alongside XAML):**
The C++ pane-activation handler (search `SetActivePaneIndex` or `LeftPaneBorder().BorderBrush(`) must be updated to also set the inactive pane border to the neutral brush. Example:

```cpp
auto activeBrush   = LookupBrush(L"AccentFillColorDefaultBrush"); // or literal #0078D4
auto inactiveBrush = LookupBrush(L"CardStrokeColorDefaultBrush");

LeftPaneBorder().BorderBrush(isLeftActive ? activeBrush : inactiveBrush);
LeftPaneBorder().BorderThickness(isLeftActive ? Thickness{ 2,2,2,2 } : Thickness{ 1,1,1,1 });
RightPaneBorder().BorderBrush(isLeftActive ? inactiveBrush : activeBrush);
RightPaneBorder().BorderThickness(isLeftActive ? Thickness{ 1,1,1,1 } : Thickness{ 2,2,2,2 });
```

---

## 6. What Is NOT Changing

- Layout structure: sidebar width, dual pane columns, tab bars — unchanged
- `ChromeToolbarButtonStyle` corner radius (14px) — unchanged
- Selection inspector card placement — unchanged
- Column headers, sort arrows, context menus, drag/drop — unaffected
- File list row layout — unaffected

---

## Implementation Slices

### Slice R16a — Chrome + Sidebar contrast
1. Remove hero card (Grid Row 0), shift remaining rows up, clean up `RowDefinitions` to 3 entries
2. Extend toolbar card to include app name + ALPHA badge as leftmost elements
3. Fix `HorizontalAlignment` on Back/Forward buttons (remove Stretch override)
4. Fix toolbar container `CornerRadius` to 14
5. Fix sidebar background to `CardBackgroundFillColorDefaultBrush`

### Slice R16b — Icon system
1. Update `BuildSidebarGlyph()` to use `FontIcon` for standard shell folders
2. Update `BuildSidebarDriveGlyph()` to use `FontIcon` with `EDA2` instead of text monogram
3. Update toolbar Back/Forward/Refresh from `SymbolIcon` to `FontIcon` with correct glyph codes
4. Add `// Win11 only` comment wherever Fluent-only glyphs are used
5. Update active/inactive pane border logic (XAML + C++ handler)

---

## Success Criteria

**Chrome:**
- [ ] Hero card removed; `Grid` has exactly 3 `RowDefinition` entries after the change
- [ ] Single toolbar row visible: "FileScout" label + ALPHA badge + Back/Fwd + address bar + Refresh
- [ ] Back/Forward buttons auto-size to content (no Stretch stretching)
- [ ] Toolbar container `CornerRadius` is 14

**Sidebar:**
- [ ] Sidebar background clearly distinct from Mica backdrop in both **dark and light theme**
- [ ] `QUICK ACCESS`, `PINNED`, `STORAGE` section labels unchanged
- [ ] Drive usage cards remain legible (progress bar + text visible against the new background)

**Icons:**
- [ ] Home, Documents, Downloads, Pictures sidebar entries show Segoe Fluent glyph icons (not bitmaps or monograms)
- [ ] C: and D: drives show `EDA2` HardDrive glyph (not text monogram "C"/"D")
- [ ] Desktop folder entry shows `EC64` glyph (not `E8FC` ThisPC)
- [ ] Back (E76B), Forward (E76C), Refresh (E72C) glyphs render in toolbar
- [ ] File list rows continue to show shell-extracted icons (regression: no blank/missing icons)

**Pane indicator:**
- [ ] Active pane: 2px `#0078D4` border
- [ ] Inactive pane: 1px `CardStrokeColorDefaultBrush` border (not `Transparent`)
- [ ] Switching active pane updates both borders correctly

**Build:**
- [ ] Build passes with no new warnings (MSB8027/LNK4042 pre-existing warnings are exempt)
