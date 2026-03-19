# Visual Polish (R16a + R16b) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refine FileScout's visual treatment: replace the hero header with a compact merged toolbar, fix faint sidebar contrast, and upgrade navigation icons to Segoe Fluent Icons system-wide.

**Architecture:** Two sequential slices. R16a is pure XAML edits (no C++ changes). R16b adds a C++ helper function and updates three C++ functions plus three XAML `SymbolIcon` elements. Both slices are independently buildable and visually verifiable.

**Tech Stack:** WinUI 3 / Windows App SDK 1.8, C++/WinRT, XAML, Segoe Fluent Icons, MSBuild Debug x64

**Spec:** `docs/superpowers/specs/2026-03-19-visual-polish-design.md`

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

## Slice R16a — Chrome + Sidebar Contrast (XAML only)

### Task 1: Remove hero card, promote toolbar to Row 0

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml:46-193`

The Grid currently has 4 `RowDefinition` entries. The hero card occupies lines 53–128 (Grid.Row="0"). The toolbar occupies lines 130–193 (Grid.Row="1"). The content is Grid.Row="2". A footer row is Grid.Row="3".

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

- [ ] **Step 4: Build and verify no errors**

Run the build command above. Expected: build succeeds (MSB8027/LNK4042 warnings are pre-existing and expected).

- [ ] **Step 5: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml
git commit -m "feat(ui): remove hero card, promote toolbar to row 0 (R16a step 1)"
```

---

### Task 2: Add app name + ALPHA badge to merged toolbar card

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml` (the toolbar card Border, now at Row 0)

The toolbar card currently starts with a `StackPanel` containing Back/Forward buttons. We need to prepend the app name and badge.

- [ ] **Step 1: Add app name, badge, and separator before the nav buttons**

Inside the toolbar card's `<Grid ColumnSpacing="12">`, the first `<StackPanel Orientation="Horizontal" Spacing="8">` contains Back and Forward. Prepend a new left section before it. Change the column layout to accommodate an extra identity column:

Replace the toolbar card's inner `Grid` column definitions:
```xml
<Grid.ColumnDefinitions>
    <ColumnDefinition Width="Auto" />   <!-- identity: name + badge + sep -->
    <ColumnDefinition Width="Auto" />   <!-- nav: back + forward -->
    <ColumnDefinition Width="*" />      <!-- address bar -->
    <ColumnDefinition Width="Auto" />   <!-- refresh -->
</Grid.ColumnDefinitions>
```

Add a new identity `StackPanel` as column 0:
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
            Background="{ThemeResource DividerStrokeColorDefaultBrush}"
            Margin="4,0" />
</StackPanel>
```

Shift existing nav `StackPanel` to `Grid.Column="1"`, address `StackPanel` to `Grid.Column="2"`, refresh `Button` to `Grid.Column="3"`.

- [ ] **Step 2: Fix CornerRadius on toolbar container Border**

The toolbar card `Border` currently has `CornerRadius="20"`. Change to `CornerRadius="14"`:
```xml
<Border Padding="12"
        CornerRadius="14"
        ...>
```

- [ ] **Step 3: Fix Back/Forward button HorizontalAlignment**

`ChromeToolbarButtonStyle` sets `HorizontalAlignment="Stretch"`. Inside the merged toolbar, this causes the buttons to expand to fill their parent. Override per-button:

```xml
<Button Style="{StaticResource ChromeToolbarButtonStyle}"
        HorizontalAlignment="Left"
        IsEnabled="False"
        ...>
```

Apply `HorizontalAlignment="Left"` to both the Back and Forward buttons.

- [ ] **Step 4: Build and verify**

Run build. Expected: no new errors. App should show compact toolbar with "FileScout" + ALPHA badge on the left.

- [ ] **Step 5: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml
git commit -m "feat(ui): merge app identity into toolbar card (R16a step 2)"
```

---

### Task 3: Fix sidebar background contrast

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml:202-206` (sidebar Border)

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

- [ ] **Step 2: Build and verify**

Run build. Expected: no new errors.

- [ ] **Step 3: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml
git commit -m "fix(ui): fix faint sidebar background - use CardBackgroundFillColorDefault (R16a step 3)"
```

---

### Task 4: Fix inactive pane border (XAML side)

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml:315-319` (RightPaneBorder)

- [ ] **Step 1: Give inactive pane a visible neutral border**

`RightPaneBorder` currently has `BorderBrush="Transparent"` and `BorderThickness="2"`. Change to a 1px neutral border so both panes are always framed:

```xml
<Border Grid.Column="2"
        x:Name="RightPaneBorder"
        CornerRadius="8"
        BorderThickness="1"
        BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}">
```

- [ ] **Step 2: Build and verify**

Run build.

- [ ] **Step 3: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml
git commit -m "fix(ui): inactive pane gets 1px neutral border instead of transparent (R16a step 4)"
```

---

## Slice R16b — Icon System (C++ + XAML)

### Task 5: Update active pane indicator to set thickness in C++

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp:3672-3682`

The existing `UpdateActivePaneIndicator()` sets brush but not thickness. Now that the XAML initial states have been changed (Task 4), the C++ toggle must manage both brush and thickness.

- [ ] **Step 1: Update UpdateActivePaneIndicator**

Replace the function body (lines 3672–3682) with:

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

Note: `Thickness` is `winrt::Microsoft::UI::Xaml::Thickness` — it is already in scope throughout `MainWindow.xaml.cpp`. No new include needed.

- [ ] **Step 2: Build and verify**

Run build. Expected: no new errors.

- [ ] **Step 3: Verify at runtime (manual)**

Launch the app. Click between the left and right panes. The active pane should show a thick blue border; the inactive pane a thin neutral border.

- [ ] **Step 4: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml.cpp
git commit -m "fix(ui): pane indicator sets thickness in addition to brush color (R16b step 1)"
```

---

### Task 6: Add BuildSidebarFontIcon helper

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp` (add after `BuildSidebarDriveGlyph`, around line 524)

This is the core building block for all subsequent icon work. Adding it as a named helper keeps the code DRY.

- [ ] **Step 1: Add the helper function**

Insert after `BuildSidebarDriveGlyph()` (after line 523):

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

Make sure `FontIcon` is accessible — add to pch.h if not already present:
```cpp
#include <winrt/Microsoft.UI.Xaml.Controls.h>  // FontIcon is in this header
```

(Check `pch.h` first — `FontIcon` may already be transitively included via existing TabView/ListView headers.)

- [ ] **Step 2: Build and verify**

Run build. Expected: no errors. The helper is not yet called, so no visual change.

- [ ] **Step 3: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml.cpp native/FileScout.Native/pch.h
git commit -m "feat(ui): add BuildSidebarFontIcon helper for Segoe Fluent glyph icons (R16b step 2)"
```

---

### Task 7: Replace drive letter monogram with HardDrive glyph

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp:512-523`

`BuildSidebarDriveGlyph()` currently renders a TextBlock with the drive letter ("C", "D"). Replace with the `EDA2` HardDrive glyph.

- [ ] **Step 1: Replace BuildSidebarDriveGlyph body**

Replace lines 512–523 with:

```cpp
winrt::Microsoft::UI::Xaml::FrameworkElement BuildSidebarDriveGlyph(
    std::filesystem::path const& /*path*/)
{
    // EDA2 = HardDrive glyph in Segoe Fluent Icons (Windows 11 only).
    return BuildSidebarFontIcon(L"\uEDA2", 18.0, { 0xFF, 0x80, 0x80, 0xC0 });
}
```

The `path` parameter is kept in the signature for callsite compatibility but is no longer used (the glyph is the same for all drives). Suppress the unused-parameter warning with a `/*path*/` comment as shown.

- [ ] **Step 2: Build and verify**

Run build. Expected: no errors.

- [ ] **Step 3: Verify at runtime**

Launch the app. The sidebar drive entries should now show the Windows hard drive icon instead of letter monograms.

- [ ] **Step 4: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml.cpp
git commit -m "feat(ui): replace drive letter monogram with Segoe Fluent HardDrive glyph EDA2 (R16b step 3)"
```

---

### Task 8: Replace sidebar place icons with Segoe Fluent glyphs

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp` (wherever `BuildSidebarGlyph` is called for places)

`BuildSidebarGlyph()` currently takes an `optional<IconBitmap>` (shell-extracted) and a fallback text. For the standard shell folders (Home, Desktop, Documents, Downloads, Pictures), we want to use Fluent glyphs instead of shell-extracted bitmaps.

- [ ] **Step 1: Find all callsites of BuildSidebarGlyph for Places**

Search `MainWindow.xaml.cpp` for `BuildSidebarGlyph`. The callsites for pinned/favorite places pass in a `FavoriteEntry` icon. Identify the entries that correspond to well-known shell folders (Home/Quick Access, Desktop, Documents, Downloads, Pictures).

- [ ] **Step 2: Add a glyph-lookup helper for known shell folders**

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

- [ ] **Step 3: Update BuildSidebarGlyph to try known-folder glyph first**

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

- [ ] **Step 4: Update all callsites of BuildSidebarGlyph**

The signature now takes `displayName` as its first argument. Search for `BuildSidebarGlyph(` in `MainWindow.xaml.cpp` and update each callsite.

`FavoriteEntry` (`Domain/FavoriteEntry.h`) has fields `display_name`, `path`, `is_drives_view`, `is_recycle_bin`, `is_pinned` — it has no `icon` or `fallback_text` fields. The icon and fallback label are computed locally at the callsite. The actual callsite pattern (around line 4041) is:

```cpp
// Before:
BuildSidebarGlyph(entryIcon, SidebarFavoriteFallbackLabel(entry))

// After:
BuildSidebarGlyph(entry.display_name, entryIcon, SidebarFavoriteFallbackLabel(entry))
```

`entryIcon` is a locally-computed `std::optional<IconBitmap>` (from `m_shellInterop.GetItemIcon`). `entry.display_name` is already in scope at the callsite.

- [ ] **Step 5: Build and verify**

Run build. Expected: no errors.

- [ ] **Step 6: Verify at runtime**

Launch app. Home, Desktop, Documents, Downloads, Pictures sidebar entries should show Segoe Fluent glyphs. Custom pinned folders fall back to shell-extracted bitmaps.

- [ ] **Step 7: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml.cpp
git commit -m "feat(ui): sidebar places use Segoe Fluent glyphs for known shell folders (R16b step 4)"
```

---

### Task 9: Replace toolbar SymbolIcon with FontIcon (XAML)

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml:150, 161, 187`

The Back, Forward, and Refresh toolbar buttons use `<SymbolIcon Symbol="Back/Forward/Refresh">`. Replace with `<FontIcon>` glyphs for pixel-perfect Segoe Fluent rendering.

**Note on line numbers:** The line numbers below are relative to the original file. After Task 1 removes the hero card block (lines 53–128), all subsequent XAML lines shift down by ~76. Use your editor's search for `Symbol="Back"`, `Symbol="Forward"`, and `Symbol="Refresh"` rather than relying on absolute line numbers.

- [ ] **Step 1: Replace Back button SymbolIcon (search: `Symbol="Back"`)**

```xml
<!-- Before -->
<SymbolIcon Symbol="Back" />

<!-- After -->
<FontIcon Glyph="&#xE76B;"
          FontFamily="Segoe Fluent Icons"
          FontSize="14" />
```

- [ ] **Step 2: Replace Forward button SymbolIcon (search: `Symbol="Forward"`)**

```xml
<!-- Before -->
<SymbolIcon Symbol="Forward" />

<!-- After -->
<FontIcon Glyph="&#xE76C;"
          FontFamily="Segoe Fluent Icons"
          FontSize="14" />
```

- [ ] **Step 3: Replace Refresh button SymbolIcon (search: `Symbol="Refresh"`)**

```xml
<!-- Before -->
<SymbolIcon Symbol="Refresh" />

<!-- After -->
<FontIcon Glyph="&#xE72C;"
          FontFamily="Segoe Fluent Icons"
          FontSize="14" />
```

- [ ] **Step 4: Build and verify**

Run build.

- [ ] **Step 5: Verify at runtime**

Launch app. Back, Forward, Refresh buttons in the merged toolbar should render clean Segoe Fluent glyphs.

- [ ] **Step 6: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml
git commit -m "feat(ui): toolbar Back/Forward/Refresh use Segoe Fluent FontIcon glyphs (R16b step 5)"
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
git commit -m "feat(ui): complete visual polish R16 (chrome + sidebar contrast + Fluent icons)

- Removes hero card, adds compact merged toolbar with identity row
- Fixes sidebar from faint LayerFillColorAlt to CardBackgroundFillColorDefault
- Replaces drive letter monograms with EDA2 HardDrive glyph
- Replaces known shell folder icons with Segoe Fluent glyphs
- Upgrades toolbar Back/Forward/Refresh to FontIcon glyphs
- Active pane now sets both border brush and thickness"
```
