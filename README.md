# FileScout

FileScout is a Windows 11 file explorer built with C++/WinRT and WinUI 3. It focuses on native shell integration, fast keyboard-driven workflows, and a modern two-pane interface that feels at home on Windows while still pushing toward power-user features.

## Highlights

- Shell-backed browsing, metadata, icons, and file operations
- Dual-pane and multi-tab workflow with active-pane routing
- Keyboard shortcuts, breadcrumb/address navigation, and fuzzy search
- Native shell hand-off for Properties, Open With, and archive flows
- Fuzzy search with live filtering, recursive search, and result activation

## What FileScout Can Do Today

### Navigation and workspace

- Browse files and folders with shell-backed rows, native icons, selection, and folder navigation
- Work across dual panes with independent tab sets and active-pane routing
- Navigate with breadcrumb segments, drive-root icons, address entry, back/forward, and type-ahead
- Use sidebar destinations including Home, Desktop, Documents, Downloads, Pictures, Music, Videos, This PC, Recycle Bin, and drives
- Pin folders into the sidebar and switch between Cards and List views with per-pane sort and grouping controls

### File operations and shell integration

- Open items, open with alternate apps, and show Properties through native shell pathways
- Create folders, rename items, delete to the recycle bin, and use copy, cut, and paste workflows
- Drag and drop within FileScout and between FileScout and Windows Explorer
- Show Windows 11-style context menus with shell overflow actions

### Search and discovery

- Filter the current pane with fuzzy matching and ranked results
- Run recursive search with streaming updates, capped result messaging, and navigate-on-select behavior
- Show search result subtitles and highlight positions shaped for the current UI
- Use dedicated search-box flows without losing the current pane snapshot

### Power-user tools

- Use keyboard shortcuts including `Ctrl+C`, `Ctrl+X`, `Ctrl+V`, `Delete`, `F2`, `F5`, `Ctrl+T`, `Ctrl+W`, `Ctrl+Tab`, `Alt+Enter`, `Alt+Left`, and `Alt+Right`
- Browse with custom title bar chrome, active-pane indicators, and shell-backed sidebar visuals

## Not Included Yet

A few previously prototyped features stay out of the shipped app while the core explorer hardens:

- Command palette
- In-app archive browsing and extract flows
- Batch rename (rename works one item at a time)

## Build and Test

### Prerequisites

- Windows 11 x64
- [Microsoft Windows App Runtime 2.x (2.6 or later)](https://aka.ms/windowsappsdk) installed separately
- Visual Studio 2022 or Visual Studio Build Tools with:
  - Desktop development with C++
  - Windows 11 SDK (`10.0.22621` or later)
  - C++ WinUI / Windows App SDK support

### Restore

```powershell
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true"
```

### Build

```powershell
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"
```

App binary output:

`native/x64/Debug/FileScout.Native/FileScoutNative.exe`

### Test

```powershell
native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no
```

## Project Layout

- `native/FileScout.Native/` - WinUI 3 application, shell integration, view models, and UI workflows
- `native/FileScout.Tests/` - GoogleTest-based native test suite
- `docs/` - design notes, the overhaul decision log, and initiative records
- `assets/` - project assets used by the app and repository

## Status

FileScout is an early preview under active development. Releases are published as pre-releases on GitHub. The focus is a trustworthy core explorer: shell-backed browsing, search, dual-pane workflows, and everyday file operations.

## License

MIT
