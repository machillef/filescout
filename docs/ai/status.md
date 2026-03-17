# FileScout Native Rewrite Status

## Initiative State

- Active track: `native/` C++20 + C++/WinRT + WinUI 3
- Target: unpackaged Windows 11 x64 desktop app
- `native/` is now tracked in git; unrelated Rust work remains untouched
- Legacy Rust prototype under `crates/` is reference material only and is no
  longer part of the active implementation plan
- Current phase: Phase 1, Explorer parity foundation
- Current blocker: none
- Current known warning: none in the current native debug build

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
