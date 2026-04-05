# FileScout File List Shell Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the dead `Properties` command, restore real executable icons, and make list-view `Type` and `Size` easier to scan without changing the overall file-list structure.

**Architecture:** This slice keeps the current shell/service boundaries intact. `MainWindow.xaml.cpp` continues to own selection rules and status messages, `ShellFileOperationService` becomes responsible for a reliable Properties-sheet launch path, and icon identity rules are centralized so both the shell bitmap cache and the WinUI image-source cache treat `.exe` files as per-item icons. List-row readability remains a XAML-only polish pass in list view.

**Tech Stack:** C++20, WinUI 3, C++/WinRT, Windows shell APIs, XAML, GoogleTest, MSBuild Debug x64

**Spec:** `docs/superpowers/specs/2026-04-01-file-list-shell-fixes-design.md`

**Build command:**
```powershell
powershell -Command "cmd /c 'call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal'"
```

**Test command:**
```powershell
native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no
```

---

## File Map

| File | What changes |
|---|---|
| `native/FileScout.Native/Shell/PropertiesLaunchSpec.h` | New tiny helper that defines the deterministic Properties launch spec used by both the service implementation and tests |
| `native/FileScout.Native/Shell/ShellFileOperationService.cpp` | Replace the generic `"properties"` shell verb path with a Properties-specific shell API call and keep failure propagation explicit |
| `native/FileScout.Native/Shell/ShellInteropService.cpp` | Use shared icon identity rules so `.exe` files use literal-path lookup and per-item bitmap cache keys |
| `native/FileScout.Native/Domain/IconCacheKey.h` | New shared helper for icon cache identity and literal-lookup rules used by both shell and UI row caches |
| `native/FileScout.Native/Domain/RowIconState.h` | Switch the WinUI row icon cache key to the shared helper so executable rows do not all reuse one cached image source |
| `native/FileScout.Native/MainWindow.xaml` | Apply the approved “A” list-row styling direction for `Type` and `Size` only |
| `native/FileScout.Tests/tests/ShellFileOperationServiceTests.cpp` | Add Properties-launch spec coverage and missing-path failure coverage |
| `native/FileScout.Tests/tests/ShellInteropServiceTests.cpp` | Add shared icon-cache-policy coverage for executables vs normal files |
| `native/FileScout.Tests/tests/RowIconStateTests.cpp` | Prove the WinUI row icon cache key no longer buckets all `.exe` rows together |

No `MainWindow.xaml.cpp` change is planned unless the styling pass uncovers a binding gap. No card/group template change is planned.

---

## Task 1: Make `Properties` use a reliable shell-native launch path

**Files:**
- Create: `native/FileScout.Native/Shell/PropertiesLaunchSpec.h`
- Modify: `native/FileScout.Native/Shell/ShellFileOperationService.cpp`
- Test: `native/FileScout.Tests/tests/ShellFileOperationServiceTests.cpp`

This task isolates the Properties launch behavior behind a tiny deterministic helper so the service can switch from a fragile shell verb to the Windows Properties-sheet API without losing testability.

- [ ] **Step 1: Write the failing tests**

Add these tests to `native/FileScout.Tests/tests/ShellFileOperationServiceTests.cpp`:

```cpp
TEST(ShellFileOperationDetail, BuildPropertiesLaunchSpec_UsesShopFilePathAndNoPage)
{
    auto const spec = FileScoutNative::Shell::detail::BuildPropertiesLaunchSpec(
        LR"(C:\Temp\sample.txt)");

    EXPECT_EQ(spec.object_type, SHOP_FILEPATH);
    EXPECT_EQ(spec.object_name, LR"(C:\Temp\sample.txt)");
    EXPECT_TRUE(spec.property_page.empty());
}

TEST_F(ShellFileOperationFixture, ShowPropertiesThrowsForMissingPath)
{
    auto const missing = m_root / L"missing.txt";
    EXPECT_THROW(m_svc.ShowProperties(nullptr, missing), std::runtime_error);
}
```

- [ ] **Step 2: Build to verify the new spec test fails**

Run:

```powershell
powershell -Command "cmd /c 'call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal'"
```

Expected:
- build fails in `ShellFileOperationServiceTests.cpp`
- compiler error mentions missing `FileScoutNative::Shell::detail::BuildPropertiesLaunchSpec`

- [ ] **Step 3: Add the tiny launch-spec helper**

Create `native/FileScout.Native/Shell/PropertiesLaunchSpec.h`:

```cpp
#pragma once

#include <filesystem>
#include <string>
#include <windows.h>
#include <shlobj_core.h>

namespace FileScoutNative::Shell::detail
{
    struct PropertiesLaunchSpec
    {
        DWORD object_type;
        std::wstring object_name;
        std::wstring property_page;
    };

    [[nodiscard]] inline PropertiesLaunchSpec BuildPropertiesLaunchSpec(
        std::filesystem::path const& path)
    {
        return PropertiesLaunchSpec{
            SHOP_FILEPATH,
            path.wstring(),
            {},
        };
    }
}
```

- [ ] **Step 4: Switch the service to `SHObjectProperties`**

Update `native/FileScout.Native/Shell/ShellFileOperationService.cpp`:

```cpp
#include "PropertiesLaunchSpec.h"

void ShellFileOperationService::ShowProperties(
    HWND const ownerWindow,
    std::filesystem::path const& path) const
{
    auto const launch = detail::BuildPropertiesLaunchSpec(path);
    auto const* propertyPage = launch.property_page.empty()
        ? nullptr
        : launch.property_page.c_str();

    if (!SHObjectProperties(
            ownerWindow,
            launch.object_type,
            launch.object_name.c_str(),
            propertyPage))
    {
        ThrowShellExecuteFailure(path, L"properties");
    }
}
```

- [ ] **Step 5: Rebuild and run the focused Properties tests**

Run:

```powershell
powershell -Command "cmd /c 'call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal'"
native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_filter=ShellFileOperationDetail.*:ShellFileOperationFixture.ShowPropertiesThrowsForMissingPath --gtest_color=no
```

Expected:
- build succeeds
- the two focused tests pass

- [ ] **Step 6: Commit**

```bash
git add native/FileScout.Native/Shell/PropertiesLaunchSpec.h native/FileScout.Native/Shell/ShellFileOperationService.cpp native/FileScout.Tests/tests/ShellFileOperationServiceTests.cpp
git commit -m "fix(shell): use native properties sheet launch"
```

---

## Task 2: Unify executable icon identity across shell and UI caches

**Files:**
- Create: `native/FileScout.Native/Domain/IconCacheKey.h`
- Modify: `native/FileScout.Native/Shell/ShellInteropService.cpp`
- Modify: `native/FileScout.Native/Domain/RowIconState.h`
- Test: `native/FileScout.Tests/tests/ShellInteropServiceTests.cpp`
- Test: `native/FileScout.Tests/tests/RowIconStateTests.cpp`

This is the real `.exe` fix. The shell bitmap cache and the WinUI image-source cache must both stop treating all executables as one shared extension bucket.

- [ ] **Step 1: Write the failing cache-policy tests**

Add these tests to `native/FileScout.Tests/tests/ShellInteropServiceTests.cpp`:

```cpp
TEST(IconCachePolicy, ExecutablesUsePerItemKeysAndLiteralLookup)
{
    auto const first = std::filesystem::path{ LR"(C:\Apps\Alpha.EXE)" };
    auto const second = std::filesystem::path{ LR"(C:\Apps\Beta.exe)" };

    EXPECT_TRUE(FileScoutNative::Domain::UsesPerItemIconIdentity(first, false));
    EXPECT_TRUE(FileScoutNative::Domain::UsesLiteralIconLookup(first, false));
    EXPECT_EQ(
        FileScoutNative::Domain::BuildFileIconCacheKey(first, false),
        LR"(__item__:c:\apps\alpha.exe)");
    EXPECT_EQ(
        FileScoutNative::Domain::BuildFileIconCacheKey(second, false),
        LR"(__item__:c:\apps\beta.exe)");
}

TEST(IconCachePolicy, DocumentsAndFoldersKeepSharedBuckets)
{
    EXPECT_EQ(
        FileScoutNative::Domain::BuildFileIconCacheKey(LR"(C:\Temp\alpha.TXT)", false),
        L".txt");
    EXPECT_EQ(
        FileScoutNative::Domain::BuildFileIconCacheKey(LR"(C:\Temp\folder)", true),
        L"__folder__");
    EXPECT_EQ(
        FileScoutNative::Domain::BuildFileIconCacheKey(LR"(C:\Temp\README)", false),
        L"__file__");
}
```

Update `native/FileScout.Tests/tests/RowIconStateTests.cpp` with the executable expectation:

```cpp
TEST(RowIconState, RowIconCacheKey_UsesPerItemIdentityForExecutables)
{
    EXPECT_EQ(
        RowIconCacheKey(MakeRowIconItem(LR"(C:\Apps\Alpha.EXE)")),
        LR"(__item__:c:\apps\alpha.exe)");
    EXPECT_EQ(
        RowIconCacheKey(MakeRowIconItem(LR"(C:\Apps\Beta.exe)")),
        LR"(__item__:c:\apps\beta.exe)");
}
```

- [ ] **Step 2: Build to verify the new icon-policy tests fail**

Run:

```powershell
powershell -Command "cmd /c 'call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal'"
```

Expected:
- build fails because `UsesPerItemIconIdentity`, `UsesLiteralIconLookup`, and `BuildFileIconCacheKey` do not exist yet

- [ ] **Step 3: Add the shared icon identity helper**

Create `native/FileScout.Native/Domain/IconCacheKey.h`:

```cpp
#pragma once

#include <filesystem>
#include <string>

namespace FileScoutNative::Domain
{
    [[nodiscard]] inline std::wstring LowercaseCopy(std::wstring value)
    {
        for (auto& ch : value)
        {
            ch = static_cast<wchar_t>(::towlower(ch));
        }
        return value;
    }

    [[nodiscard]] inline bool UsesPerItemIconIdentity(
        std::filesystem::path const& path,
        bool const isFolder)
    {
        if (isFolder)
        {
            return false;
        }

        return LowercaseCopy(path.extension().wstring()) == L".exe";
    }

    [[nodiscard]] inline bool UsesLiteralIconLookup(
        std::filesystem::path const& path,
        bool const isFolder)
    {
        return isFolder
            ? (path == path.root_path() && !path.root_name().empty())
            : UsesPerItemIconIdentity(path, false);
    }

    [[nodiscard]] inline std::wstring BuildFileIconCacheKey(
        std::filesystem::path const& path,
        bool const isFolder)
    {
        if (isFolder)
        {
            return L"__folder__";
        }

        auto const extension = LowercaseCopy(path.extension().wstring());
        if (extension.empty())
        {
            return L"__file__";
        }

        if (UsesPerItemIconIdentity(path, false))
        {
            return L"__item__:" + LowercaseCopy(path.lexically_normal().wstring());
        }

        return extension;
    }
}
```

- [ ] **Step 4: Move both caches onto the shared helper**

Update `native/FileScout.Native/Domain/RowIconState.h`:

```cpp
#include "IconCacheKey.h"

[[nodiscard]] inline std::wstring RowIconCacheKey(ShellItemRef const& item)
{
    return BuildFileIconCacheKey(item.path, item.is_folder);
}
```

Update `native/FileScout.Native/Shell/ShellInteropService.cpp`:

```cpp
#include "Domain/IconCacheKey.h"

FileScoutNative::Domain::IconBitmap ShellInteropService::GetIconForItem(
    std::filesystem::path const& path,
    bool const isFolder) const
{
    auto const cacheKey = ::FileScoutNative::Domain::BuildFileIconCacheKey(path, isFolder);
    // existing cache lookup remains the same

    std::wstring lookupName;
    DWORD fileAttributes = FILE_ATTRIBUTE_NORMAL;
    bool useLiteralPath = false;

    if (isFolder)
    {
        if (::FileScoutNative::Domain::UsesLiteralIconLookup(path, true))
        {
            lookupName = path.wstring();
            useLiteralPath = true;
        }
        else
        {
            lookupName = L"folder";
            fileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        }
    }
    else if (::FileScoutNative::Domain::UsesLiteralIconLookup(path, false))
    {
        lookupName = NormalizeShellLookupPath(path).wstring();
        useLiteralPath = true;
    }
    else
    {
        auto extension = path.extension().wstring();
        lookupName = extension.empty() ? L"file" : L"file" + extension;
    }

    auto icon = ExtractShellIconBitmap(lookupName, fileAttributes, useLiteralPath)
        .value_or(FileScoutNative::Domain::IconBitmap{});
    // existing cache insert remains the same
}
```

- [ ] **Step 5: Rebuild and run the focused icon tests**

Run:

```powershell
powershell -Command "cmd /c 'call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal'"
native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_filter=IconCachePolicy.*:RowIconState.* --gtest_color=no
```

Expected:
- build succeeds
- focused icon cache tests pass

- [ ] **Step 6: Commit**

```bash
git add native/FileScout.Native/Domain/IconCacheKey.h native/FileScout.Native/Domain/RowIconState.h native/FileScout.Native/Shell/ShellInteropService.cpp native/FileScout.Tests/tests/ShellInteropServiceTests.cpp native/FileScout.Tests/tests/RowIconStateTests.cpp
git commit -m "fix(icons): keep executable icons per item"
```

---

## Task 3: Apply the approved list-view metadata polish

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml`

This is a restrained XAML-only pass. Keep the current columns and bindings, but make `Type` and `Size` more readable in list view using the approved “A” direction.

- [ ] **Step 1: Edit the list-row template only**

Update the `ListRowTemplate` section in `native/FileScout.Native/MainWindow.xaml` so `Type` gets a subtle pill treatment and `Size` gets slightly stronger weight/contrast:

```xml
<Border Grid.Column="2"
        HorizontalAlignment="Left"
        Padding="6,2"
        CornerRadius="6"
        Background="{ThemeResource LayerFillColorAltBrush}"
        Opacity="0.88">
    <TextBlock Text="{x:Bind TypeLabel}"
               FontSize="11.5"
               Foreground="{ThemeResource TextFillColorPrimaryBrush}"
               VerticalAlignment="Center"
               TextTrimming="CharacterEllipsis" />
</Border>

<TextBlock Grid.Column="3"
           Text="{x:Bind SizeText}"
           FontSize="12"
           FontWeight="SemiBold"
           Foreground="{ThemeResource TextFillColorPrimaryBrush}"
           Opacity="0.78"
           VerticalAlignment="Center"
           TextTrimming="CharacterEllipsis" />
```

Keep these existing behaviors unchanged:
- same `Grid.ColumnDefinitions`
- same `TypeLabel` / `SizeText` bindings
- same `DateText` position and emphasis
- same selection fill/outline behavior

- [ ] **Step 2: Build after the XAML change**

Run:

```powershell
powershell -Command "cmd /c 'call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal'"
```

Expected:
- build succeeds
- no new XAML compile errors

- [ ] **Step 3: Manual smoke-check the list row**

Validate in the running app:
- `Type` is easier to read but still visually calmer than `Name`
- `Size` reads stronger than before without turning into a badge
- long type names still truncate cleanly
- selected rows remain readable in both light and dark themes

- [ ] **Step 4: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml
git commit -m "style(list): improve type and size visibility"
```

---

## Task 4: Run full verification and confirm the slice end-to-end

**Files:**
- No new files required unless implementation uncovers a tiny follow-up fix

This task proves the slice against the spec before any merge or PR work.

- [ ] **Step 1: Run the full build**

Run:

```powershell
powershell -Command "cmd /c 'call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal'"
```

Expected:
- exit code `0`
- no new warnings beyond the known baseline

- [ ] **Step 2: Run the full test suite**

Run:

```powershell
native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no
```

Expected:
- all tests pass

- [ ] **Step 3: Manually verify `Properties`**

In the app:
- right-click a single file and choose `Properties`
- right-click a single folder and choose `Properties`
- select multiple items and confirm the status bar still says Properties needs a single selected item

Expected:
- valid single selections open the real Windows Properties sheet
- invalid selections stay guarded by the existing status message

- [ ] **Step 4: Manually verify `.exe` icons**

Use a folder with distinct Windows executables, for example a temp folder containing copies of:
- `C:\Windows\System32\notepad.exe`
- `C:\Windows\System32\mspaint.exe`
- `C:\Windows\System32\write.exe`

Expected:
- each row shows its own app icon
- rows no longer all collapse to the same generic file icon

- [ ] **Step 5: Manually verify list-view readability**

In both light and dark theme:
- confirm `Type` and `Size` are more legible in list view
- confirm `Date Modified` remains aligned and visually dominant over the polished metadata
- confirm grouped and card presentations remain unchanged

- [ ] **Step 6: Commit any final follow-up fix if needed**

If verification required one last tiny fix:

```bash
git add native/FileScout.Native native/FileScout.Tests
git commit -m "fix(shell): finalize file list shell polish"
```

If no follow-up fix was needed, skip this commit.

---

## Guardrails

- Do not broaden Properties behavior into multi-select support in this slice
- Do not rework directory enumeration to fetch per-item shell metadata for all files
- Do not fix `.lnk`, `.ico`, or other file types unless they become blockers while implementing `.exe`
- Do not touch card/group row templates during the metadata polish
- Do not claim the `.exe` fix is complete unless both the shell bitmap cache and the row image-source cache use the new identity policy

---

## Final Verification Checklist

- [ ] `Properties` uses a Properties-sheet API path instead of the generic `"properties"` shell verb
- [ ] Missing-path Properties calls fail cleanly and surface through the existing status pattern
- [ ] `.exe` files use per-item cache identity in both shell and row caches
- [ ] Normal files still use shared extension buckets
- [ ] Full build passes
- [ ] Full test suite passes
- [ ] Manual verification confirms distinct executable icons and working Properties sheets
- [ ] List-view `Type` and `Size` are easier to read without changing the overall file-list layout
