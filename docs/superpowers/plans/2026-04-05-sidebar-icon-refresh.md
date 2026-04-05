# Sidebar Icon Refresh Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace flat monochrome Segoe Fluent glyphs in the sidebar with colorful shell bitmap icons (Quick Access) and custom XAML-shape drive icons (This PC), plus an orange warning bar when a drive is >85% full.

**Architecture:** Three independent edits in `MainWindow.xaml.cpp`. `SidebarIconInfo.cpp/.h` is deleted (its functions are dead once the glyph branch is removed). No new dependencies or asset files — drive icons are built from `Border` elements already available via `Microsoft.UI.Xaml.Controls`.

**Tech Stack:** C++20, WinUI 3 (Windows App SDK 1.8), C++/WinRT. Build via MSBuild — use the PowerShell wrapper in CLAUDE.md.

---

## File Map

| File | Action | What changes |
|---|---|---|
| `native/FileScout.Native/MainWindow.xaml.cpp` | Modify | Remove glyph branch; replace `BuildSidebarDriveGlyph` with `BuildDriveTypeIcon`; add warning-bar logic |
| `native/FileScout.Native/Domain/SidebarIconInfo.cpp` | **Delete** | Entire file — `KnownFolderGlyph` and `DriveTypeGlyph` are dead |
| `native/FileScout.Native/Domain/SidebarIconInfo.h` | **Delete** | Entire file |
| `native/FileScout.Native/FileScout.Native.vcxproj` | Modify | Remove both `SidebarIconInfo` entries |
| `native/FileScout.Tests/tests/SidebarIconInfoTests.cpp` | **Delete** | Tests for the deleted functions |
| `native/FileScout.Tests/FileScout.Tests.vcxproj` | Modify | Remove two `SidebarIconInfo` entries |

---

## Task 1: Remove glyph priority from `BuildSidebarGlyph` and delete dead code

No new tests to write — this deletes code. Verification is a clean build + all remaining tests pass.

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp:18` (remove include)
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp:1010-1043` (rewrite function)
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp:6705` (remove now-unused variable)
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp:6728,6818,6890` (remove first arg)
- Modify: `native/FileScout.Native/FileScout.Native.vcxproj:136,168`
- Modify: `native/FileScout.Tests/FileScout.Tests.vcxproj:135,158`
- Delete: `native/FileScout.Native/Domain/SidebarIconInfo.cpp`
- Delete: `native/FileScout.Native/Domain/SidebarIconInfo.h`
- Delete: `native/FileScout.Tests/tests/SidebarIconInfoTests.cpp`

- [ ] **Step 1: Remove `#include "Domain/SidebarIconInfo.h"` from `MainWindow.xaml.cpp` line 18**

  Delete this line:
  ```cpp
  #include "Domain/SidebarIconInfo.h"
  ```

- [ ] **Step 2: Rewrite `BuildSidebarGlyph` — remove the `displayName` parameter and the glyph branch**

  Replace lines 1010–1043 with:
  ```cpp
  winrt::Microsoft::UI::Xaml::FrameworkElement BuildSidebarGlyph(
      std::optional<::FileScoutNative::Domain::IconBitmap> const& icon,
      std::wstring const& fallbackText)
  {
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

- [ ] **Step 3: Remove the now-unused `knownFolderDisplayName` variable (line ~6705)**

  Delete this line (it is inside `BuildSidebarFavorites`, between the `for` loop opening and `if (!entry.path.empty())`):
  ```cpp
  auto const knownFolderDisplayName = entry.is_pinned ? std::wstring{} : entry.display_name;
  ```

- [ ] **Step 4: Update the three `BuildSidebarGlyph` call sites to match the new two-parameter signature**

  Line ~6728 — change:
  ```cpp
  BuildSidebarGlyph(knownFolderDisplayName, entryIcon, SidebarFavoriteFallbackLabel(entry)),
  ```
  to:
  ```cpp
  BuildSidebarGlyph(entryIcon, SidebarFavoriteFallbackLabel(entry)),
  ```

  Line ~6818 — change:
  ```cpp
  BuildSidebarGlyph(displayName, entryIcon, L"folder"),
  ```
  to:
  ```cpp
  BuildSidebarGlyph(entryIcon, L"folder"),
  ```

  Line ~6890 — change:
  ```cpp
  BuildSidebarGlyph(std::wstring{}, entryIcon, L"folder"),
  ```
  to:
  ```cpp
  BuildSidebarGlyph(entryIcon, L"folder"),
  ```

- [ ] **Step 5: Remove `SidebarIconInfo` entries from `FileScout.Native.vcxproj`**

  In `native/FileScout.Native/FileScout.Native.vcxproj`, delete:
  ```xml
  <ClInclude Include="Domain\SidebarIconInfo.h" />
  ```
  and:
  ```xml
  <ClCompile Include="Domain\SidebarIconInfo.cpp" />
  ```

- [ ] **Step 6: Remove `SidebarIconInfo` entries from `FileScout.Tests.vcxproj`**

  In `native/FileScout.Tests/FileScout.Tests.vcxproj`, delete:
  ```xml
  <ClCompile Include="..\FileScout.Native\Domain\SidebarIconInfo.cpp" />
  ```
  and:
  ```xml
  <ClCompile Include="tests\SidebarIconInfoTests.cpp" />
  ```

- [ ] **Step 7: Delete the three source files**

  ```bash
  rm "native/FileScout.Native/Domain/SidebarIconInfo.cpp"
  rm "native/FileScout.Native/Domain/SidebarIconInfo.h"
  rm "native/FileScout.Tests/tests/SidebarIconInfoTests.cpp"
  ```

- [ ] **Step 8: Build and run tests**

  ```powershell
  powershell -Command "cmd /c 'call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal'"
  ```
  Expected: build succeeds with no errors.

  ```bash
  native/bin/x64/Debug/Tests/FileScout.Tests.exe
  ```
  Expected: all tests pass (two `SidebarIconInfo` tests are gone; total count drops from 181 to 179).

- [ ] **Step 9: Commit**

  ```bash
  git add native/FileScout.Native/MainWindow.xaml.cpp \
          native/FileScout.Native/FileScout.Native.vcxproj \
          native/FileScout.Tests/FileScout.Tests.vcxproj
  git rm native/FileScout.Native/Domain/SidebarIconInfo.cpp \
         native/FileScout.Native/Domain/SidebarIconInfo.h \
         native/FileScout.Tests/tests/SidebarIconInfoTests.cpp
  git commit -m "feat(icons): use shell bitmaps for Quick Access sidebar icons"
  ```

---

## Task 2: Replace `BuildSidebarDriveGlyph` with `BuildDriveTypeIcon`

No unit tests — this is pure XAML rendering code. Verification is a clean build and visual inspection.

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp:1253-1259` (replace function)
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp:7000` (update call site)

- [ ] **Step 1: Replace `BuildSidebarDriveGlyph` with `BuildDriveTypeIcon`**

  Replace lines 1253–1259:
  ```cpp
  winrt::Microsoft::UI::Xaml::FrameworkElement BuildSidebarDriveGlyph(
      SidebarDriveUsage const& drive)
  {
      return BuildSidebarFontIcon(
          ::FileScoutNative::Domain::DriveTypeGlyph(drive.drive_type),
          16.0);
  }
  ```
  with:
  ```cpp
  winrt::Microsoft::UI::Xaml::FrameworkElement BuildDriveTypeIcon(uint32_t const driveType)
  {
      struct DriveColors
      {
          winrt::Windows::UI::Color body;
          winrt::Windows::UI::Color cap;
          winrt::Windows::UI::Color led;
      };

      DriveColors colors{};
      switch (driveType)
      {
      case DRIVE_REMOVABLE:
          colors = { {0xff,0x14,0x53,0x2d}, {0xff,0x16,0xa3,0x4a}, {0xff,0x86,0xef,0xac} };
          break;
      case DRIVE_REMOTE:
          colors = { {0xff,0x31,0x2e,0x81}, {0xff,0x7c,0x3a,0xed}, {0xff,0xc4,0xb5,0xfd} };
          break;
      default: // DRIVE_FIXED, DRIVE_RAMDISK, DRIVE_CDROM, DRIVE_UNKNOWN
          colors = { {0xff,0x1e,0x3a,0x5f}, {0xff,0x25,0x63,0xeb}, {0xff,0x93,0xc5,0xfd} };
          break;
      }

      auto canvas = winrt::Microsoft::UI::Xaml::Controls::Grid{};
      canvas.Width(18);
      canvas.Height(18);

      // Body — darker lower portion
      auto body = winrt::Microsoft::UI::Xaml::Controls::Border{};
      body.Width(15);
      body.Height(10);
      body.CornerRadius({ 2.5f, 2.5f, 2.5f, 2.5f });
      body.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{ colors.body });
      body.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Bottom);
      body.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
      body.Margin({ 0, 0, 0, 1 });

      // Cap — brighter upper portion
      auto cap = winrt::Microsoft::UI::Xaml::Controls::Border{};
      cap.Width(15);
      cap.Height(6);
      cap.CornerRadius({ 2.5f, 2.5f, 0, 0 });
      cap.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{ colors.cap });
      cap.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Top);
      cap.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
      cap.Margin({ 0, 2, 0, 0 });

      // LED dot — top-right accent circle
      auto led = winrt::Microsoft::UI::Xaml::Controls::Border{};
      led.Width(3);
      led.Height(3);
      led.CornerRadius({ 1.5f, 1.5f, 1.5f, 1.5f });
      led.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{ colors.led });
      led.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Top);
      led.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Right);
      led.Margin({ 0, 3, 2, 0 });

      canvas.Children().Append(body);
      canvas.Children().Append(cap);
      canvas.Children().Append(led);

      // USB plug nub — removable only: small block above the cap
      if (driveType == DRIVE_REMOVABLE)
      {
          auto nub = winrt::Microsoft::UI::Xaml::Controls::Border{};
          nub.Width(4);
          nub.Height(3);
          nub.CornerRadius({ 0.8f, 0.8f, 0, 0 });
          nub.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{ colors.cap });
          nub.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Top);
          nub.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
          canvas.Children().Append(nub);
      }

      return canvas;
  }
  ```

- [ ] **Step 2: Update the call site at line ~7000**

  Change:
  ```cpp
  auto iconTile = BuildSidebarTile(BuildSidebarDriveGlyph(drive), 18.0);
  ```
  to:
  ```cpp
  auto iconTile = BuildSidebarTile(BuildDriveTypeIcon(drive.drive_type), 18.0);
  ```

- [ ] **Step 3: Build**

  ```powershell
  powershell -Command "cmd /c 'call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal'"
  ```
  Expected: build succeeds with no errors.

- [ ] **Step 4: Commit**

  ```bash
  git add native/FileScout.Native/MainWindow.xaml.cpp
  git commit -m "feat(icons): replace drive glyph with custom XAML-shape drive type icons"
  ```

---

## Task 3: Warning bar and detail text color when drive is >85% full

No unit tests — rendering-only change. Verification is a clean build.

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp:7010-7034`

The current code builds `details` TextBlock (line 7010) with `Opacity(0.62)` and then — separately — builds the `ProgressBar` inside an `if (drive.has_usage)` block (line 7025). Both objects are already in scope when the `has_usage` block runs, so the foreground/opacity overrides can go inside that block.

- [ ] **Step 1: Add `isCritical` flag and override `details` appearance for critical drives**

  Change lines 7010–7034 from:
  ```cpp
  auto details = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
  details.Text(ToHString(BuildDriveUsageText(drive)));
  details.FontSize(11);
  details.Opacity(0.62);
  details.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);

  labelStack.Children().Append(title);
  labelStack.Children().Append(details);

  winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(iconTile, 0);
  winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(labelStack, 1);
  headerGrid.Children().Append(iconTile);
  headerGrid.Children().Append(labelStack);

  stack.Children().Append(headerGrid);
  if (drive.has_usage)
  {
      auto bar = winrt::Microsoft::UI::Xaml::Controls::ProgressBar{};
      bar.Minimum(0);
      bar.Maximum(100);
      bar.Value(drive.used_ratio * 100.0);
      bar.Height(4);
      bar.Background(LookupBrush(L"SubtleFillColorSecondaryBrush"));
      stack.Children().Append(bar);
  }
  ```
  to:
  ```cpp
  auto details = winrt::Microsoft::UI::Xaml::Controls::TextBlock{};
  details.Text(ToHString(BuildDriveUsageText(drive)));
  details.FontSize(11);
  details.Opacity(0.62);
  details.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);

  labelStack.Children().Append(title);
  labelStack.Children().Append(details);

  winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(iconTile, 0);
  winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(labelStack, 1);
  headerGrid.Children().Append(iconTile);
  headerGrid.Children().Append(labelStack);

  stack.Children().Append(headerGrid);
  if (drive.has_usage)
  {
      bool const isCritical = drive.used_ratio > 0.85;
      if (isCritical)
      {
          details.Opacity(1.0);
          details.Foreground(LookupBrush(L"SystemFillColorCriticalBrush"));
      }
      auto bar = winrt::Microsoft::UI::Xaml::Controls::ProgressBar{};
      bar.Minimum(0);
      bar.Maximum(100);
      bar.Value(drive.used_ratio * 100.0);
      bar.Height(4);
      bar.Background(LookupBrush(L"SubtleFillColorSecondaryBrush"));
      if (isCritical)
      {
          bar.Foreground(LookupBrush(L"SystemFillColorCriticalBrush"));
      }
      stack.Children().Append(bar);
  }
  ```

- [ ] **Step 2: Build and run tests**

  ```powershell
  powershell -Command "cmd /c 'call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal'"
  ```
  Expected: build succeeds with no errors.

  ```bash
  native/bin/x64/Debug/Tests/FileScout.Tests.exe
  ```
  Expected: all 179 tests pass.

- [ ] **Step 3: Commit**

  ```bash
  git add native/FileScout.Native/MainWindow.xaml.cpp
  git commit -m "feat(icons): show warning color on drive bar and detail text when >85% full"
  ```
