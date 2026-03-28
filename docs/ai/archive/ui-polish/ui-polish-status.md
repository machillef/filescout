# UI Polish — Status

Initiative: **ui-polish**
Size: **Medium**
Started: 2026-03-25

---

| Slice | Status | Notes |
|---|---|---|
| 1. Sidebar Icon Priority Fix | Complete | Quick Access known folders now prefer Fluent glyphs, This PC drives use drive-type glyphs, and pinned bookmarks keep shell bitmaps. |
| 2. ThemeDefinition + ThemeService | Complete | Added 3 built-in palettes and persisted canonical theme IDs to `theme.txt` with LOCALAPPDATA fallback. |
| 3. Runtime Theme Application | Complete | Saved theme now loads into `App.Resources().MergedDictionaries()` before window creation, and the app can replace the active theme dictionary at runtime. |
| 4. Title Bar Theme Dropdown | Complete | Live theme switching now repaints broad pane/file-view surfaces in place, grouped mode renders visible section headers, and pane background context menus are now wired to the actual scroll surface. |

---

## Slice 1: Sidebar Icon Priority Fix
Status: Complete
Last updated: 2026-03-26

### What was implemented
- Added `Domain/SidebarIconInfo.h` and `Domain/SidebarIconInfo.cpp` with pure known-folder and drive-type glyph mappings.
- Carried Win32 `drive_type` through `ShellInteropService::EnumerateDrives()` via `ShellItemRef`.
- Updated `MainWindow.xaml.cpp` so Quick Access known folders prefer Fluent glyphs, This PC uses drive-type glyphs, and pinned bookmarks continue to use shell bitmaps.
- Added `native/FileScout.Tests/tests/SidebarIconInfoTests.cpp` and wired the new production/test sources into both `.vcxproj` files.

### What was validated
- `cmd /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS (only exempt `MSB8027` and `LNK4042` warnings remain)
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — PASS (`[  PASSED  ] 113 tests`)

### What remains unverified
- Manual app check that Quick Access shows distinct glyphs for known folders like Desktop, Documents, Downloads, Pictures, Music, and Videos.
- Manual app check that This PC shows different icons for fixed, removable, and network drives.
- Manual app check that a user-pinned bookmark named like a known folder still renders the shell bitmap instead of the built-in glyph.

### Blockers
- None

### Next recommended step
- Start Slice 2: add the theme domain model, built-in palettes, and persisted theme selection service.

---

## Slice 2: ThemeDefinition + ThemeService (Domain Layer)
Status: Complete
Last updated: 2026-03-26

### What was implemented
- Added `native/FileScout.Native/Domain/ThemeDefinition.h` with the 10-token theme model and built-in `DefaultDark`, `TokyoNight`, and `CatppuccinMocha` factories.
- Added `native/FileScout.Native/Domain/ThemeService.h` and `native/FileScout.Native/Domain/ThemeService.cpp` with canonical theme lookup, UTF-8 `theme.txt` persistence, `%LOCALAPPDATA%\FileScoutNative\theme.txt` storage, and current-directory fallback when `LOCALAPPDATA` is unavailable.
- Added `native/FileScout.Tests/tests/ThemeServiceTests.cpp` covering palette tokens, save/load round-trip, missing-file fallback, unknown-id fallback, first-run directory creation, and current-directory fallback.
- Wired the new production/test files into `native/FileScout.Native/FileScout.Native.vcxproj` and `native/FileScout.Tests/FileScout.Tests.vcxproj`.

### What was validated
- `cmd /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS (only exempt `MSB8027` and `LNK4042` warnings remain)
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — PASS (`[  PASSED  ] 121 tests`)

### What remains unverified
- No user-facing UI is wired in this slice yet; runtime theme application and visual validation are deferred to Slice 3.

### Blockers
- None

### Next recommended step
- Start Slice 3: build the WinUI resource-dictionary application path so the saved theme loads at startup and can swap live at runtime.

---

## Slice 3: Runtime Theme Application
Status: Complete
Last updated: 2026-03-26

### What was implemented
- Added `BuildThemeResourceEntries()` in `native/FileScout.Native/Domain/ThemeService.h` and `native/FileScout.Native/Domain/ThemeService.cpp` so the domain layer owns the exact 10 theme-token to WinUI resource-key mappings.
- Added `ThemeService.BuildThemeResourceEntries_ReturnsTenMappedBrushKeys` in `native/FileScout.Tests/tests/ThemeServiceTests.cpp` to lock the resource-key count and representative key-to-color mappings.
- Extended `native/FileScout.Native/App.xaml.h` and `native/FileScout.Native/App.xaml.cpp` with `App::ApplyTheme(std::wstring_view)`, an app-owned theme `ResourceDictionary`, and helpers that convert ARGB tokens into `SolidColorBrush` instances.
- Updated `App::OnLaunched()` to load the saved theme ID before `MainWindow` creation, merge the theme dictionary into `App.Resources().MergedDictionaries()`, and replace the previous merged dictionary on subsequent `ApplyTheme()` calls so later UI wiring can swap themes live without stacking duplicate overrides.

### What was validated
- `cmd /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS (only exempt `MSB8027` and `LNK4042` warnings remain)
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — PASS (`[  PASSED  ] 122 tests`)

### What remains unverified
- Manual app launch check that the saved theme is already visible on first paint, especially for `tokyo-night` versus `default-dark`.
- Manual app check that the overridden brushes are actually applied across the page background, cards/layers, text, borders, hover states, sidebar, file list, and tabs.
- Manual app check that repeated theme changes do not accumulate stale merged dictionaries once Slice 4 wires the title-bar selector into `App::ApplyTheme()`.

### Blockers
- None

### Next recommended step
- Start Slice 4: add the title-bar theme dropdown, persist user selection through `ThemeService`, and route the selection to `App::ApplyTheme()` for live runtime switching.

---

## Slice 4: Title Bar Theme Dropdown
Status: Complete
Last updated: 2026-03-26

### What was implemented
- Added `ChromeDropDownButtonStyle` and a new `ThemeDropDownButton` to the title-bar toolbar in `native/FileScout.Native/MainWindow.xaml`, positioned to the right of search and styled to match the existing chrome.
- Extended `native/FileScout.Native/MainWindow.xaml.h` and `native/FileScout.Native/MainWindow.xaml.cpp` with theme-dropdown setup, current-theme tracking, click handlers, and a code-built content/flyout path that shows the active theme name and accent swatches for all three palettes.
- Routed theme selection through `App::ApplyTheme()` and `ThemeService::SaveThemeId()` so changes apply instantly and persist across restart, and added a targeted refresh path for code-built chrome (`BuildSidebarFavorites()`, both sort headers, active-pane indicator, and the open command palette) so live theme swaps update surfaces that were created with `LookupBrush(...)`.
- Updated title-bar non-client passthrough handling to exclude the new dropdown from the drag region, and exposed `App::Current()` in `native/FileScout.Native/App.xaml.h` / `native/FileScout.Native/App.xaml.cpp` so `MainWindow` can safely reach the running app instance when applying a theme.
- Added `ThemeService.GetAllThemes_ReturnsThreeThemesInDropdownOrder` in `native/FileScout.Tests/tests/ThemeServiceTests.cpp` to lock the selector’s built-in theme list and display order.

### What was validated
- `cmd /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS (only exempt `MSB8027` and `LNK4042` warnings remain)
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — PASS (`[  PASSED  ] 123 tests`)

### What remains unverified
- Manual app check that the dropdown is visible and clickable inside the custom title bar, rather than being swallowed by drag behavior.
- Manual app check that each theme applies instantly across the title bar, sidebar, sort headers, file list, and tabs, and that the current theme indicator updates after each selection.
- Manual app restart check that the selected theme persists and reappears as the current choice in the dropdown on first launch.

### Blockers
- None

### Next recommended step
- Add a retro entry and archive the `docs/ai/ui-polish-*` files if the remaining non-theme view-mode UX findings are being moved into a separate follow-on initiative.

### Bug Report (2026-03-26)
Reported by: User report
Description: On Tokyo Night and Catppuccin Mocha, the sidebar known-folder glyphs are too low-contrast to read clearly, and switching themes barely changes the visible pane/card surfaces so the app feels almost unchanged outside accent edges.

### Fix: Theme Surface Coverage + Sidebar Glyph Contrast (2026-03-26)
Reported by: User report
Root cause:
- `BuildThemeResourceEntries()` did not override `CardBackgroundFillColorSecondaryBrush` or `LayerFillColorAltBrush`, so several high-visibility pane/card surfaces kept WinUI’s default dark tokens instead of the selected palette.
- `BuildSidebarFontIcon()` rendered sidebar glyphs with `TextFillColorSecondaryBrush`, which made known-folder and drive glyphs too muted on Tokyo Night and Catppuccin Mocha.
Fix:
- Extended the theme resource map to include `CardBackgroundFillColorSecondaryBrush` and `LayerFillColorAltBrush`, both sourced from each palette’s `surface_alt` token.
- Switched sidebar known-folder and drive glyphs to `TextFillColorPrimaryBrush` for stronger contrast while keeping the bookmark unpin icon on the secondary text brush.
- Added a targeted regression test that locks the expanded theme resource-key set and validates representative Tokyo Night mappings for the newly covered surface brushes.
Validated:
- `cmd /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS (only exempt `MSB8027` and `LNK4042` warnings remain)
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=ThemeService.BuildThemeResourceEntries_ReturnsExtendedMappedBrushKeys` — PASS
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — PASS (`[  PASSED  ] 123 tests`)
Manual retest:
- Relaunch the app and compare Default Dark, Tokyo Night, and Catppuccin Mocha side by side.
- Confirm Quick Access and This PC glyphs remain readable on all themes.
- Confirm the file cards, detail/selection panes, and other secondary surface panels now shift with the selected palette rather than only accent outlines changing.

### Manual Acceptance Findings (2026-03-26)
Reported by: User report
Findings:
- Theme switching still reads as incomplete: the most visible change is the outer pane border, while the broader file-view background and overall pane coloration still look nearly unchanged across themes.
- In at least one file view, switching between `Group`, `Cards`, and `List` can remove item icons entirely.
- `Group` and `Cards` currently appear visually identical or close enough that users will read one of them as broken.
- `List` view loses per-item separation, making rows harder to distinguish than in the card-style view.
- The pane provides too little reliable empty hit area for right-clicking background actions such as creating a new folder.
- Hovering near the top of the pane after `Alt+Tab` can surface an unexplained `Ctrl+C` tooltip.
- Clicking `Type` twice can trigger a Visual C++ runtime assertion (`invalid comparator`) and crash the app.

Suggested handling:
- Keep `ui-polish` open and use `codex-fix-bugs` for the remaining theme-coverage issue because it is directly within this initiative’s acceptance criteria.
- Treat the `Type` sort crash as the highest-priority follow-up bug because it is a reproducible app crash.
- Bootstrap a separate initiative after that for file-view mode clarity and pane interaction UX if the team wants those grouped and designed together rather than fixed opportunistically.

### Bug Report (2026-03-26)
Reported by: User report
Description: Clicking the `Type` column twice can trigger a Visual C++ runtime assertion (`invalid comparator`) and crash the app.

### Fix: Type sort descending comparator crash (2026-03-26)
Reported by: User report
Root cause:
- `MainWindow::SortItems()` inverted the ascending comparison with `!ascending` for descending order, so equal `Type` values compared as `true` instead of `false`, violating strict weak ordering and tripping the debug STL comparator assertion.
Fix:
- Added `native/FileScout.Native/Domain/ShellItemSort.h` as a pure sort helper that computes a three-way comparison for `Name`, `Type`, `Size`, and `Date Modified`, including deterministic tie-breaks.
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` so `MainWindow::SortItems()` maps its UI enums into the pure helper instead of carrying its own invalid descending comparator.
- Added `native/FileScout.Tests/tests/ShellItemSortTests.cpp` and wired it into `native/FileScout.Tests/FileScout.Tests.vcxproj` to lock the equal-`Type` descending case and the equal-item comparator contract.
Tests added:
- `ShellItemSort.TypeDescending_UsesNameTieBreakForEqualTypes`
- `ShellItemSort.DescendingComparator_RemainsFalseForEqualItems`
Validated:
- `cmd /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS (only exempt `MSB8027` and `LNK4042` warnings remain)
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=ShellItemSort.*` — PASS
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — PASS (`[  PASSED  ] 125 tests`)

### Bug Report (2026-03-26)
Reported by: User report
Description: Switching the file view between `Group`, `Cards`, or `List` can rebuild the pane without restoring row icons, leaving every item with an empty icon slot.

### Fix: View toggle row rebuild drops item icons (2026-03-26)
Reported by: User report
Root cause:
- `RebuildAllTabRowsForPane()` recreated every `FileRowViewModel` with `MakeFileRowViewModel(item)`, but that factory does not populate `IconSource`.
- The async icon backfill path only runs during directory loads, so view-mode and grouping rebuilds discarded already-cached icons without reapplying them.
Fix:
- Added `native/FileScout.Native/Domain/RowIconState.h` with pure helpers for icon cache keys and identifying which rows can be rehydrated from the existing extension/folder icon cache.
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` so `RebuildAllTabRowsForPane()` reapplies cached icons immediately after rebuilding rows, preserving icons across `Group`, `Cards`, and `List` toggles without waiting for a reload.
- Added `native/FileScout.Tests/tests/RowIconStateTests.cpp` and wired it into `native/FileScout.Tests/FileScout.Tests.vcxproj` to lock the cache-key normalization and rehydration-index behavior.
Tests added:
- `RowIconState.CachedRowIconIndices_FindsRowsThatShouldBeRehydrated`
- `RowIconState.RowIconCacheKey_NormalizesExtensionsAndFolders`
Validated:
- `cmd /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS (only exempt `MSB8027` and `LNK4042` warnings remain)
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=RowIconState.*` — PASS
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — PASS (`[  PASSED  ] 127 tests`)
Manual retest:
- Switch both panes through `Group`, `Cards`, and `List` repeatedly without reloading the directory.
- Confirm folders and files keep their icons in every mode and after returning to the original mode.
- Confirm this still holds after sorting or toggling grouping on the same pane.

### Bug Report (2026-03-26)
Reported by: User report
Description: Changing themes from the title-bar dropdown mostly updated accent edges and a few rebuilt controls, but many already-rendered pane and file-view surfaces stayed on the old palette until restart.

### Fix: Live theme switching now updates existing pane surfaces (2026-03-26)
Reported by: User report
Root cause:
- `App::ApplyTheme()` removed the old merged theme dictionary and appended a brand-new one on each selection.
- Cold launch worked because controls resolved the latest brushes during creation, but many already-rendered WinUI surfaces retained references to brush instances from the old dictionary, so live switching looked incomplete until restart.
Fix:
- Updated `native/FileScout.Native/App.xaml.cpp` so the app creates its merged theme dictionary once and then mutates the existing `SolidColorBrush` instances in place on later theme changes instead of replacing the entire dictionary object.
- Kept the existing startup path intact, so first-paint theming still uses the saved theme while runtime switching now repaints already-instantiated controls that were bound to the previous brush objects.
Tests added:
- None. No practical automated regression test exists in the current suite for live WinUI `ThemeResource` invalidation across already-rendered controls; verification used the existing build/test gates plus direct UI automation.
Validated:
- `cmd /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS (only exempt `MSB8027` and `LNK4042` warnings remain)
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — PASS (`[  PASSED  ] 127 tests`)
- Manual UI verification with `filescout-ui-automation` — PASS: cold launch into `Tokyo Night` showed fully themed pane surfaces, and a live switch from `Tokyo Night` to `Catppuccin Mocha` repainted the title bar, sidebar, pane backgrounds, and file cards without restart.
Manual retest:
- Switch between all 3 themes from the title-bar dropdown without restarting.
- Confirm the title bar, sidebar, pane bodies, tab strip area, and file cards all repaint immediately.
- Restart once and confirm the last selected theme still loads on first paint.

### Bug Report (2026-03-26)
Reported by: User report
Description: The file pane offers little or no reliable empty-space hit target for right-clicking background actions such as `New folder`, even though the item context menu exposes those commands.

### Fix: Empty pane right-click now opens the background context menu (2026-03-26)
Reported by: User report
Root cause:
- `MainWindow::CreateTabListView()` wired `RightTapped` on the pane `ListView`, but the control itself did not expose a hit-testable background surface.
- As a result, right-clicks on whitespace between or below items often never reached `FileRowsListView_RightTapped()`, so the background context menu could not be opened reliably.
Fix:
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` so each pane `ListView` gets a transparent background brush during construction, making the control hit-testable across empty space while preserving the current appearance.
Tests added:
- None. There is no practical automated regression test in the current suite for WinUI pointer hit-testing on empty `ListView` surface area; verification relies on the standard build/test gates plus manual UI retest.
Validated:
- `cmd /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS (only exempt `MSB8027` and `LNK4042` warnings remain)
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — PASS (`[  PASSED  ] 127 tests`)
Manual retest:
- Right-click whitespace below or between items in both panes.
- Confirm the background context menu opens and includes actions such as `New folder` and `Refresh`.
- Confirm item right-click still opens the item-aware menu and selection behavior remains unchanged.

### Bug Report (2026-03-26)
Reported by: User report
Description: The empty-pane background menu still does not open reliably, the stray `Ctrl+C` badge can still appear near the pane header, and selected rows are no longer visually obvious.

### Fix: Pane background hit target + selection visibility regression (2026-03-26)
Reported by: User report
Root cause:
- Blank-space context handling was split across inner list surfaces (`ScrollViewer.RightTapped`) and the tab-content host, so pane-level empty space did not have a single stable owner for background context requests.
- The row templates had lost a strong explicit selected-state overlay, making selection too subtle against the dark card/list surfaces.
- Keyboard accelerators were still eligible for placement hints at the root visual, which allowed the `Ctrl+C` popup badge to surface independently of the actual menu.
Fix:
- Updated `native/FileScout.Native/MainWindow.xaml` and `native/FileScout.Native/Domain/FileRowViewModel.h` so both card and list row templates render a visible accent-backed fill/outline when `IsSelected` is true.
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` so the pane border owns background `ContextRequested`, the `ListView` owns item `RightTapped`, and the temporary trace-only event plumbing was removed.
- Kept the root keyboard accelerators in hidden placement mode so commands still work without leaking the floating `Ctrl+C` badge.
Tests added:
- None. The regressions are WinUI interaction/visual issues with no practical automated coverage in the current suite.
Validated:
- `cmd /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS (only exempt `MSB8027` and `LNK4042` warnings remain)
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — PASS (`[  PASSED  ] 127 tests`)
- Manual UI automation spot-check — partial PASS: rebuilt app preserved the stronger selected-row visuals and no detached `Ctrl+C` badge was observed; the current automation harness could not deterministically synthesize a blank-space right-click, so background-menu verification remains a direct human retest item.
Manual retest:
- Select a row in both `Cards` and `List` views and confirm the selection is clearly visible.
- Hover and switch focus around the pane header after selecting items and confirm no stray `Ctrl+C` badge appears.
- Right-click pane whitespace, row gaps, and the area below the last item; confirm the background menu opens and still includes `New folder` / `Refresh`.

### Bug Report (2026-03-26)
Reported by: User report
Description: `List` view is still hard to scan because the rows blend into the pane background and do not read as distinct entries the way `Cards` does.

### Fix: List view row separation and readability (2026-03-26)
Reported by: User report
Root cause:
- `ListRowTemplate` still rendered most of its content on an almost flat transparent surface, with no row background, no stroke, and no icon tile anchor.
- That made the denser list mode visually collapse into the pane background, especially on dark themes where the row boundaries needed a subtle but explicit structure.
Fix:
- Updated `native/FileScout.Native/MainWindow.xaml` so list rows now use a compact bordered surface with `LayerFillColorAltBrush`, tighter but clearer spacing, a small icon tile, and slightly stronger secondary text contrast.
- Kept `List` denser than `Cards` by using smaller padding/radius and a single-line layout rather than reusing the full card treatment.
Tests added:
- None. This is a template-level visual regression with no practical automated assertion in the current suite.
Validated:
- `cmd /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS (only exempt `MSB8027` and `LNK4042` warnings remain)
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — PASS (`[  PASSED  ] 127 tests`)
- Manual UI verification: pending direct human retest of `List` view. The current automation harness could relaunch and inspect the window, but it did not reliably toggle the pane into `List` mode for a trustworthy screenshot.
Manual retest:
- Switch both panes to `List`.
- Confirm each row has a visible surface/boundary and is easier to scan than before.
- Confirm `List` still feels denser than `Cards`, not like the same mode with slightly different spacing.

### Bug Report (2026-03-26)
Reported by: User report
Description: `Group` view still looked the same as `Cards`, so toggling grouping did not give users a visible cue that the pane had changed modes.

### Fix: Group mode now renders visible section headers (2026-03-26)
Reported by: User report
Root cause:
- `RebuildAllTabRowsForPane()` already computed per-group boundaries and set group-header metadata on each `FileRowViewModel`, but the row templates tried to bind `RowDefinition.Height` directly to a `Double`, which is invalid in WinUI XAML.
- Because the grouping header template path did not compile cleanly, `Group` fell back to the same effective row rendering as `Cards`, making the mode look broken.
Fix:
- Updated `native/FileScout.Native/MainWindow.xaml` so both `CardRowTemplate` and `ListRowTemplate` use valid `Auto` row definitions and render the group header through a height-bound container with a bordered label chip and divider line.
- Increased the header treatment slightly in `native/FileScout.Native/Domain/FileRowViewModel.h` so grouped sections have enough vertical presence to read as a distinct mode.
- Wired `native/FileScout.Native/Domain/FileRowViewModel.cpp` into `native/FileScout.Tests/FileScout.Tests.vcxproj` so the existing `FileRowViewModel` grouping-property tests can link and verify the header-state plumbing.
Tests added:
- `FileRowViewModel.GroupHeaderHiddenByDefault`
- `FileRowViewModel.GroupHeaderVisibleWhenConfigured`
Validated:
- `cmd /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS (only exempt `MSB8027` and `LNK4042` warnings remain)
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=FileRowViewModel.*` — PASS
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — PASS (`[  PASSED  ] 129 tests`)
Manual retest:
- Toggle `Group` on and off in both panes while staying in `Cards`.
- Confirm the first row of each group now shows a visible section header chip/divider so the mode is clearly distinct from plain `Cards`.
- Repeat once after changing the active sort column (`Name`, `Type`, `Size`, or `Date Modified`) to confirm the group headers follow the current grouping basis.

### Bug Report (2026-03-26)
Reported by: User report
Description: Right-clicking blank pane space still did not reliably open the background context menu, so commands such as `New folder` and `Refresh` remained hard to reach unless an item was under the cursor.

### Fix: Blank-space context menu now listens on the live scroll surface (2026-03-26)
Reported by: User report
Root cause:
- Blank-space right-clicks happen on the `ListView`'s internal `ScrollViewer`, but the background-menu path was primarily listening on the outer pane border.
- That meant the actual surface the user right-clicked often had no direct background context handler, so the event could die inside the scrolling content instead of reaching `PaneSurface_ContextRequested(...)`.
Fix:
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` so each pane `ListView` now also routes `ContextRequested` into `PaneSurface_ContextRequested(...)`.
- Updated the `ListView.Loaded(...)` path to mark the internal `ScrollViewer` as right-tap-enabled and wire both `RightTapped` and `ContextRequested` to the same pane-aware handlers, guarded by a one-time `Tag` marker so the handler is not duplicated on later loads.
Tests added:
- None. This is a WinUI pointer-routing issue on runtime-generated controls, and there is no practical automated regression harness for it in the current suite.
Validated:
- `cmd /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS (only exempt `MSB8027` and `LNK4042` warnings remain)
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — PASS (`[  PASSED  ] 129 tests`)
Manual retest:
- Right-click whitespace below the last item, between row gaps, and in the empty lower portion of both panes.
- Confirm the background menu opens reliably and still includes `New folder` and `Refresh`.
- Confirm right-clicking an actual item still opens the item-aware context menu instead of the background menu.

### Bug Report (2026-03-26)
Reported by: User report
Description: Row gaps still behaved like part of the item, selected rows no longer looked selected, empty folders still had no reliable background right-click target, disabled forward/back buttons could fall through to title-bar chrome, bookmarks lacked a right-click `Unpin`, and the pane/sidebar scrollbars were too hidden until hover.

### Fix: Pane interaction regressions + nav/button chrome cleanup (2026-03-26)
Reported by: User report
Root cause:
- Row spacing lived inside the row templates instead of the `ListViewItem` container, so the visible gap still belonged to the item hit target and never became real background space.
- The selected-state tint sat behind the opaque row surface, so `IsSelected` was still changing but the visual feedback was effectively hidden.
- Empty-folder background input only existed on inner list surfaces; when the list rendered little or no content, the pane host itself had no direct background context handlers.
- Disabled back/forward buttons were removed from the custom title-bar passthrough rectangles, so clicks on them could be interpreted as non-client title-bar interaction.
- Navigation button state was only refreshed on breadcrumb rebuilds, leaving stale history affordances in edge cases like pane switches and drives view.
Fix:
- Updated `native/FileScout.Native/MainWindow.xaml` with separate `CardRowItemStyle` / `ListRowItemStyle` container margins so row gaps are owned by the pane, not by each item, and moved the selection tint inside the opaque row surface so selected rows are visibly highlighted again.
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` so pane surface hosts now forward `RightTapped` and `ContextRequested` for empty/underfilled tabs, the internal list `ScrollViewer` keeps a visible vertical scrollbar, and sidebar/bookmark buttons expose a right-click `Unpin` flyout.
- Kept disabled navigation buttons inside the title-bar passthrough region and refreshed `UpdateNavButtons()` when history state changes, pane focus changes, and drives view loads so disabled buttons stop acting like title-bar chrome and history affordances stay in sync.
- Updated the sidebar `ScrollViewer` to keep its vertical scrollbar visible instead of hover-only.
Tests added:
- None. These are WinUI hit-testing, title-bar routing, and visual-template regressions with no practical automated regression harness in the current suite.
Validated:
- `cmd /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` — PASS (only exempt `MSB8027` and `LNK4042` warnings remain)
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` — PASS (`[  PASSED  ] 129 tests`)
- Local UI automation attempt: launch/focus worked, but screenshot capture was not reliable enough to count as acceptance evidence for these interaction fixes.
Manual retest:
- Right-click directly between two rows, below the last row, and inside an empty folder; confirm the background menu opens and shows actions such as `New folder` and `Refresh`.
- Select one row in both `Cards` and `List`; confirm the selected row has an obvious tinted fill/outline.
- Switch panes, navigate into and out of folders, then confirm disabled back/forward buttons do not minimize the window or eat title-bar double-click behavior.
- Right-click a bookmark and confirm a context menu appears with `Unpin`.
- Confirm the pane and sidebar scrollbars stay visible enough to discover without hovering them first.
