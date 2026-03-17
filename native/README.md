# FileScout Native Rewrite

This folder hosts the parallel Windows-native rewrite of FileScout.

## Direction

- Host UI: C++/WinRT + WinUI 3
- Target OS: Windows 11 x64
- Packaging model: unpackaged desktop app
- Goal: Explorer-parity shell behavior first, FileScout differentiation second

## Current Slice

Slice 1 establishes:

- a same-repo native workspace
- an unpackaged WinUI 3 desktop shell
- the initial domain and shell contracts for parity work
- a placeholder Explorer surface backed by real filesystem enumeration

It does **not** yet implement:

- shell-backed context menu hosting
- `IFileOperation` copy/cut/paste/delete flows
- native icon extraction
- drag/drop parity
- tabs or dual-pane behavior

## Prerequisites

Install Visual Studio 2022 Build Tools or Visual Studio 2022 with:

- Desktop development with C++
- Windows 11 SDK
- C++ WinUI / Windows App SDK support

## Restore

```powershell
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true"
```

## Build

```powershell
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"
```

## Notes

- The project pins current stable NuGet package versions for `Microsoft.WindowsAppSDK`, `Microsoft.Windows.CppWinRT`, and `Microsoft.Windows.SDK.BuildTools`.
- The shell layer is intentionally thin in Slice 1: directory enumeration is real, but Explorer-class shell verbs are not yet wired.
