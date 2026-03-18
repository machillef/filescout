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

## What's next

- Favorites, sidebar tree, and pinned locations (R8)
- Sort, group, and view mode parity (R9)
- Preview and metadata panel (R10)
- Archive browsing, batch rename, command palette (R11–R13)

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
