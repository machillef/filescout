# FileScout Native Rewrite Status

## Initiative State

- Active track: `native/` C++20 + C++/WinRT + WinUI 3
- Target: unpackaged Windows 11 x64 desktop app
- `native/` is now tracked in git and is the only active implementation track
- Current phase: Phase 3, Differentiated features complete through R18
- Current blocker: none
- Current known warning: MSB8027 + LNK4042 for WindowsAppRuntimeAutoInitializer.obj (duplicate obj, extras ignored — harmless, pre-existing WinAppSDK issue)
- App launches and shows window as of 2026-03-20
- R17 (fuzzy search) complete 2026-03-22: FuzzyMatcher, SearchService, Filter + Recursive
  modes with streaming ProgressBar and navigate-on-select (R17a–R17e); 71/71 tests
- R18 (chrome UX redesign) complete 2026-03-22: drive icon fix, breadcrumb navigation,
  dedicated SearchTextBox, keyboard type-ahead (R18a–R18c); 71/71 tests
- Perf staged row publication (Tasks 1–3) complete 2026-03-21; Task 4 (manual
  benchmark acceptance) pending — launch app and observe TraceLog PerfLoad lines
  on a 1k–5k item folder to capture before/after timings and close the slice
- R13 spec written 2026-03-22: CommandRegistry + palette overlay architecture defined,
  sub-slices R13a–R13d specified in slices.md
- R13a complete 2026-03-22: CommandRegistry + palette scaffold; Ctrl+P opens centred
  overlay, typing fuzzy-filters 14 scaffold commands, Escape closes, click/Enter
  dispatches; 7/7 new tests pass, both binaries build clean

## Slice R13a: CommandRegistry + Palette Scaffold
Status: Complete
Last updated: 2026-03-22

### What was implemented
- `Domain/CommandEntry.h` — pure C++ struct (id, display_name, shortcut_text, category)
- `Shell/CommandRegistry.h/.cpp` — FuzzyScore-backed filter returning scored+sorted results;
  14 scaffold commands across Navigation/File/View categories
- `MainWindow.xaml` — `CommandPaletteOverlay` Grid (Visibility=Collapsed, rowspan=3),
  `CommandPaletteTextBox` (TextChanged + KeyDown), `CommandPaletteListView` (ItemClick)
- `MainWindow.xaml.h/.cpp` — `m_commandRegistry` member; `ShowCommandPalette()`,
  `HideCommandPalette()`, `RefreshCommandPalette()`, `CreateCommandRowView()`;
  Ctrl+P accelerator in `SetupKeyboardAccelerators()`; Up/Down/Enter/Escape keyboard nav
- `FileScout.Tests/tests/CommandRegistryTests.cpp` — 7 tests: AllReturnsNonEmpty,
  EmptyQueryReturnsAll, FilterMatchesByDisplayName, FilterReturnsEmptyForNoMatch,
  FilterIsCaseInsensitive, FilterResultsAreSubsetOfAll, EachCommandHasNonEmptyId
- Both vcxproj files updated with new sources

### What was validated
- All 7 CommandRegistry tests pass
- FileScout.Native.exe builds clean (only pre-existing MSB8027/LNK4042 warnings)
- Fix applied: XAML event handlers must be in public section; (std::min) idiom for Win32 macro conflict

### What remains unverified
- Runtime Ctrl+P gesture (requires manual launch of app)

### Blockers
- None

### Next recommended step
- R13b: wire navigation commands (newTab, closeTab, switchPane, refresh, back, forward)
  through ExecuteContextCommand to real implementations

## Slice R1: Native Workspace And Shell Scaffold
Status: Complete
Last updated: 2026-03-15

### What was implemented
- Added the same-repo WinUI 3 solution under `native/`
- Added the unpackaged desktop host and initial app/window XAML shell
- Added thin domain contracts for shell items, commands, operations, clipboard,
  and selection state
- Added real directory enumeration via `std::filesystem`

### What was validated
- NuGet package restore for `native/FileScout.Native.sln` - pass
- Manual code review of scaffolded project structure - pass

### What remains unverified
- End-to-end native compile in the current local environment

### Blockers
- `MSB8020` during native build

### Next recommended step
- Continue `R2` on top of the existing shell-backed scaffold while keeping the
  SDK discovery issue visible in every validation report

## Slice R2a: Selectable Shell Pane Foundation
Status: Complete
Last updated: 2026-03-16

### What was implemented
- Extended `native/FileScout.Native/Domain/ShellItemRef.h` with item kind and
  size metadata needed for shell-pane presentation
- Updated `native/FileScout.Native/Shell/ShellInteropService.cpp` to populate
  file kind labels, file sizes, and folder-first sorting metadata
- Replaced the placeholder repeater in
  `native/FileScout.Native/MainWindow.xaml` with a selectable `ListView`
- Added selection details UI in the left sidebar for selected item name,
  metadata, and full path
- Added selection tracking and folder activation logic in
  `native/FileScout.Native/MainWindow.xaml.cpp` and
  `native/FileScout.Native/MainWindow.xaml.h`

### What was validated
- Added explicit `TargetPlatformVersion` and `TargetPlatformMinVersion` to
  `native/FileScout.Native/FileScout.Native.vcxproj`, which removes the
  misleading `MSB8036` failure and unlocks normal Windows SDK resolution
- Added explicit WinAppSDK component imports and WebView2/Runtime metadata
  wiring in `native/FileScout.Native/FileScout.Native.vcxproj` and
  `native/FileScout.Native/packages.config`
- `cmd /c --% ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"` - pass
- Self-review of the native slice diff for selection flow, metadata wiring, and
  row rendering - pass

### What remains unverified
- Manual WinUI behavior for single-click selection and double-click folder
  activation

### Blockers
- None

### Next recommended step
- Continue with `R2b`: native icon extraction, richer file-row visuals, and the
  first pass at the modern File Pilot-style visual system while keeping
  functionality first

## Slice R2b: Native Icon Extraction And Richer Rows
Status: Complete
Last updated: 2026-03-16

### What was implemented
- Added shell icon bitmap data to
  `native/FileScout.Native/Domain/ShellItemRef.h`
- Updated `native/FileScout.Native/Shell/ShellInteropService.cpp` to extract
  and cache native small shell icons by folder or file extension
- Upgraded `native/FileScout.Native/MainWindow.xaml.cpp` to render richer row
  cards with icon tiles, title/detail/path text, and selection detail icons
- Bound the native list view to an observable row collection and restored the
  initial current-directory load path
- Added startup error surfacing in `native/FileScout.Native/App.xaml.cpp` so
  future WinUI/XAML startup failures are visible instead of collapsing into an
  opaque `combase.dll` crash

### What was validated
- Removed the duplicate WinAppSDK auto-initializer registration path in
  `native/FileScout.Native/FileScout.Native.vcxproj`; native debug builds now
  complete with `0 Warning(s)` and `0 Error(s)`
- `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"` - pass
- Launch smoke for
  `native/x64/Debug/FileScout.Native/FileScout.Native.exe` - pass

### What remains unverified
- Manual WinUI behavior for native-icon rendering, single-click selection, and
  double-click folder activation
- Visual polish beyond the first modern File Pilot-style pass

### Blockers
- None

### Next recommended step
- Track `native/` in git and begin `R3`: FileScout-owned Windows 11 style
  context menu with shell-backed overflow

## Slice R3a: FileScout-Owned Context Menu Foundation
Status: Complete
Last updated: 2026-03-16

### What was implemented
- Added a dedicated context-menu builder service in
  `native/FileScout.Native/Shell/ContextMenuService.h` and
  `native/FileScout.Native/Shell/ContextMenuService.cpp`
- Extended `native/FileScout.Native/MainWindow.xaml` and
  `native/FileScout.Native/MainWindow.xaml.h` with right-click event wiring for
  the shell-backed list surface
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` to resolve right-clicked
  items, build grouped menu content, show a WinUI `MenuFlyout`, and dispatch
  commands through one centralized handler
- Kept already-supported actions live (`Open` for folders, `Refresh`) and routed
  future operations to clear status placeholders instead of silently doing
  nothing

### What was validated
- `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"` - pass
- Launch smoke for
  `native/x64/Debug/FileScout.Native/FileScout.Native.exe` - pass

### What remains unverified
- Manual right-click behavior for selected items vs empty list space
- Manual menu command behavior for placeholder `R4` actions and the visual feel
  of the WinUI flyout against the current explorer surface

### Blockers
- None

### Next recommended step
- Continue `R4` into `R4c`: implement `Copy`, `Cut`, and `Paste` against a
  coherent native clipboard model

## Slice R3b: Shell-Backed Overflow For More Windows Actions
Status: Complete
Last updated: 2026-03-16

### What was implemented
- Added a dedicated native overflow host in
  `native/FileScout.Native/Shell/ShellOverflowMenuService.h` and
  `native/FileScout.Native/Shell/ShellOverflowMenuService.cpp`
- Updated `native/FileScout.Native/MainWindow.xaml.h` and
  `native/FileScout.Native/MainWindow.xaml.cpp` to capture the right-click
  screen position and route `More Windows actions...` through the shell-backed
  overflow menu
- Reused the existing FileScout-owned WinUI menu as the primary surface while
  limiting the native fallback to the explicit overflow path only
- Filtered shell clipboard verbs from the overflow popup so the future app-owned
  clipboard model is not pre-empted by partial shell behavior
- Updated `native/FileScout.Native/MainWindow.xaml`,
  `native/FileScout.Native/FileScout.Native.vcxproj`, and
  `native/FileScout.Native/FileScout.Native.vcxproj.filters` so the explorer
  surface and project metadata reflect the new overflow slice

### What was validated
- `cmd /c --% ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"` - pass
- Launch smoke for
  `native/x64/Debug/FileScout.Native/FileScout.Native.exe` - pass

### What remains unverified
- Manual right-click behavior for `More Windows actions...`
- Manual shell overflow behavior for extension-owned commands and dismiss paths

### Blockers
- None

### Next recommended step
- Begin `R4a`: wire the existing placeholder commands to real shell-backed
  open/open-with/properties and file activation

## Slice R4a: Shell-Backed Open, Open With, Properties, And File Activation
Status: Complete
Last updated: 2026-03-16

### What was implemented
- Added a dedicated shell verb service in
  `native/FileScout.Native/Shell/ShellFileOperationService.h` and
  `native/FileScout.Native/Shell/ShellFileOperationService.cpp`
- Updated `native/FileScout.Native/MainWindow.xaml.h` and
  `native/FileScout.Native/MainWindow.xaml.cpp` to inject the new service and
  route file double-click activation through a real shell `Open`
- Replaced the `Open`, `Open with...`, and `Properties` context-menu
  placeholders in `native/FileScout.Native/MainWindow.xaml.cpp` with live
  shell-backed execution and user-visible success/failure status text
- Updated `native/FileScout.Native/MainWindow.xaml`,
  `native/FileScout.Native/FileScout.Native.vcxproj`, and
  `native/FileScout.Native/FileScout.Native.vcxproj.filters` so the explorer
  surface and project manifest reflect the active `R4a` slice

### What was validated
- `cmd /c --% ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"` - pass
- Launch smoke for
  `native/x64/Debug/FileScout.Native/FileScout.Native.exe` - pass

### What remains unverified
- Manual double-click activation for files
- Manual `Open with...` and `Properties` behavior from the native context menu

### Blockers
- None

### Next recommended step
- Continue with `R4b`: implement `New folder`, `Rename`, and `Delete` so the
  alpha GUI can perform its first real in-app file mutations

## Slice R4b: Native New Folder, Rename, And Delete Mutations
Status: Complete
Last updated: 2026-03-16

### What was implemented
- Extended `native/FileScout.Native/Shell/ShellFileOperationService.h` and
  `native/FileScout.Native/Shell/ShellFileOperationService.cpp` with shell-native
  `IFileOperation` support for `NewItem`, `RenameItem`, and recycle-bin delete
- Updated `native/FileScout.Native/MainWindow.xaml.h` and
  `native/FileScout.Native/MainWindow.xaml.cpp` with WinUI `ContentDialog`
  flows for folder creation, rename, and delete confirmation
- Added Windows filename validation, default `New Folder` name generation, and
  reload-and-reselect behavior after successful mutations in
  `native/FileScout.Native/MainWindow.xaml.cpp`
- Updated `native/FileScout.Native/MainWindow.xaml` so the explorer surface
  reflects the active `R4b` mutation slice

### What was validated
- `cmd /c --% ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"` - pass
- Launch smoke for
  `native/x64/Debug/FileScout.Native/FileScout.Native.exe` - pass

### What remains unverified
- Manual `New Folder` dialog flow and collision handling
- Manual rename and delete confirmation behavior in the running WinUI app
- Recycle Bin semantics after delete in the current unpackaged desktop host

### Blockers
- None

### Next recommended step
- Continue with `R4c`: wire `Copy`, `Cut`, and `Paste` through a native
  clipboard model so the alpha explorer can move beyond single-item mutations

## Slice R4c: Native Clipboard-Backed Copy, Cut, And Paste
Status: Complete
Last updated: 2026-03-16

### What was implemented
- Added `native/FileScout.Native/Shell/ShellClipboardService.h` and
  `native/FileScout.Native/Shell/ShellClipboardService.cpp` to own the native
  clipboard path through shell `IDataObject`, `CFSTR_PREFERREDDROPEFFECT`, and
  `OleSetClipboard`/`OleGetClipboard`
- Extended `native/FileScout.Native/Shell/ShellFileOperationService.h` and
  `native/FileScout.Native/Shell/ShellFileOperationService.cpp` with a shell
  paste path that routes clipboard-backed copy or move operations through
  `IFileOperation`
- Updated `native/FileScout.Native/MainWindow.xaml.h` and
  `native/FileScout.Native/MainWindow.xaml.cpp` so right-click menus enable
  `Paste` when shell items are available on the clipboard and so `Copy`, `Cut`,
  and `Paste` now perform real native operations instead of placeholder status
  text
- Updated `native/FileScout.Native/MainWindow.xaml`,
  `native/FileScout.Native/pch.h`,
  `native/FileScout.Native/FileScout.Native.vcxproj`, and
  `native/FileScout.Native/FileScout.Native.vcxproj.filters` so the live native
  explorer surface and project metadata reflect the `R4c` clipboard slice

### What was validated
- `cmd /c --% ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"` - pass
- Launch smoke for `native/x64/Debug/FileScout.Native/FileScout.Native.exe` -
  pass

### What remains unverified
- Manual `Copy`, `Cut`, and `Paste` behavior inside the running WinUI app
- Collision behavior and naming when pasting into a directory that already
  contains the same item names
- Clipboard behavior when pasting shell items copied from Explorer into
  FileScout and vice versa

### Blockers
- None

### Next recommended step
- Continue with `R5`: add native drag/drop and broader clipboard interop on top
  of the now-live shell clipboard model

## Slice R4d: Native Runtime Stability Fix
Status: Complete
Last updated: 2026-03-17

### What was implemented
- Identified root cause via startup log: `Cannot locate resource from
  'ms-appx:///Microsoft.UI.Xaml/Themes/themeresources.xaml'` — MRT resource
  resolution was broken because `WindowsAppSDKSelfContained=true` caused
  `MICROSOFT_WINDOWSAPPSDK_SELFCONTAINED=1` to be defined only for app code but
  NOT for `WindowsAppRuntimeAutoInitializer.cpp`, creating a mixed-mode
  initialization where `MddBootstrapInitialize2` was a no-op but the
  auto-initializer ran in framework-package mode; the MRT namespace mapping for
  `ms-appx://Microsoft.UI.Xaml/` was never established
- Fixed by changing `WindowsAppSDKSelfContained` from `true` to `false` in
  `native/FileScout.Native/FileScout.Native.vcxproj`; `MddBootstrapInitialize2`
  in `main.cpp` now performs a real framework-package bootstrap against the
  installed `Microsoft.WindowsAppRuntime.1.8 8000.770.947.0` runtime, which
  properly establishes the MRT resource namespace
- Fixed a clean-build compiler failure (`C3859`/`C1076: compiler limit: internal
  heap limit reached`) by adding `DebugInformationFormat=ProgramDatabase` to
  the Debug configuration (switches from `/ZI` Edit-and-Continue to `/Zi`, which
  removes the low-address virtual memory pressure for the PCH) and adding
  `/Zm200` to `AdditionalOptions` (increases the compiler's internal heap
  allocation for the large WinUI header set)

### What was validated
- Clean rebuild (`/t:Rebuild`) with `0 Error(s)` and `0 Warning(s)` — pass
- Launch smoke: process stays alive for 5+ seconds, no VC++ runtime abort — pass
- Startup log is empty (no `ms-appx` XAML resource errors) — pass

### What remains unverified
- Manual exercise of all context menu, clipboard, and file mutation flows after
  the deployment-mode change; the framework-package path is functionally
  equivalent but has not been manually re-exercised

### Blockers
- None

### Next recommended step
- Continue with `R5`: native drag/drop and clipboard interop

## Slice R5: Native Drag/Drop And Clipboard Interop
Status: Complete
Last updated: 2026-03-17

### What was implemented
- Added three WinRT namespace headers to `native/FileScout.Native/pch.h` for
  Windows Storage and DataTransfer APIs
- Added `CopyItemsFromPaths` to `native/FileScout.Native/Shell/ShellFileOperationService.h`
  and `native/FileScout.Native/Shell/ShellFileOperationService.cpp` — mirrors the
  existing `PasteItems` pattern but accepts a `std::span<std::filesystem::path>`
  instead of an `IDataObject`, routing path-based copy/move through `IFileOperation`
- Added drag source support via `FileRowsListView_DragItemsStarting` in
  `native/FileScout.Native/MainWindow.xaml.cpp`: resolves the selected item via
  `StorageFile::GetFileFromPathAsync` or `StorageFolder::GetFolderFromPathAsync`,
  packages it as `StorageItems` in the `DataPackage`, and sets
  `RequestedOperation` to `Copy`
- Added drop target support via `FileRowsListView_DragOver` (accepts
  `StorageItems` payloads, sets Copy/Move based on Shift modifier, updates
  DragUIOverride caption) and `FileRowsListView_Drop` (a `fire_and_forget`
  coroutine using `GetDeferral()` to stay alive across the `co_await` of
  `GetStorageItemsAsync`, then calls `CopyItemsFromPaths` and reloads the
  directory)
- Added `SetupKeyboardAccelerators()` in
  `native/FileScout.Native/MainWindow.xaml.cpp` called from the constructor:
  attaches WinUI `KeyboardAccelerator` objects for Ctrl+C (`copy`), Ctrl+X
  (`cut`), Ctrl+V (`paste`), Delete (`delete`), F2 (`rename`), and F5
  (`refresh`), all routed through the existing `ExecuteContextCommand` dispatch
- Updated `native/FileScout.Native/MainWindow.xaml` with `AllowDrop="True"`,
  `CanDrag="True"`, and the three DnD event attributes on the ListView
- Updated `native/FileScout.Native/MainWindow.xaml.h` with the three DnD event
  handler declarations and the `SetupKeyboardAccelerators` private helper

### What was validated
- Clean build (`/t:Rebuild`) with `0 Error(s)` and `0 Warning(s)` — pass
- Launch smoke: process stays alive for 4+ seconds, no VC++ runtime abort,
  startup log empty — pass

### What remains unverified
- Manual drag-and-drop from FileScout pane to itself (copy and move paths)
- Manual drag-from-Explorer-into-FileScout and drag-from-FileScout-into-Explorer
- Keyboard shortcut behavior (Ctrl+C/X/V/Del/F2/F5) in the running WinUI app

### Blockers
- None

### Next recommended step
- Continue with `R6`: multiple tabs per pane in the native shell

## Slice R6: Tabs
Status: Complete
Last updated: 2026-03-17

### What was implemented
- Added `native/FileScout.Native/Domain/TabState.h` — domain struct holding per-tab
  path, item list, selection state, and selected-item index; exposes a `display_name()`
  helper that returns the directory basename or `"New Tab"` for empty tabs
- Rewrote `native/FileScout.Native/MainWindow.xaml`: replaced the static `ListView`
  in the right panel with a WinUI `TabView` (`PaneTabView`); moved
  `FileRowItemStyle` into a `Grid.Resources` keyed resource so it can be
  looked up at runtime when programmatically creating per-tab `ListView` instances
- Updated `native/FileScout.Native/MainWindow.xaml.h`: added `m_tabs`,
  `m_tabRows`, `m_activeListView`, `m_activeTabIndex`, and `m_tabSwitching`
  members; added `AddTabButtonClick`, `TabCloseRequested`, and
  `PaneTabView_SelectionChanged` event handler declarations; added private
  helpers `CreateTabListView`, `AddTab`, `CloseTab`, `RestoreActiveTab`,
  `UpdateTabHeader`, and `UpdateTabCloseability`
- Rewrote `native/FileScout.Native/MainWindow.xaml.cpp`: constructor creates the
  initial tab rows/ListView/TabViewItem; `CreateTabListView` builds a fully-wired
  `ListView` with item style and all event handlers; `AddTab` persists current
  state, creates new rows/ListView/TabViewItem, and loads the new tab's directory;
  `CloseTab` guards last-tab closure, erases from `m_tabs`/`m_tabRows`/`TabItems`,
  clamps `m_activeTabIndex`, and triggers `RestoreActiveTab` via
  `SelectedIndex`; `RestoreActiveTab` switches `m_activeListView`, restores
  path/items/selection from the cached `TabState`; `UpdateTabHeader` sets the
  `TabViewItem.Header`; `UpdateTabCloseability` toggles `IsClosable` based on
  tab count; keyboard accelerators Ctrl+T and Ctrl+W added for new/close tab
- Updated `native/FileScout.Native/FileScout.Native.vcxproj.filters` with
  `Domain\TabState.h` entry

### What was validated
- Clean build (`0 Error(s)`, `0 Warning(s)`) after fixing `IVector::IndexOf`
  two-argument WinRT signature — pass

### What remains unverified
- Launch smoke and manual tab open/close/switch behavior in the running WinUI app
- Ctrl+T / Ctrl+W keyboard shortcuts in the live WinUI shell

### Blockers
- None

### Next recommended step
- Continue with `R7`: dual-pane layout with two independent panes and active-pane routing

## Slice R7: Dual-Pane Layout
Status: Complete
Last updated: 2026-03-17

### What was implemented
- Added `PaneContext` struct in `MainWindow.xaml.h` holding all per-pane browse state:
  `current_path`, `items`, `selection`, `tabs`, `tab_rows`, `active_list_view`,
  `active_tab_index`, `tab_switching`
- Rewrote `MainWindow.xaml` to a three-column layout: sidebar (260 px) + two pane
  columns each wrapping a `TabView` (`LeftPaneTabView` / `RightPaneTabView`) inside
  named `Border` elements (`LeftPaneBorder` / `RightPaneBorder`) for the active-pane
  highlight
- Refactored `MainWindow.xaml.h` private members: replaced the seven individual pane
  fields with `std::array<PaneContext, 2> m_panes` and `int32_t m_activePaneIndex`;
  added `ActivePane()`, `ActiveTabView()`, `TabViewForPane()`, `PaneIndexForTabView()`,
  `SetActivePaneIndex()`, `UpdateActivePaneIndicator()`, and `InitPane()`
- Rewrote `MainWindow.xaml.cpp` to route all operations through `ActivePane()` and
  `ActiveTabView()`; both panes share the same XAML event handler names and are
  disambiguated in the handler bodies via `PaneIndexForTabView(sender)`; `CreateTabListView`
  now takes `paneIndex` and captures it in lambdas that call `SetActivePaneIndex`
  before dispatching, so clicking in an inactive pane activates it automatically
- Added `InitPane(paneIndex, startPath)` to bootstrap a pane's first tab and run
  `LoadDirectory` through the pane-local context without clobbering the other pane
- Added `switchPane` command (`Ctrl+Tab`) to toggle the active pane from the keyboard
- `UpdateActivePaneIndicator()` sets a `#0078D4` accent-blue 2px border on the active
  pane and transparent on the inactive one
- Both panes load `std::filesystem::current_path()` at startup

### What was validated
- Clean build (`0 Error(s)`, `0 Warning(s)`) after fixing WinRT value-type binding
  (`auto& tabView` → `auto tabView`) — pass

### What remains unverified
- Launch smoke and manual two-pane browse, pane focus switching (click + Ctrl+Tab)
- Per-pane tab management (Ctrl+T, Ctrl+W) in both panes
- Drag-and-drop between the two panes

### Blockers
- None

### Next recommended step
- Continue with `R8`: favorites, sidebar tree, and pinned locations — Complete

## Slice R8: Favorites, Sidebar Tree, And Pinned Locations
Status: Complete
Last updated: 2026-03-18

### What was implemented
- Added `native/FileScout.Native/Domain/FavoriteEntry.h` — domain struct for sidebar pinned entries
  with `display_name`, `path`, `is_drives_view`, and `is_recycle_bin` flags
- Added `native/FileScout.Native/Shell/FavoritesService.h` and `FavoritesService.cpp` — resolves
  shell known-folder paths via `SHGetKnownFolderPath` for Home, Desktop, Documents, Downloads,
  Pictures, Music, and Videos; `GetUserHomePath()` falls back to `USERPROFILE` env var
- Extended `native/FileScout.Native/Shell/ShellInteropService.h` and `ShellInteropService.cpp`
  with `EnumerateDrives()` — uses `GetLogicalDriveStringsW` + `GetDriveTypeW` +
  `GetVolumeInformationW` to build `ShellItemRef` entries for all attached drives
- Added `is_drives_view` flag to `native/FileScout.Native/Domain/TabState.h`; `display_name()`
  now returns `"This PC"` when the flag is set
- Added `is_drives_view` flag to `PaneContext` in `native/FileScout.Native/MainWindow.xaml.h`;
  cleared by `LoadDirectory`, set by `LoadDrivesView`
- Added `FavoritesService m_favoritesService` member and `BuildSidebarFavorites()` /
  `LoadDrivesView()` methods to `native/FileScout.Native/MainWindow.xaml.h`
- Updated `native/FileScout.Native/MainWindow.xaml` — replaced static `TextBlock` sidebar
  entries with a named `StackPanel x:Name="SidebarPlacesPanel"` populated in code
- Updated `native/FileScout.Native/MainWindow.xaml.cpp`:
  - Constructor now starts both panes at `m_favoritesService.GetUserHomePath()` instead of CWD
  - `BuildSidebarFavorites()` creates `Button` elements for each favorite; folders navigate
    the active pane via `LoadDirectory`, "This PC" calls `LoadDrivesView()`, "Recycle Bin"
    uses `ShellExecuteW(L"shell:RecycleBinFolder")`
  - `LoadDrivesView()` populates the active pane with drive entries, sets `is_drives_view = true`,
    and shows "This PC" in the address bar
- Updated `native/FileScout.Native/FileScout.Native.vcxproj` and `.vcxproj.filters` to
  include the new `FavoriteEntry.h`, `FavoritesService.h`, and `FavoritesService.cpp`

### What was validated
- Clean rebuild (`/t:Rebuild`) with `0 Error(s)` and `0 Warning(s)` — pass
- Launch smoke: process stays alive for 5+ seconds, no abort — pass

### What remains unverified
- Manual click behavior for each sidebar entry (Home, Desktop, Documents, Downloads, Pictures,
  Music, Videos, This PC, Recycle Bin)
- "This PC" drive list rendering and double-click navigation into a drive root
- Recycle Bin shell:// redirect in Explorer

### Blockers
- None

### Next recommended step
- Continue with `R9`: sort/group/view mode parity

## Slice R8b: Sidebar Places Polish, Pinning, And Drive Usage Cards
Status: Complete
Last updated: 2026-03-19

### What was implemented
- Extended `native/FileScout.Native/Domain/FavoriteEntry.h` and
  `native/FileScout.Native/Shell/FavoritesService.h/.cpp` so sidebar favorites
  now support persisted custom pinned entries in addition to the built-in
  Places list, stored under `%LOCALAPPDATA%\FileScoutNative\pinned-places.txt`
- Updated `native/FileScout.Native/Shell/ContextMenuService.h/.cpp` and
  `native/FileScout.Native/MainWindow.xaml.cpp` so the FileScout-owned
  right-click menu can expose `Pin to Places` for a single folder selection or
  the current folder when it is not already present in the sidebar
- Reworked `native/FileScout.Native/MainWindow.xaml.cpp` so sidebar Place
  buttons render small icon tiles, custom pinned entries get a distinct
  `Pinned` section and badge, mojibake-prone separator copy uses ASCII-safe
  formatting, and the sidebar adds compact drive-usage cards with capacity text
  and progress bars
- Made the new drive-usage cards actionable in
  `native/FileScout.Native/MainWindow.xaml.cpp`, so clicking a drive card
  loads that drive into the active pane instead of presenting a dead status-only
  widget
- Added dedicated coverage in
  `native/FileScout.Tests/tests/FavoritesServiceTests.cpp` for persisted pinned
  Places behavior, including successful persistence, duplicate rejection, and
  built-in favorite dedupe; updated
  `native/FileScout.Tests/tests/ContextMenuServiceTests.cpp` to cover the new
  `Pin to Places` command paths

### What was validated
- `cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal'` - pass (with only the pre-existing `MSB8027` / `LNK4042` WinAppSDK duplicate-object warnings)
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` - pass (`54` tests, `0` failures)

### What remains unverified
- Manual sidebar readability and click targets for the new icon tiles, pinned
  section, and drive-usage cards in the running WinUI host
- Manual `Pin to Places` discoverability and end-to-end feel from real
  right-click flows in the host
- Manual confirmation that the drive cards feel visually balanced in the
  sidebar at smaller window widths

### Blockers
- None

### Next recommended step
- Run one focused host-side UI pass for sidebar navigation, pinning, and drive
  cards, then decide whether to close sidebar polish or take one more narrow
  visual pass before moving on

## Slice R9a: Sort Column And Direction With Clickable Column Headers
Status: Complete
Last updated: 2026-03-18

### What was implemented
- Added `date_modified`, `date_modified_available`, `date_modified_str` to
  `native/FileScout.Native/Domain/ShellItemRef.h`
- Updated `native/FileScout.Native/Shell/ShellInteropService.cpp` to populate
  date-modified fields via `last_write_time()` → FILETIME → SYSTEMTIME with
  `GetDateFormatEx`/`GetTimeFormatEx`; removed the hardcoded sort from enumeration
- Added `SortColumn` and `SortDirection` enums to `MainWindow.xaml.h`; added
  `sort_column`/`sort_direction` to `PaneContext`
- Added `SortItems()` comparator with folders-first on all columns, name tie-break
  for Size/DateModified; `SetSortColumn()` with per-pane toggle/flip logic
- Added `BuildSortHeader(paneIndex)` — programmatic 4-column header row
  (Name/Type/Size/Date Modified) with ↑/↓ active indicator and accent colour;
  one header per pane in `LeftPaneSortHeader`/`RightPaneSortHeader` Borders
- Updated `native/FileScout.Native/MainWindow.xaml` with sort header containers
  above each `TabView`
- Updated `CreateRowView()`: date shown right-aligned in the metadata row;
  tighter card padding (8 px corner radius, 10/12 px padding)
- Fixed HIGH: `ULONGLONG` cast before FILETIME bit-split removes signed right-shift UB
- Fixed HIGH: folders-first guard applied to all four sort columns

### What was validated
- Clean build (`0 Error(s)`, `0 Warning(s)`) — pass
- Launch smoke: process alive 5+ seconds — pass

### What remains unverified
- Manual click of each sort column header and direction toggle
- Manual verification that Date Modified renders correctly for various file ages
- Sort behaviour on the "This PC" drives view

### Blockers
- None

### Next recommended step
- Continue with `R9b`: view mode toggle (compact card vs. details list) — Complete
- Continue with `R10`: preview and inspect surface — Complete

## Slice R9b: View Mode Toggle (Cards / List)
Status: Complete
Last updated: 2026-03-18

### What was implemented
- Added `ViewMode` enum (`Cards`, `List`) to `MainWindow.xaml.h` and `PaneContext`
- Split `CreateRowView` into `CreateCardRow` (rich 68px card with icon tile, name, metadata) and
  `CreateListRow` (compact 5-column grid: 20px icon | name 3* | type 1.8* | size 1.2* | date 2.0*)
- `UpdateRows` passes `pane.view_mode == ViewMode::List` to `CreateRowView`
- `BuildSortHeader` gains trailing "Cards"/"List" toggle buttons (accent on active);
  List mode prepends a 28px icon spacer column for approximate header/row alignment
- `SetViewMode` rebuilds sort header and calls `RebuildAllTabRowsForPane`
- `RebuildAllTabRowsForPane`: uses `pane.items` for the active tab (live), cached items
  for background tabs; bounds-guarded with `(std::min)()` to prevent Windows `min` macro
- `FileRowItemStyle` `Margin` set to `0`; gap is embedded per row element (8px for cards, 0 for list)

### What was validated
- Clean build (`0 Error(s)`, `0 Warning(s)`) — pass

### What remains unverified
- Manual click of Cards/List toggle and visual inspection of both modes
- Header/row alignment in list mode at various window widths

### Blockers
- None

## Slice R9c: Grouped Rows Tied To The Active Sort Column
Status: Complete
Last updated: 2026-03-18

### What was implemented
- Added `group_by_sort_column` to `PaneContext` in
  `native/FileScout.Native/MainWindow.xaml.h` and introduced
  `SetGroupingEnabled()` in `native/FileScout.Native/MainWindow.xaml.cpp`
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` with a per-pane `Group`
  toggle in `BuildSortHeader()` so grouping follows the currently selected sort
  column instead of adding a second independent grouping model
- Extended `CreateRowView()` in `native/FileScout.Native/MainWindow.xaml.cpp`
  with lightweight in-row group headers for Name/Type/Size/Date Modified
  groupings, preserving the existing one-row-per-item `ListView` index mapping
  used by selection, activation, drag/drop, and context menus
- Updated `SetSortColumn()` and `RebuildAllTabRowsForPane()` in
  `native/FileScout.Native/MainWindow.xaml.cpp` so shared pane sort/group state
  re-sorts cached inactive tabs and rebuilds their rows instead of leaving them
  stale until the next disk reload

### What was validated
- `cmd /c --% ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"` - pass
- Launch smoke for
  `native/x64/Debug/FileScout.Native/FileScoutNative.exe` - pass

### What remains unverified
- Manual `Group` toggle behavior in both panes and in both Cards/List modes
- Manual validation of Name/Size/Date Modified group labels on normal folders and
  the "This PC" drives view
- Manual validation that inactive tabs reflect shared pane sort/group changes
  after tab switches

### Blockers
- None

### Next recommended step
- Continue with `R11`: archive mode (browse zip/7z/tar as virtual views)

## Slice R10: Preview And Inspect Surface
Status: Complete
Last updated: 2026-03-18

### What was implemented
- Added to `ShellItemRef`: `is_image`, `date_created_available/str`,
  `file_attributes_available`, `file_attributes` (uint32_t)
- `ShellInteropService::EnumerateDirectory` populates all new fields via
  `GetFileAttributesExW` (creation time, attributes) and extension matching
  (`std::wstring_view` array for 9 image extensions)
- XAML sidebar: `PreviewBorder`/`PreviewImage` (image preview, collapsed by default),
  `SelectionIconBorder` (named for code-behind toggle), `SelectionCreatedTextBlock`,
  `SelectionAttributesTextBlock`
- `LoadImagePreviewAsync`: `fire_and_forget` with `m_previewGeneration` counter to
  discard stale loads; uses `StorageFile::GetFileFromPathAsync + SetSourceAsync` to
  correctly handle paths with spaces and Unicode (avoids URI percent-encoding issues)
- `UpdateSelectionDetails`: shows image thumbnail for image files, created date, and
  attribute badges (Read-only / Hidden / System / Archive)
- `ResetSelectionDetails`: increments generation counter, hides new elements
- `pch.h`: added `winrt/Windows.Storage.Streams.h` for `OpenReadAsync` return type

### What was validated
- Clean build (`0 Error(s)`, `0 Warning(s)`) — pass

### What remains unverified
- Manual image preview for jpg/png/gif/bmp/webp with paths containing spaces
- Created date and attribute rendering for various file types
- Graceful fallback to icon tile when image load fails (access denied, corrupt)

### Blockers
- None

### Next recommended step
- Continue with `R11`: archive mode (browse zip/7z/tar as virtual views)

## Slice R11a: ZIP Archive Virtual View Foundation
Status: Complete
Last updated: 2026-03-18

### What was implemented
- Added archive-path detection plus ZIP shell-namespace enumeration to
  `native/FileScout.Native/Shell/ShellInteropService.h` and
  `native/FileScout.Native/Shell/ShellInteropService.cpp`; `LoadDirectory` can
  now treat `*.zip` roots and nested `*.zip\subfolder` paths as virtual folders
- Extended `native/FileScout.Native/Domain/TabState.h`,
  `native/FileScout.Native/MainWindow.xaml.h`, and
  `native/FileScout.Native/MainWindow.xaml.cpp` with per-tab/per-pane
  `is_archive_view` state so archive tabs restore correctly and status text
  distinguishes archive rows from filesystem rows
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` so double-click and
  `Open` enter ZIP roots/folders as virtual views, while drag/drop, paste,
  rename, delete, new-folder, shell overflow, and archived-file activation are
  explicitly blocked with read-only archive-mode status text
- Updated `native/FileScout.Native/Shell/ContextMenuService.h` and
  `native/FileScout.Native/Shell/ContextMenuService.cpp` so archive views show a
  minimal read-only context menu (`Open` for folders, `Refresh`) instead of
  misleading filesystem mutations
- Added `propkey.h` to `native/FileScout.Native/pch.h` so ZIP entries can pick
  up shell properties such as item type, size, and modified date when the
  compressed-folder namespace exposes them

### What was validated
- `cmd /c --% ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"` - pass
- Launch smoke for `native/x64/Debug/FileScout.Native/FileScoutNative.exe` - pass
- Local PowerShell probe of Windows `Shell.Application.NameSpace()` against a
  temp ZIP root and nested ZIP folder - pass

### What remains unverified
- Manual open/navigation flow for ZIP roots and nested folders in the running
  WinUI app
- Manual sort/group/view behavior against ZIP entries, including rows where the
  shell does not expose size/date metadata
- Archived-file activation/extract behavior and non-ZIP formats (`7z`/`tar`)

### Blockers
- None

### Next recommended step
- Continue with `R11b`: archived-file open/extract behavior and broader archive
  affordances before widening into `7z` and `tar`

## Slice R11b: Archived-File Temp Extraction And Shell Open
Status: Complete
Last updated: 2026-03-18

### What was implemented
- Added `TryResolveArchivePath` to
  `native/FileScout.Native/Shell/ShellInteropService.h` and
  `native/FileScout.Native/Shell/ShellInteropService.cpp` so the UI layer can
  resolve a ZIP entry back to its archive root plus virtual in-archive path
- Updated `native/FileScout.Native/MainWindow.xaml.h` and
  `native/FileScout.Native/MainWindow.xaml.cpp` with
  `OpenArchiveItemAsync`, plus temp-session path helpers that extract a selected
  archive file into `%TEMP%\\FileScoutNative\\ArchiveOpen\\...` before handing it
  off to the normal shell `Open` path
- Updated archive double-click and archive-view `Open` command handling in
  `native/FileScout.Native/MainWindow.xaml.cpp` so ZIP files now extract and
  open instead of stopping at a placeholder read-only message
- Updated `native/FileScout.Native/Shell/ContextMenuService.cpp` so read-only
  archive views expose `Open` for archived files as well as folders, while
  keeping mutation verbs hidden

### What was validated
- `cmd /c --% ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"` - pass
- Launch smoke for `native/x64/Debug/FileScout.Native/FileScoutNative.exe` - pass
- Local PowerShell probe of Windows `Shell.Application.NameSpace().CopyHere()` against temp ZIP file and folder entries - pass

### What remains unverified
- Manual activation of archived files from ZIP views in the running WinUI app
- Manual repeated-open behavior for the temp extraction workspace and resulting temp-file cleanup expectations
- Explicit extract/reveal UX, overwrite/conflict semantics, and non-ZIP archive formats (`7z`/`tar`)

### Blockers
- None

### Next recommended step
- Continue with `R11c`: explicit extract/reveal affordances and conflict semantics before widening archive coverage

## Slice R11c: Explicit Extract/Reveal Affordances And Conflict-Safe Extraction
Status: Complete
Last updated: 2026-03-18

### What was implemented
- Added `CopyItemFromPath` to
  `native/FileScout.Native/Shell/ShellFileOperationService.h` and
  `native/FileScout.Native/Shell/ShellFileOperationService.cpp` so archive
  extraction can copy a single shell item into a target directory under a
  caller-chosen output name
- Updated `native/FileScout.Native/Shell/ContextMenuService.cpp` so read-only
  archive views expose `Extract to archive folder` and `Extract and reveal`
  alongside the existing `Open` action
- Updated `native/FileScout.Native/MainWindow.xaml.h` and
  `native/FileScout.Native/MainWindow.xaml.cpp` with
  `ExtractArchiveItemAsync`, archive-relative path validation, and deterministic
  conflict handling: explicit extraction now preserves the in-archive relative
  path under the archive's parent folder and never overwrites an existing item,
  instead choosing a unique sibling name such as `name (2)`
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` so `Extract and reveal`
  navigates the active pane to the extracted item's parent directory and
  reselects the extracted file or folder after the shell copy completes

### What was validated
- `cmd /c --% ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"` - pass
- Launch smoke for `native/x64/Debug/FileScout.Native/FileScoutNative.exe` - pass

### What remains unverified
- Manual archive context-menu extraction flows in the running WinUI app for both files and folders
- Manual repeated extraction against existing filesystem items to confirm the unique-name conflict policy feels correct in practice
- Additional archive formats (`7z`/`tar`) and any richer extract destination picker UX

### Blockers
- None

### Next recommended step
- Continue with `R11d`: widen archive coverage beyond ZIP while preserving the current non-overwriting extract semantics

## Slice R11d: Shell-Capability-Based Archive Detection Beyond ZIP
Status: Complete
Last updated: 2026-03-19

### What was implemented
- Updated `native/FileScout.Native/Shell/ShellInteropService.h` and
  `native/FileScout.Native/Shell/ShellInteropService.cpp` so archive detection
  is no longer hardcoded to `.zip`
- Added a shell-capability probe plus cache in
  `native/FileScout.Native/Shell/ShellInteropService.cpp`; regular files are now
  treated as archive roots only when `SHCreateItemFromParsingName(...)->BindToHandler(BHID_EnumItems)`
  succeeds, which keeps normal files out while letting shell-backed archive
  formats participate automatically
- Updated archive-path resolution in
  `native/FileScout.Native/Shell/ShellInteropService.cpp` so the existing
  archive browse, open, temp-extract, and explicit extract/reveal flows work for
  any shell-enumerable archive root, not just ZIP

### What was validated
- `cmd /c --% ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"` - pass
- Launch smoke for `native/x64/Debug/FileScout.Native/FileScoutNative.exe` - pass
- Local PowerShell probe of `Shell.Application.NameSpace()` with temp `.zip` and `.tar` archives plus folder extraction from `.tar` - pass

### What remains unverified
- Manual browsing, file open, and explicit extraction for TAR archives in the running WinUI app
- Any archive formats that require third-party shell extensions (`7z`, `rar`, etc.) on machines where those extensions may or may not be installed
- Visual affordances that distinguish shell-backed archive files from ordinary files in non-archive directory listings

### Blockers
- None

### Next recommended step
- Continue with `R11e`: add explicit unsupported-archive feedback and polish for archive roots that the shell cannot enumerate

## Slice R11e: Unsupported-Archive Feedback And Archive Listing Polish
Status: Complete
Last updated: 2026-03-19

### What was implemented
- Extended `native/FileScout.Native/Domain/ShellItemRef.h` with
  `is_archive_candidate` and `is_supported_archive` flags so normal directory
  listings can distinguish archive-like files from ordinary files without
  crossing the domain/COM boundary
- Updated `native/FileScout.Native/Shell/ShellInteropService.cpp` to classify
  common archive-like filenames (`.zip`, `.tar`, `.7z`, `.rar`, `.tar.gz`,
  `.tgz`, `.bz2`, `.xz`, etc.), label them as `... archive`, and mark whether
  the host shell can actually enumerate them as browsable archive roots
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` so selection details and
  status text now surface archive-specific affordances in normal listings:
  supported archives show a browse hint, while unsupported archives explicitly
  explain that FileScout cannot browse them on the current machine and that the
  file is being opened with Windows instead
- Added deterministic coverage in
  `native/FileScout.Tests/tests/ShellInteropServiceTests.cpp` for archive-like
  classification and labeling in ordinary directory enumeration

### What was validated
- `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal"` - pass
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` - pass (`31` tests, `0` failures)

### What remains unverified
- Manual selection and double-click behavior for supported archives in a normal
  directory listing (confirm the new browse hint matches the actual transition
  into archive mode)
- Manual double-click behavior for unsupported archive-like files on a machine
  without third-party shell archive extensions (confirm the new status text is
  visible and the Windows fallback still feels intentional)
- Additional archive formats that depend on third-party shell extensions
  (`7z`, `rar`, etc.) across machines with different extension sets installed

### Blockers
- None

### Next recommended step
- Continue with `R12`: batch rename in the native shell

## Slice R12a: Multi-Selection Batch Rename Foundation
Status: Complete
Last updated: 2026-03-19

### What was implemented
- Added a pure sequential batch rename planner in
  `native/FileScout.Native/Domain/BatchRenamePlan.h` that preserves file
  extensions, numbers selections in list order, and blocks sibling-name
  collisions before any shell mutation runs
- Extended `native/FileScout.Native/Shell/ShellFileOperationService.h` and
  `native/FileScout.Native/Shell/ShellFileOperationService.cpp` with bulk
  `RenameItems` and `DeleteItems` helpers so multi-item mutations execute
  through one shell `IFileOperation`
- Updated `native/FileScout.Native/MainWindow.xaml.h` and
  `native/FileScout.Native/MainWindow.xaml.cpp` to switch file panes into
  extended selection mode, preserve multi-selection on right-click, surface
  multi-selection details, route `F2` / `Rename` into a batch rename dialog for
  multi-selection, and apply selection-aware copy/cut/delete/drag behavior
- Updated `native/FileScout.Native/Shell/ContextMenuService.h` and
  `native/FileScout.Native/Shell/ContextMenuService.cpp` so multi-selection
  context menus advertise `Batch rename...` and hide single-item-only inspect
  actions
- Added coverage in
  `native/FileScout.Tests/tests/ContextMenuServiceTests.cpp` and
  `native/FileScout.Tests/tests/ShellFileOperationServiceTests.cpp` for
  multi-selection menu shaping, sequential rename planning, bulk rename, and
  bulk delete behavior

### What was validated
- `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal"` - pass
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` - pass (`36` tests, `0` failures)

### What remains unverified
- Manual Ctrl/Shift multi-selection behavior across both panes and restored tabs
- Manual batch rename dialog behavior for mixed file/folder selections and larger selections
- Manual multi-item drag/drop and recycle-bin delete UX in the WinUI host

### Blockers
- None

### Next recommended step
- Continue with `R12b`: richer rename patterns and preview/polish for the batch rename dialog

## Slice R12b: Sequential Pattern Controls And Live Preview Polish
Status: Complete
Last updated: 2026-03-19

### What was implemented
- Extended `native/FileScout.Native/Domain/BatchRenamePlan.h` with
  `BatchRenameOptions` so the sequential planner now supports custom start
  numbers, optional zero padding, and the same preflight sibling-conflict
  checks before shell mutation
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` so the batch rename
  dialog now captures `Start number` and `Zero padding`, validates those fields
  live, disables the primary action while the plan is invalid, and renders a
  preview of the first few rename results before the shell operation runs
- Added coverage in
  `native/FileScout.Tests/tests/ShellFileOperationServiceTests.cpp` for custom
  numbering/padding output and rejection of invalid zero start numbers

### What was validated
- `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal"` - pass
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` - pass (`38` tests, `0` failures)

### What remains unverified
- Manual batch rename dialog layout and preview readability in the WinUI host,
  especially for larger selections
- Manual rename execution with non-default start-number and zero-padding values
  across mixed file/folder selections

### Blockers
- None

### Next recommended step
- Manually validate the polished batch rename dialog in the WinUI host, then
  continue with `R12c`: broader rename presets such as prefix/suffix or
  find/replace without widening into a free-form rename DSL

## Slice R12c: Prefix/Suffix Batch Rename Presets
Status: Complete
Last updated: 2026-03-19

### What was implemented
- Extended `native/FileScout.Native/Domain/BatchRenamePlan.h` with shared
  generated-name validation plus new `BuildPrefixBatchRenamePlan` and
  `BuildSuffixBatchRenamePlan` helpers so affix-based renames preserve file
  extensions, rename folders sensibly, and reuse the same duplicate/conflict
  checks as the sequential planner
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` so the batch rename
  dialog now includes a `Preset` picker with `Sequential numbers`, `Add
  prefix`, and `Add suffix` modes; the dialog switches its visible controls,
  reuses the existing live preview, and still routes execution through the same
  shell bulk-rename path
- Added coverage in
  `native/FileScout.Tests/tests/ShellFileOperationServiceTests.cpp` for prefix
  planning, suffix placement before file extensions, and sibling-conflict
  rejection for affix presets

### What was validated
- `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal"` - pass
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` - pass (`40` tests, `0` failures)

### What remains unverified
- Manual preset switching and preview readability in the WinUI batch rename
  dialog
- Manual prefix/suffix rename execution for mixed file/folder selections and
  names with multiple dots

### Blockers
- None

### Next recommended step
- Manually validate the affix presets in the WinUI host, then continue with
  `R12d`: a narrow find/replace preset without widening into a full rename DSL

## Slice R12d: Literal Find/Replace Batch Rename Preset
Status: Complete
Last updated: 2026-03-19

### What was implemented
- Extended `native/FileScout.Native/Domain/BatchRenamePlan.h` with a literal
  `BuildFindReplaceBatchRenamePlan` plus shared rename-context helpers so
  find/replace operates on folder names and file stems, preserves file
  extensions, rejects no-op selections, and still catches collisions against
  unchanged selected items
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` so the batch rename
  dialog now includes a `Find and replace` preset with dedicated `Find` and
  `Replace with` fields, preset-specific explanatory text, and the same live
  preview/validation flow used by the existing sequential and affix presets
- Tightened the shared batch-rename execution path in
  `native/FileScout.Native/MainWindow.xaml.cpp` so unchanged plan entries are
  skipped before calling the shell bulk rename operation
- Added coverage in
  `native/FileScout.Tests/tests/ShellFileOperationServiceTests.cpp` for literal
  find/replace behavior, no-op rejection, and duplicate-name detection when a
  changed item collides with an unchanged selected item

### What was validated
- `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal"` - pass
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` - pass (`43` tests, `0` failures)

### What remains unverified
- Manual `Find and replace` preset behavior in the WinUI dialog, especially
  empty replacement text and mixed file/folder selections
- Manual literal matching behavior for names with repeated segments or multiple
  dots

### Blockers
- None

### Next recommended step
- Manually validate the find/replace preset in the WinUI host, then continue
  with `R12e`: case-transform presets such as upper/lower/title case

## Slice R12e: Case-Transform Batch Rename Preset
Status: Complete
Last updated: 2026-03-19

### What was implemented
- Extended `native/FileScout.Native/Domain/BatchRenamePlan.h` with
  `BatchRenameCaseTransform` and `BuildCaseTransformBatchRenamePlan` so batch
  rename can apply lowercase, uppercase, or title case to folder names and
  file stems while preserving existing file extensions, rejecting no-op
  selections, and still catching duplicate-name collisions
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` so the batch rename
  dialog now includes a `Change case` preset plus a secondary `Case` picker for
  `lowercase`, `UPPERCASE`, and `Title Case`, reusing the existing live preview
  and shared shell bulk-rename execution path
- Added coverage in
  `native/FileScout.Tests/tests/ShellFileOperationServiceTests.cpp` for case
  transforms across files and folders, title-case behavior, no-op rejection,
  and duplicate detection when case normalization would collapse names

### What was validated
- `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal"` - pass
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` - pass (`47` tests, `0` failures)

### What remains unverified
- Manual `Change case` preset behavior in the WinUI dialog, especially preset
  switching between lowercase, uppercase, and title case on mixed file/folder
  selections
- Manual title-case readability for names that include punctuation, repeated
  separators, or non-ASCII characters

### Blockers
- None

### Next recommended step
- Manually validate the full batch rename dialog in the WinUI host, then decide
  whether `R12` is ready to close or needs one final narrow preset/polish slice

## Slice R12f: Shared Windows Filename Validation Parity
Status: Complete
Last updated: 2026-03-19

### What was implemented
- Added `native/FileScout.Native/Domain/FileNameValidation.h` as a pure shared
  validator for Windows file-name rules, including control characters,
  reserved device names, `.` / `..`, and trailing space/period rejection
- Updated `native/FileScout.Native/Domain/BatchRenamePlan.h` so batch rename
  preview and preflight validation now use the shared validator instead of a
  local duplicate helper
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` and
  `native/FileScout.Native/MainWindow.xaml.h` so single-item `New folder` and
  `Rename` flows reuse the same shared validation path and no longer drift from
  batch rename behavior
- Added coverage in
  `native/FileScout.Tests/tests/ShellFileOperationServiceTests.cpp` for shared
  file-name validation and the batch-rename validation hook that consumes it

### What was validated
- `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal"` - pass (with only the pre-existing `MSB8027` / `LNK4042` WinAppSDK duplicate-object warnings)
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` - pass (`50` tests, `0` failures)

### What remains unverified
- Manual batch rename dialog behavior when affix/find-replace presets generate
  invalid names and the dialog now blocks the operation preflight
- Manual single-item `New folder` / `Rename` behavior for the new `.` / `..`
  and trailing space/period validation messages in the WinUI host

### Blockers
- None

### Next recommended step
- Manually validate the full batch rename dialog plus single-item rename/create
  error states in the WinUI host, then decide whether `R12` is ready to close
  or needs one final UX-only polish slice

## Slice R12g: Batch Rename Dialog Layout And Preview Polish
Status: Complete
Last updated: 2026-03-19

### What was implemented
- Reworked `native/FileScout.Native/MainWindow.xaml.cpp` so the batch rename
  dialog now opens with a structured selection summary that surfaces item
  counts, folder/file breakdown, current folder, and representative sample
  names before the user edits any preset fields
- Replaced the prior flat control stack with a two-column WinUI dialog layout:
  a dedicated `Rename setup` panel for preset-specific inputs and a separate
  `Live preview` panel for result review, both wrapped in themed cards with
  clearer visual hierarchy
- Upgraded preview feedback in
  `native/FileScout.Native/MainWindow.xaml.cpp` so invalid states disable the
  primary action with explicit error text, while valid states show compact
  preview cards that distinguish renamed items from unchanged ones and summarize
  how many generated entries are ready
- Wrapped the dialog content in scrollable containers so larger selections keep
  the preview readable without collapsing the control layout on shorter windows

### What was validated
- `cmd /c --% ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal"` - pass (with only the pre-existing `MSB8027` / `LNK4042` WinAppSDK duplicate-object warnings)
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` - pass (`50` tests, `0` failures)

### What remains unverified
- Manual readability and spacing of the redesigned batch rename dialog in the
  running WinUI host, especially on shorter window heights and larger
  multi-selection sets
- Manual preset switching flow to confirm the new setup/preview split feels
  natural when moving between sequential, affix, find/replace, and case-change
  modes
- Manual confirmation that the preview card treatment makes unchanged items and
  blocked invalid states feel obvious enough without extra copy

### Blockers
- None

### Next recommended step
- Manually validate the redesigned batch rename dialog in the running WinUI
  host, then close `R12` and move to `R13` if the host-level UX holds up

## Launch Crash Fix (post-R10 infrastructure fix)
Status: Complete
Last updated: 2026-03-18

### Root cause
Unpackaged WinUI 3 C++ apps need `resources.pri` in the output directory for
`Application::LoadComponent(ms-appx:///FileScoutNative/MainWindow.xaml)` to resolve the
XBF path at runtime. The MSBuild `AppxPackage` targets (which normally run `makepri.exe`)
are only imported when `WindowsAppContainer=true`, which is not set for desktop apps.
As a result, `resources.pri` was never generated, causing `InitializeComponent` to crash.

Additionally, `App.xaml` had `XamlControlsResources` which tried to load
`ms-appx:///Microsoft.UI.Xaml/Themes/themeresources.xaml`. Without `resources.pri`
in the app directory, the MRT resource manager failed to initialize, making that
URI unresolvable — silently swallowing startup and preventing `OnLaunched` from running.

### Fix
1. Added `GenerateResourcesPri` MSBuild target to `FileScout.Native.vcxproj`: runs
   `makepri.exe createconfig` + `makepri.exe new` after every build to generate
   `resources.pri` in the output directory (`AfterTargets="CopyFilesToOutputDirectory"`).
2. Restored `XamlControlsResources` in `App.xaml` — it works correctly now that
   `resources.pri` is present and MRT Core can resolve `ms-appx:///` URIs.

### What was validated
- Clean build with auto-generated `resources.pri` — pass
- App launches, shows main window, loads directory listing (`C:\Users\machi`, 108 items) — pass
- No entries in startup error log — pass

### Remaining items to verify
- Manual inspection of R9b (Cards/List toggle), R10 (image preview, created date, attributes)

## Perf Fix: Background Directory Enumeration
Status: Complete
Last updated: 2026-03-18

### What was implemented
- `LoadDirectory` is now a thin wrapper that calls `LoadDirectoryAsync` (fire_and_forget)
- `LoadDirectoryAsync` clears UI immediately on the UI thread, then `co_await resume_background()`
  to enumerate on a background thread; posts results back via `m_dispatcherQueue.TryEnqueue`
- Per-pane `load_generation` counter discards stale results from rapid navigation
- `m_dispatcherQueue` captured in constructor via `DispatcherQueue::GetForCurrentThread()`
- `ShellInteropService::m_iconCache` protected by `std::mutex m_cacheMutex` for
  concurrent two-pane background enumeration
- `selectAfter` optional path parameter added to `LoadDirectory` / `LoadDirectoryAsync`
  to replace the prior `LoadDirectory(); SelectItemByPath()` pattern
- `UpdateRows` and `SetSortColumn` updated to take `paneIndex` parameter
- Dark theme applied: `RequestedTheme="Dark"` on Application, `MicaBackdrop` system
  backdrop, all hardcoded light hex colors replaced with `{ThemeResource}` references

### Attempted and reverted: ContainerContentChanging virtualization
- Tried replacing UIElement items with boxed int32 indices + `ContainerContentChanging`
  to create rows on demand — ListView rendered items as raw number strings; reverted
- `UpdateRows` and `RebuildAllTabRowsForPane` now use `CreateRowView` directly (same
  as before), keeping background enumeration as the sole perf improvement this slice

### What was validated
- Clean build (`0 Error(s)`, `0 Warning(s)`) — pass
- App now loads directories without freezing the UI thread

### Blockers
- None

### Next recommended step
- Continue with `R11`: archive mode (browse zip/7z/tar as virtual views)

## Slice R16a: Visual Polish — Chrome Restructure
Status: Complete
Last updated: 2026-03-21

### What was implemented
- Removed hero card from `MainWindow.xaml`; reduced `Grid.RowDefinitions` from 4 to 3
- Added `TitleBarDragHost` as a named draggable title-bar region with
  `TitleBarLeftInsetColumn` / `TitleBarRightInsetColumn` inset columns
- Added `ConfigureTitleBar()` in `MainWindow.xaml.cpp`: extends app content into the
  title bar via `SetTitleBar(TitleBarDragHost())`, wires `AppWindow`, and handles
  `TitleBarDragHost_Loaded` / `TitleBarDragHost_SizeChanged` to track caption button insets
- Merged toolbar into title-bar row: FileScout + ALPHA badge branding in leftmost
  column, Back/Forward/Refresh with Fluent glyph icons (`E76B`/`E76C`/`E72C`),
  address text box; `ChromeToolbarButtonStyle` / `ChromeAddressTextBoxStyle` styles added
- Updated `UpdateActivePaneIndicator()` to manage `BorderThickness` as well as
  `BorderBrush` — active pane gets 2px `AccentFillColorDefaultBrush`, inactive pane gets
  1px `CardStrokeColorDefaultBrush` (set in both XAML defaults and runtime toggle)
- Added `BuildSidebarFontIcon()` and `KnownFolderGlyph()` helpers; updated
  `BuildSidebarGlyph()` with shell-backed-first priority: shell-extracted bitmap →
  Fluent glyph fallback → text monogram; `BuildSidebarDriveGlyph()` now tries
  shell-extracted icon then falls back to `EDA2` HardDrive glyph

### What was validated
- Build: pass (0 errors, only pre-existing MSB8027/LNK4042 warnings)
- Tests: 57 passed, 0 failures
- Smoke validation: app launched and showed window with integrated title bar (dark mode)

### What remains unverified
- Light-theme validation (sidebar bg, title-bar, pane borders) in Windows Settings
- Drag region interaction inside the title-bar host (click-drag to move window,
  caption button coexistence) — blocked by existing `filescout-ui-automation` bug

### Blockers
- None

### Next recommended step
- Continue with `R16b`: sidebar background fix

## Slice R16b: Explorer Chrome — Sidebar Background
Status: Complete
Last updated: 2026-03-21

### What was implemented
- Changed sidebar `Border` background in `MainWindow.xaml` from
  `LayerFillColorAltBrush` to `CardBackgroundFillColorDefaultBrush`; this gives
  the left sidebar more visible separation from the Mica backdrop in both dark
  and light themes, matching the Explorer-inspired chrome goal
- No code-side icon changes needed: `BuildSidebarGlyph()` was already
  shell-backed-first (shell bitmap → Fluent glyph fallback → text monogram)
- Updated docs: slices.md closed R12, added R16 entry (R16a + R16b), plan.md updated

### What was validated
- Build: pass (0 errors, only pre-existing MSB8027/LNK4042 warnings)
- Tests: 57 passed, 0 failures

### What remains unverified
- Manual sidebar contrast in dark theme vs Mica backdrop
- Manual sidebar contrast in light theme
- Manual validation of all R16 spec success criteria (white frame gone, sidebar
  closer to Explorer, pane borders correct, Fluent glyph toolbar icons visible)

### Blockers
- None

### Next recommended step
- Manually validate the R16 visual changes in the running WinUI host (both themes),
  then run the performance benchmark acceptance measurements

## Perf: Staged Row Publication (Tasks 1–3)
Status: In Progress
Last updated: 2026-03-21

### What was implemented
- Added `awaiting_final_batch` and `user_interacted_after_first_batch` bool fields
  to `PaneContext` in `native/FileScout.Native/MainWindow.xaml.h`
- Replaced the single `UpdateRows()` call in `LoadDirectoryAsync`'s UI-thread callback
  (`native/FileScout.Native/MainWindow.xaml.cpp`) with a staged publish:
  - First dispatch builds rows for `min(150, total)` items (preserving group-label
    continuity), records `first_batch_publish_tick`, resets
    `user_interacted_after_first_batch = false`
  - For folders ≤150 items: both timing points set to same tick (existing behaviour)
  - For folders >150 items: `awaiting_final_batch = true`, then a second
    `TryEnqueue` dispatch appends the remaining rows, records `final_publish_tick`,
    and calls `SelectItemByPath` only if `!user_interacted_after_first_batch`
- Added `pane.awaiting_final_batch = false` at start of `RebuildAllTabRowsForPane`
  so any synchronous sort/group/viewmode change cancels the pending second dispatch
  (prevents duplicate row appends)
- Added `ActivePane().user_interacted_after_first_batch = true` in
  `FileRowsListView_SelectionChanged` after the `tab_switching` early-return guard,
  so live user selection wins over deferred `SelectItemByPath` restoration

### What was validated
- Build: pass (0 errors, only pre-existing MSB8027/LNK4042 warnings)
- Tests: 57/57 pass (`native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no`)

### What remains unverified
- Manual runtime benchmark: 5× measurements of first-batch and full-completion
  times on a 1k–5k item folder before and after staged publish
  (spec: ≥25% or 150ms improvement on first-batch median; ≤10% regression on full-completion)
- Manual validation: first rows appear earlier on large folders; pane remains
  responsive while remaining rows complete; no stale-load flashes; no selection/scroll
  clobber if user interacts after first batch appears; final ordering stable
- Task 4 in `docs/superpowers/plans/2026-03-20-performance-benchmark.md` must be
  run in the host to close this slice

### Blockers
- None (code complete; acceptance requires manual app execution)

### Next recommended step
- Launch `native/x64/Debug/FileScout.Native/FileScoutNative.exe`, navigate to a
  folder with 1k–5k items, observe TraceLog output for `PerfLoad … first_batch_ms`
  vs `full_completion_ms` to capture before/after baseline, then record results and
  close the perf slice

## Slice R17a: FuzzyMatcher Header And Unit Tests
Status: Complete
Last updated: 2026-03-22

### What was implemented
- Added STL headers (`<atomic>`, `<chrono>`, `<functional>`, `<latch>`,
  `<memory>`, `<queue>`, `<thread>`) to `native/FileScout.Tests/pch.h` for
  upcoming SearchService tests (R17b)
- Created `native/FileScout.Native/Search/FuzzyMatcher.h` — pure C++ header,
  stack-only, no WinRT dependency. `FuzzyScore(query, filename) → int` with
  prefix bonus (+20), consecutive-run bonus (+15/run), word-boundary bonus (+8),
  and match-ratio penalty. Returns –1 for query length < 2 or no subsequence
  match. Wildcards treated as literals.
- Created `native/FileScout.Tests/tests/FuzzyMatcherTests.cpp` with 7 tests
  covering empty/single-char queries, no-match, prefix advantage, consecutive
  advantage, and wildcard literal treatment (positive and negative)
- Registered `Search\FuzzyMatcher.h` in `FileScout.Native.vcxproj` (ClInclude)
- Registered `tests\FuzzyMatcherTests.cpp` in `FileScout.Tests.vcxproj` (ClCompile)

### What was validated
- Build: PASS (64 total, 0 errors; pre-existing MSB8027/LNK4042 warnings only)
- Tests: 64/64 PASS (57 pre-existing + 7 new FuzzyMatcher tests)

### What remains unverified
- Nothing — pure C++ scorer with complete unit test coverage

### Blockers
- None

### Next recommended step
- Start R17b: `SearchService` background walk + unit tests (synchronous PostFn)

## Slice R17b: SearchService Background Walk And Unit Tests
Status: Complete
Last updated: 2026-03-22

### What was implemented
- Created `native/FileScout.Native/Search/SearchService.h` — interface with
  `ResultCallback`, `CompleteCallback`, `PostFn` typedefs; `shared_ptr<atomic<bool>>`
  cancellation token; min-heap top-200 with `k_max_depth=12`, `k_heap_cap=200`
- Created `native/FileScout.Native/Search/SearchService.cpp` — `WalkDir` recursive
  walk via `directory_iterator` (skip_permission_denied), min-heap displacement with
  `cap_reached` flag, generation-counter stale-result guard, final flush + complete
  callback, destructor join
- Created `native/FileScout.Tests/tests/SearchServiceTests.cpp` with 7 tests:
  CancelBeforeFirstResult, CancelMidWalk, CapEnforcement, Exact200NoCap,
  TieTruncationNoCap, DepthLimit, StartCancelStartNoCrash
- Registered new files in `FileScout.Native.vcxproj` and `FileScout.Tests.vcxproj`
- Added `<atomic>`, `<chrono>`, `<functional>`, `<thread>` to main `pch.h`

### Deviations from plan
- `std::latch::wait()` in MSVC C++20 takes no timeout argument; removed `chrono::seconds`
  timeout args from all 4 `done.wait()` calls
- `CapEnforcement` test: original used 500 identical-scoring files which can never
  trigger `cap_reached` (ties don't displace). Fixed to use 300 `ama_*.txt` (score 14
  for "ma") + 300 `match_*.txt` (score 34 for "ma"); NTFS alphabetical iteration ensures
  low-scored files fill the heap first, then high-scored files displace them

### What was validated
- Build: PASS (0 errors; pre-existing MSB8027/LNK4042 warnings only)
- Tests: 71/71 PASS (64 pre-existing + 7 new SearchService tests)

### What remains unverified
- Nothing — pure C++ background service with complete unit test coverage

### Blockers
- None

### Next recommended step
- Start R17c: address bar state machine (SearchMode enum, PaneContext search fields,
  Ctrl+F handler, TextChanged filter, Escape restore, nav/tab cancel guards)

## Slice R17c: SearchFilter Mode — Address Bar State Machine
Status: Complete
Last updated: 2026-03-22

### What was implemented
- Added `SearchMode` enum `{ None, Filter, Recursive }` to `MainWindow.xaml.h`
- Added search state fields to `PaneContext`: `search_mode`, `search_query`,
  `search_snapshot`, `search_service` (owns a `SearchService` instance)
- Added `SearchAccelerator_Invoked` and `AddressTextBox_TextChanged` public
  handler declarations; added `#include "Search/SearchService.h"` to header
- Added `#include "Search/FuzzyMatcher.h"` to `MainWindow.xaml.cpp`
- `SearchAccelerator_Invoked`: enters Filter mode, snapshots current tab items,
  clears address bar with contextual placeholder, focuses TextBox
- `AddressTextBox_TextChanged`: scores snapshot items with `FuzzyScore`, shows
  top-200 sorted by score; shows all unfiltered for queries shorter than 2 chars
- `AddressTextBox_KeyDown`: Escape in Filter/Recursive cancels walk, restores
  ListView from snapshot, resets address bar text and placeholder
- `SetActivePaneIndex`: cancels search on outgoing pane before switching
- `PaneTabView_SelectionChanged`: cancels search before tab switch
- Wired Ctrl+F via `SetupKeyboardAccelerators` (code-behind), NOT XAML markup

### Deviations from plan
- `Window.KeyboardAccelerators` XAML block not supported by WinUI 3 XAML compiler
  (error WMC0011). Wired Ctrl+F via `SetupKeyboardAccelerators()` in code-behind —
  semantically identical, consistent with all other keyboard accelerators
- `item.name` → `item.display_name`: plan referenced non-existent field; correct
  field name is `display_name` in `FileScoutNative::Domain::ShellItemRef`

### What was validated
- Build: PASS (0 errors; pre-existing MSB8027/LNK4042 warnings only)
- Tests: 71/71 PASS (no regression)

### What remains unverified
- Manual smoke: Ctrl+F in running app enters filter mode, address bar clears with
  placeholder, typing filters the file list, Escape restores the original list
- Behavior when TextBox is focused and Ctrl+F fires (WinUI 3 may route the key
  to the TextBox first — if so, add ProcessKeyboardAccelerators on TextBox)
- Pane-switch and tab-switch cancel behavior in the running app

### Blockers
- None

### Next recommended step
- Start R17d: SearchRecursive mode — Enter escalation, ProgressBar, streaming
  results from SearchService into the ListView

## Slice R17d: SearchRecursive Mode — Enter Escalation And Streaming Results
Status: Complete
Last updated: 2026-03-22

### What was implemented
- Added per-pane `ProgressBar` (`LeftPaneSearchProgress` / `RightPaneSearchProgress`)
  in XAML — third row (Auto) between sort header and TabView, `Visibility=Collapsed`
- Added `SearchProgressForPane(int32_t)` accessor helper in header + cpp
- `AddressTextBox_KeyDown`: Enter in Filter mode with query ≥ 2 chars escalates to
  Recursive mode — shows ProgressBar, sets address bar read-only to "Search: <query>",
  starts `SearchService::Start()` with `m_dispatcherQueue.TryEnqueue` as PostFn;
  streaming `on_results` replaces ListView rows on each batch; `on_complete` hides
  ProgressBar and updates status bar (cap warning or "N matches in <path>")
- Escape handler extended: hides ProgressBar before restoring ListView
- `LoadDirectory`: exits search mode at start — cancels walk, hides ProgressBar,
  resets address bar read-only/placeholder before navigating
- `SetActivePaneIndex`: hides ProgressBar when cancelling outgoing pane search
- `PaneTabView_SelectionChanged`: hides ProgressBar when cancelling search on tab switch

### Deviations from plan
- Plan used `tmp.name` and `tmp.full_path` (non-existent fields); corrected to
  `tmp.display_name` and `tmp.path` per `ShellItemRef` struct definition
- `SearchProgressForPane` added to `.h` as private member (plan omitted the declaration)

### What was validated
- Build: PASS (0 errors; pre-existing MSB8027/LNK4042 warnings only)
- Tests: 71/71 PASS (no regression)

### What remains unverified
- Manual smoke: Enter from Filter mode starts recursive walk, ProgressBar appears,
  results stream in, status bar updates live, Escape exits cleanly
- Double-click on a recursive result to navigate (not yet wired — R17e)
- Relative-path subtitle in row view (not yet implemented — R17e)

### Blockers
- None

### Next recommended step
- Start R17e: result row polish — bold highlights, relative-path subtitle, double-click
  navigation from Recursive results

## Slice R17e: Result Row Polish And Navigate-On-Select
Status: Complete
Last updated: 2026-03-22

### What was implemented
- Added `search_results` field to `PaneContext` (parallel to ListView rows in Recursive mode)
- Added `CreateSearchRowView(SearchResult, searchRoot)` helper: builds a StackPanel with a
  filename TextBlock (size 13) and, when non-trivial, a relative-path subtitle TextBlock
  (size 11, opacity 0.6) showing the directory relative to the search root
- `on_results` callback now calls `CreateSearchRowView` for each result and stores the
  batch into `p.search_results` for navigate-on-select indexing
- `FileRowsListView_DoubleTapped`: guard at top — when in Recursive mode, resolves the
  tapped row index into `search_results`, then calls `LoadDirectory(parent, fullPath)`;
  `LoadDirectory`'s own search-teardown (added R17d) handles mode reset + ProgressBar hide

### Deviations from plan
- Plan duplicated search-mode teardown before `LoadDirectory`; omitted since `LoadDirectory`
  already handles it (added in R17d) — DRY, no behaviour change
- `listMode` variable removed from `on_results` lambda; `CreateSearchRowView` always
  uses its own StackPanel layout regardless of view mode

### What was validated
- Build: PASS (0 errors)
- Tests: 71/71 PASS (no regression)

### What remains unverified
- Manual smoke: double-click a Recursive result navigates to its parent and selects the file
- Relative-path subtitle visible in streamed rows
- Bold highlight on matched characters (deferred — not in current spec)

### Blockers
- None

### Next recommended step
- R17 fuzzy search initiative is code-complete (R17a–R17e); manual smoke test recommended

## Slice R18a: Drive Icon Bug Fix
Status: Complete
Last updated: 2026-03-22

### What was implemented
- `Shell/ShellInteropService.cpp`: `ExtractShellIconBitmap` gains `bool useLiteralPath = false`
  parameter; when true, omits `SHGFI_USEFILEATTRIBUTES` flag so shell uses the actual file path
  for icon lookup instead of a type-hint string
- `GetIconForItem`: detects drive roots via `path == path.root_path() && !path.root_name().empty()`,
  sets `useLiteralPath = true` and passes `path.wstring()` as the lookup name so `C:\` gets the
  real drive icon instead of the generic folder icon

### Deviations from plan
- None

### What was validated
- Build: PASS (0 errors)
- Tests: 71/71 PASS

### What remains unverified
- Manual smoke: drive roots (C:\, D:\) show correct drive icon in the sidebar / drives view

### Blockers
- None

### Next recommended step
- R18b: breadcrumb nav bar + dedicated search box

## Slice R18b: Breadcrumb Navigation Bar And Dedicated Search Box
Status: Complete
Last updated: 2026-03-22

### What was implemented
- `MainWindow.xaml`: toolbar row replaced with 3-column layout — nav buttons / breadcrumb+address
  overlay / dedicated SearchTextBox (Width=220)
- `BreadcrumbBarBorder` (Border+ScrollViewer+StackPanel) shown by default; `AddressTextBox`
  collapsed behind it, revealed on F4 or tap
- `SearchTextBox` always visible; `PlaceholderText` updated to `"Search <folder>…"` on navigation
- `MainWindow.xaml.h`: added `BreadcrumbBarBorder_Tapped`, `SearchTextBox_KeyDown`,
  `SearchTextBox_TextChanged`, `UpdateBreadcrumbs`, `ShowAddressTextBox`, `HideAddressTextBox`,
  `UpdateSearchPlaceholder`; removed `AddressTextBox_TextChanged` declaration
- `MainWindow.xaml.cpp`:
  - `UpdateBreadcrumbs`: builds ancestor list root→current, renders HyperlinkButton per segment
    with click→`LoadDirectory` wiring and `›` separators
  - `ShowAddressTextBox` / `HideAddressTextBox`: toggle visibility of breadcrumb vs address overlay
  - `AddressTextBox_KeyDown`: stripped to Escape→hide + Enter→navigate (search logic moved out)
  - `SearchTextBox_TextChanged`: auto-enters Filter mode when text non-empty + `search_mode == None`
  - `SearchTextBox_KeyDown`: Escape exits search/hides ProgressBar; Enter→Recursive escalation
  - `SearchAccelerator_Invoked` (Ctrl+F): now focuses `SearchTextBox()` instead of `AddressTextBox()`
  - F4 KeyboardAccelerator added: calls `ShowAddressTextBox()`
  - `LoadDirectory`: calls `UpdateBreadcrumbs`, `UpdateSearchPlaceholder`, `HideAddressTextBox`,
    resets SearchTextBox text+IsReadOnly
  - `SetActivePaneIndex`: syncs breadcrumbs, placeholder, and SearchTextBox state from incoming pane
  - `LoadDrivesView`: clears BreadcrumbPanel, sets placeholder to "Search This PC…"
  - Title bar drag regions updated to include BreadcrumbBarBorder and SearchTextBox

### Deviations from plan
- `PaneSelectionState.primary_index` does not exist; type-ahead uses
  `pane.active_list_view.SelectedIndex()` instead

### What was validated
- Build: PASS (0 errors)
- Tests: 71/71 PASS

### What remains unverified
- Manual smoke: breadcrumb segments clickable and navigate correctly; F4 shows address overlay;
  Escape hides it; SearchTextBox placeholder updates on folder change; Ctrl+F focuses search

### Blockers
- None

### Next recommended step
- R18c: keyboard type-ahead in file list

## Slice R18c: Keyboard Type-Ahead In File List
Status: Complete
Last updated: 2026-03-22

### What was implemented
- `CreateTabListView`: wires `listView.KeyDown` → `FileRowsListView_KeyDown(args)` lambda
- `FileRowsListView_KeyDown`: handles VK codes 65–90 (A–Z); in non-search mode, walks
  `pane.items` starting from `active_list_view.SelectedIndex() + 1` (wrapping), matches first
  char case-insensitively via `std::towlower`; calls `SelectItemByIndex` + `ScrollIntoView`;
  sets `args.Handled(true)` to prevent ListView's own key handling
- `MainWindow.xaml.h`: `FileRowsListView_KeyDown` declared as private method

### Deviations from plan
- Plan used `pane.selection.primary_index` (non-existent); replaced with
  `pane.active_list_view.SelectedIndex()`

### What was validated
- Build: PASS (0 errors)
- Tests: 71/71 PASS

### What remains unverified
- Manual smoke: pressing a letter in the file list jumps to first matching item (wrapping)

### Blockers
- None

### Next recommended step
- R18 chrome UX redesign initiative is code-complete (R18a–R18c); manual smoke test recommended
- Next initiatives: async icon loading (perf), R13 command palette (spec needed)
- Next automatable slice: R13 command palette (requires spec before implementation)
