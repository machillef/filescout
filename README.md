# FileScout

A Windows 11 file explorer built with C++/WinRT and WinUI 3.

> **Status: active development** — Explorer parity foundation is in place; power features are in progress.

## What works today

- Shell-backed file pane with native icons, selection, and folder navigation
- Windows 11–style context menu with shell overflow (`More Windows actions...`)
- Shell-backed file operations: open, open with, properties, new folder, rename, delete (recycle bin), copy, cut, paste
- Native drag-and-drop within the app and with Windows Explorer
- Keyboard shortcuts: Ctrl+C/X/V, Delete, F2, F5, Ctrl+T, Ctrl+W, Ctrl+Tab
- Multi-tab pane (Ctrl+T to open, Ctrl+W to close)
- Dual-pane layout with independent tab sets and active-pane routing
- Sidebar navigation: Home, Desktop, Documents, Downloads, Pictures, Music, Videos, This PC, Recycle Bin, drives; pin any folder from the context menu
- Sort by name/size/date (column headers), group toggle, Cards and List view modes — all per-pane
- Sidebar image preview with created date, file attributes; metadata inspect surface
- Archive browsing: ZIP and shell-detected archives open as virtual views; extract-to affordances
- Batch rename: sequential numbering, prefix/suffix presets, find-and-replace, live preview and validation
- Explorer-style chrome: custom title bar, Fluent icon glyphs, 2-pane active indicator, shell-backed sidebar icons
- Breadcrumb address bar with clickable path segments and drive root icons
- Dedicated search box with fuzzy matching, streaming results, Filter and Recursive modes, keyboard type-ahead

## What's next

- Command palette (R13) — write spec first, then implement
- Git-aware and power-user workflows (R14)
- AI features (R15)

## Prerequisites

- Windows 11 x64
- [Microsoft Windows App Runtime 1.8](https://aka.ms/windowsappsdk) (framework package, installed separately)
- Visual Studio 2022 or VS Build Tools with:
  - Desktop development with C++
  - Windows 11 SDK (10.0.22621 or later)
  - C++ WinUI / Windows App SDK support

## Build

**Restore NuGet packages:**
```
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true"
```

**Build (Debug):**
```
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"
```

The binary lands at `native/x64/Debug/FileScout.Native/FileScout.Native.exe`.

## Stack

- Language: C++20
- UI: WinUI 3 (Windows App SDK 1.8)
- Shell integration: C++/WinRT, `IFileOperation`, `IContextMenu`, `IShellItem`
- Packaging: unpackaged desktop app

## License

MIT
