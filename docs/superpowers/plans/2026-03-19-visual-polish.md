# Visual Polish (R16) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Apply the approved `full-design-v2.html` visual direction to the native shell by replacing the hero header with a compact merged toolbar, fixing faint sidebar contrast, upgrading navigation icons to Segoe Fluent glyphs, and keeping file-list icons on the native Windows shell path.

**Architecture:** One combined visual-only slice touching the WinUI shell surface in `MainWindow.xaml` and the sidebar/pane-indicator construction code in `MainWindow.xaml.cpp`. Toolbar and sidebar chrome move toward the approved mockup, while file rows continue to use shell-extracted runtime icons so Windows-authentic file type rendering is preserved.

**Tech Stack:** WinUI 3 / Windows App SDK 1.8, C++/WinRT, XAML, Segoe Fluent Icons, MSBuild Debug x64

**Spec:** `docs/superpowers/specs/2026-03-19-visual-polish-design.md`
**Visual source:** `C:\Users\machi\source\repos\filescout\.superpowers\brainstorm\2171-1773962558\full-design-v2.html`

**Build command:**
```
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"
```

---

## File Map

| File | What changes |
|---|---|
| `native/FileScout.Native/MainWindow.xaml` | Remove hero card, merge toolbar, fix sidebar bg, fix RightPaneBorder, add FontIcon to toolbar |
| `native/FileScout.Native/MainWindow.xaml.cpp` | Add `BuildSidebarFontIcon()` helper; update `BuildSidebarDriveGlyph()`, `BuildSidebarGlyph()` callsites; update `UpdateActivePaneIndicator()` |

No new files. No new headers. No project file changes.

---

## Slice R16 — Combined Visual Polish

### Task 1: Align the shell chrome structure with the approved mockup

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml:46-193`

The Grid currently has 4 `RowDefinition` entries. The hero card occupies Grid.Row="0". The toolbar occupies Grid.Row="1". The content is Grid.Row="2". A footer row is Grid.Row="3". The approved design removes the hero card and folds app identity into the toolbar.

- [ ] **Step 1: Delete the hero card block (lines 53–128)**

In `MainWindow.xaml`, delete the entire `<Grid Grid.Row="0" Padding="20,16,20,10">` block (lines 53–128 inclusive). This is the block that contains "NATIVE SHELL EXPLORER", the 32px "FileScout" title, description text, tech chips, and ALPHA badge.

- [ ] **Step 2: Remove one RowDefinition**

In the `<Grid.RowDefinitions>` block (lines 46–51), delete one `<RowDefinition Height="Auto" />` entry so there are **3** entries total:

```xml
<Grid.RowDefinitions>
    <RowDefinition Height="Auto" />   <!-- Row 0: toolbar -->
    <RowDefinition Height="*" />      <!-- Row 1: content -->
    <RowDefinition Height="Auto" />   <!-- Row 2: footer (if any) -->
</Grid.RowDefinitions>
```

- [ ] **Step 3: Renumber remaining rows**

After deletion, update `Grid.Row` attributes on remaining elements:
- Former `Grid.Row="1"` (toolbar) → `Grid.Row="0"`
- Former `Grid.Row="2"` (content) → `Grid.Row="1"`
- Former `Grid.Row="3"` (if present) → `Grid.Row="2"`

- [ ] **Step 4: Add app identity and compact spacing to the promoted toolbar**

Inside the toolbar `Border`, add the `FileScout` label, ALPHA badge, and separator before the navigation buttons. Update the inner grid to four columns:

```xml
<Grid.ColumnDefinitions>
    <ColumnDefinition Width="Auto" />
    <ColumnDefinition Width="Auto" />
    <ColumnDefinition Width="*" />
    <ColumnDefinition Width="Auto" />
</Grid.ColumnDefinitions>
```

The left identity block should include:

```xml
<StackPanel Grid.Column="0"
            Orientation="Horizontal"
            Spacing="8"
            VerticalAlignment="Center">
    <TextBlock Text="FileScout"
               FontSize="13"
               FontWeight="SemiBold"
               VerticalAlignment="Center" />
    <Border Padding="6,2"
            CornerRadius="999"
            Background="#E1513A">
        <TextBlock Text="ALPHA"
                   Foreground="White"
                   FontSize="9"
                   FontWeight="SemiBold"
                   CharacterSpacing="60" />
    </Border>
    <Border Width="1"
            Height="18"
            Margin="4,0"
            Background="{ThemeResource DividerStrokeColorDefaultBrush}" />
</StackPanel>
```

Move nav/address/refresh to columns 1/2/3 respectively. Change the toolbar container `CornerRadius` from `20` to `14`.

- [ ] **Step 5: Prevent toolbar buttons from stretching**

Override the Back and Forward buttons with `HorizontalAlignment="Left"` so they size to content inside the merged toolbar.

- [ ] **Step 6: Replace toolbar SymbolIcon usage with FontIcon glyphs**

Replace:
- `Back` with `E76B`
- `Forward` with `E76C`
- `Refresh` with `E72C`

Use `FontFamily="Segoe Fluent Icons"` and `FontSize="14"`.

- [ ] **Step 7: Build and verify no errors**

Run the build command above. Expected: build succeeds (MSB8027/LNK4042 warnings are pre-existing and expected).

- [ ] **Step 8: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml
git commit -m "feat(ui): align shell chrome with approved visual design"
```

---

### Task 2: Fix sidebar contrast and preserve the approved shell surfaces

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml:202-329`

- [ ] **Step 1: Replace sidebar Background brush**

The sidebar `Border` starts at line 202 (the `Background` attribute is at line 205):
```xml
Background="{ThemeResource LayerFillColorAltBrush}"
```

Replace with the `CardBackgroundFillColorDefaultBrush` token, which is opaque and theme-aware:
```xml
Background="{ThemeResource CardBackgroundFillColorDefaultBrush}"
```

This resolves clearly in both dark and light themes without conflicting with the Mica backdrop.

- [ ] **Step 2: Keep the inspector and pane backgrounds unchanged**

Do not widen this task into list-row, tab-strip, or inspector structure changes. The design direction is surface polish, not a control rewrite.

- [ ] **Step 3: Give the inactive pane a visible neutral border**

`RightPaneBorder` currently has `BorderBrush="Transparent"`. Change it to:

```xml
BorderThickness="1"
BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}"
```

Leave `LeftPaneBorder` as the active default in XAML.

- [ ] **Step 4: Build and verify**

Run build. Expected: no new errors.

- [ ] **Step 5: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml
git commit -m "fix(ui): strengthen sidebar and pane surfaces for visual polish"
```

---

### Task 3: Replace ad hoc sidebar iconography with Fluent glyphs where appropriate

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp:382-523`

- [ ] **Step 1: Add the helper function**

Add a local helper for Segoe Fluent `FontIcon` construction near the existing sidebar glyph helpers:

```cpp
// Build a sidebar FontIcon using the Windows 11 Segoe Fluent Icons font.
// glyph: Unicode character string, e.g. L"\uEDA2"
// fontSize: icon size in pixels (default 15 for sidebar navigation)
// Win11 only: Segoe Fluent Icons glyphs are not available on Windows 10 MDL2 Assets.
winrt::Microsoft::UI::Xaml::Controls::FontIcon BuildSidebarFontIcon(
    std::wstring_view const glyph,
    double const fontSize = 15.0,
    winrt::Windows::UI::Color const color = { 0xFF, 0xC0, 0xC0, 0xE0 })
{
    FontIcon icon{};
    icon.Glyph(winrt::hstring{ glyph });
    icon.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily(L"Segoe Fluent Icons"));
    icon.FontSize(fontSize);
    icon.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(color));
    icon.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
    icon.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
    return icon;
}
```

- [ ] **Step 2: Replace the drive monogram with a drive glyph**

Update `BuildSidebarDriveGlyph()` so it no longer renders a drive-letter text block. Use Fluent glyph `EDA2` instead:

```cpp
winrt::Microsoft::UI::Xaml::FrameworkElement BuildSidebarDriveGlyph(
    std::filesystem::path const& /*path*/)
{
    // EDA2 = HardDrive glyph in Segoe Fluent Icons (Windows 11 only).
    return BuildSidebarFontIcon(L"\uEDA2", 18.0, { 0xFF, 0x80, 0x80, 0xC0 });
}
```

- [ ] **Step 3: Add a glyph lookup for well-known shell folders**

Add this helper after `BuildSidebarFontIcon` (around line 540):

```cpp
// Returns the Segoe Fluent Icons glyph for well-known shell folder names.
// Returns empty string if the folder is not in the known set.
// Win11 only. Codepoints verified for Segoe Fluent Icons.
std::wstring_view KnownFolderGlyph(std::wstring_view const displayName)
{
    using namespace std::literals;
    // Case-insensitive match against English display names.
    // TODO: If locale matters in future, match on KNOWNFOLDERID instead.
    if (displayName == L"Home"sv      || displayName == L"Quick access"sv) return L"\uE80F";
    if (displayName == L"Desktop"sv)                                        return L"\uEC64";
    if (displayName == L"Documents"sv)                                      return L"\uE8A5";
    if (displayName == L"Downloads"sv)                                      return L"\uE896";
    if (displayName == L"Pictures"sv)                                       return L"\uEB9F";
    if (displayName == L"Music"sv)                                          return L"\uEC4F";
    if (displayName == L"Videos"sv)                                         return L"\uE8B2";
    return {};
}
```

- [ ] **Step 4: Update BuildSidebarGlyph to prefer Fluent glyphs for known places**

Replace `BuildSidebarGlyph` (lines 382–409) with:

```cpp
winrt::Microsoft::UI::Xaml::FrameworkElement BuildSidebarGlyph(
    std::wstring const& displayName,
    std::optional<::FileScoutNative::Domain::IconBitmap> const& icon,
    std::wstring const& fallbackText)
{
    // Prefer Segoe Fluent glyph for well-known shell folders (Win11).
    auto const glyph = KnownFolderGlyph(displayName);
    if (!glyph.empty())
    {
        return BuildSidebarFontIcon(glyph);
    }

    // Fall back to shell-extracted bitmap icon for unknown/custom locations.
    if (icon.has_value())
    {
        auto image = winrt::Microsoft::UI::Xaml::Controls::Image{};
        image.Width(16);
        image.Height(16);
        image.Stretch(winrt::Microsoft::UI::Xaml::Media::Stretch::Uniform);
        image.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
        image.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
        if (auto const source = BuildIconSource(*icon))
        {
            image.Source(source);
            return image;
        }
    }

    // Final fallback: text monogram.
    auto fallback = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
    fallback.Text(ToHString(fallbackText));
    fallback.FontSize(10);
    fallback.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
    fallback.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
    fallback.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
    fallback.Opacity(0.82);
    return fallback;
}
```

- [ ] **Step 5: Update all callsites of BuildSidebarGlyph**

The signature now takes `displayName` as its first argument. Search for `BuildSidebarGlyph(` in `MainWindow.xaml.cpp` and update each callsite.

`FavoriteEntry` (`Domain/FavoriteEntry.h`) has fields `display_name`, `path`, `is_drives_view`, `is_recycle_bin`, `is_pinned` — it has no `icon` or `fallback_text` fields. The icon and fallback label are computed locally at the callsite. The actual callsite pattern (around line 4041) is:

```cpp
// Before:
BuildSidebarGlyph(entryIcon, SidebarFavoriteFallbackLabel(entry))

// After:
BuildSidebarGlyph(entry.display_name, entryIcon, SidebarFavoriteFallbackLabel(entry))
```

`entryIcon` is a locally-computed `std::optional<IconBitmap>` (from `m_shellInterop.GetItemIcon`). `entry.display_name` is already in scope at the callsite.

- [ ] **Step 6: Preserve native file-list icons**

Do not modify the shell icon extraction path in `ShellInteropService.cpp` or the row rendering path in XAML. The sidebar/tool chrome is moving to glyphs; file rows remain shell-extracted.

- [ ] **Step 7: Build and verify**

Run build. Expected: no errors.

- [ ] **Step 8: Verify at runtime**

Launch app. Home, Desktop, Documents, Downloads, Pictures sidebar entries should show Segoe Fluent glyphs. Custom pinned folders fall back to shell-extracted bitmaps.

- [ ] **Step 9: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml.cpp
git commit -m "feat(ui): switch approved sidebar chrome to Fluent glyphs"
```

---

### Task 4: Keep pane activation visuals aligned with the mockup

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp:3672-3682`

The existing `UpdateActivePaneIndicator()` sets brush but not thickness. With the new XAML defaults, the runtime toggle must manage both active and inactive states explicitly.

- [ ] **Step 1: Update UpdateActivePaneIndicator**

Replace the function body with:

```cpp
void MainWindow::UpdateActivePaneIndicator()
{
    auto const activeBrush   = LookupBrush(L"AccentFillColorDefaultBrush");
    auto const inactiveBrush = LookupBrush(L"CardStrokeColorDefaultBrush");

    bool const leftActive = (m_activePaneIndex == 0);

    LeftPaneBorder().BorderBrush(leftActive ? activeBrush : inactiveBrush);
    LeftPaneBorder().BorderThickness(leftActive
        ? Thickness{ 2, 2, 2, 2 }
        : Thickness{ 1, 1, 1, 1 });

    RightPaneBorder().BorderBrush(leftActive ? inactiveBrush : activeBrush);
    RightPaneBorder().BorderThickness(leftActive
        ? Thickness{ 1, 1, 1, 1 }
        : Thickness{ 2, 2, 2, 2 });
}
```

- [ ] **Step 2: Build and verify**

Run build.

- [ ] **Step 3: Verify at runtime**

Launch app. Active pane should show a thicker accent border; inactive pane should retain a visible thin neutral border.

- [ ] **Step 4: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml.cpp
git commit -m "fix(ui): align pane activation visuals with approved chrome"
```

---

## Final Verification Checklist

Before considering R16 complete, manually verify all spec acceptance criteria:

**Chrome:**
- [ ] Hero card gone — `Grid` has 3 `RowDefinition` entries
- [ ] "FileScout" + ALPHA badge visible in the toolbar row (leftmost)
- [ ] Back/Forward buttons do not stretch to fill their container
- [ ] Toolbar container `CornerRadius` is 14

**Sidebar:**
- [ ] Sidebar background is clearly distinct from Mica backdrop in dark theme
- [ ] Sidebar background is clearly distinct from Mica backdrop in light theme (toggle in Windows Settings → Personalization → Colors → Light)
- [ ] QUICK ACCESS, PINNED, STORAGE section labels unchanged
- [ ] Drive usage bar + free-space text readable against new background

**Icons:**
- [ ] Home, Documents, Downloads, Pictures show Segoe Fluent glyphs (not bitmaps)
- [ ] Desktop shows `EC64` glyph
- [ ] C:/D: drives show `EDA2` HardDrive glyph (not "C" / "D" text)
- [ ] Back (E76B), Forward (E76C), Refresh (E72C) render in toolbar
- [ ] File list rows continue to show shell-extracted icons (no blank icons)

**Pane indicator:**
- [ ] Active pane: 2px blue border
- [ ] Inactive pane: 1px neutral border (not transparent)
- [ ] Clicking between panes swaps borders correctly

**Build:**
- [ ] Build passes with no new warnings (MSB8027/LNK4042 are pre-existing, exempt)

---

## Final Commit

After all tasks pass and the checklist is green:

```bash
git add -u
git commit -m "feat(ui): complete visual polish R16

- Applies approved full-design-v2 shell chrome direction
- Keeps file rows on native shell-extracted icon rendering
- Replaces ad hoc sidebar/toolbar chrome icons with Fluent glyphs
- Strengthens sidebar contrast and pane activation surfaces"
```
