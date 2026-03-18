# FileScout

## Stack

- Language: C++20
- UI: WinUI 3 (Windows App SDK 1.8, C++/WinRT)
- Target: Windows 11 x64 (unpackaged desktop app)

## Build & Test

```powershell
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true"
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"
```

## Structure

```
filescout/
├── native/               # Active implementation (C++/WinRT + WinUI 3)
│   └── FileScout.Native/ # WinUI 3 shell, domain, shell services
├── assets/               # Icons, themes
└── docs/ai/              # Initiative history and slice docs
```

## Architecture

- WinUI 3 host: unpackaged desktop app bootstrapped via `MddBootstrapInitialize2`
- Shell layer: C++/WinRT wrappers around `IShellItem`, `IFileOperation`, `IContextMenu`
- Domain: pure C++ structs (`ShellItemRef`, `TabState`, `PaneContext`) — no WinRT dependencies
- Per-pane state: `std::array<PaneContext, 2>` in `MainWindow`; all operations route through `ActivePane()`
