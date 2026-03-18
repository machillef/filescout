# FileScout Native Rewrite Status

## Initiative State

- Active track: `native/` C++20 + C++/WinRT + WinUI 3
- Target: unpackaged Windows 11 x64 desktop app
- `native/` is now tracked in git; unrelated Rust work remains untouched
- Legacy Rust prototype under `crates/` is reference material only and is no
  longer part of the active implementation plan
- Current phase: Phase 1, Explorer parity foundation
- Current blocker: none
- Current known warning: MSB8027 + LNK4042 for WindowsAppRuntimeAutoInitializer.obj (duplicate obj, extras ignored — harmless, pre-existing WinAppSDK issue)
- App launches and shows window as of 2026-03-18 (launch crash fixed)

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
