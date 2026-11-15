# FileScout Native

Windows 11 file explorer built with C++/WinRT + WinUI 3.

## Current state (R7 complete)

| Feature | Status |
|---|---|
| Shell-backed file pane + native icons | Done |
| Windows 11 context menu + shell overflow | Done |
| Open / Open with / Properties | Done |
| New folder / Rename / Delete (recycle bin) | Done |
| Copy / Cut / Paste (shell clipboard) | Done |
| Drag-and-drop (in-app and cross-app) | Done |
| Keyboard shortcuts (Ctrl+C/X/V, Del, F2, F5) | Done |
| Multi-tab pane (Ctrl+T / Ctrl+W) | Done |
| Dual-pane layout (Ctrl+Tab to switch panes) | Done |
| Favorites / sidebar tree | Next (R8) |

## Prerequisites

- Windows 11 x64
- [Windows App Runtime 2.x (2.6 or later)](https://aka.ms/windowsappsdk) installed
- Visual Studio 2022 or Build Tools with **Desktop development with C++** and **Windows 11 SDK**

## Restore

```
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true"
```

## Build

```
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"
```

Binary: `native/x64/Debug/FileScout.Native/FileScout.Native.exe`

## Structure

```
native/FileScout.Native/
├── Domain/          # ShellItemRef, TabState — pure data contracts
├── Shell/           # ShellInteropService, ContextMenuService, ShellFileOperationService,
│                    #   ShellClipboardService, ShellOverflowMenuService
├── MainWindow.xaml  # Two-pane TabView layout
└── App.xaml         # WinUI 3 application entry + MddBootstrap init
```
