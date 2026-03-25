# FileScout

## Stack

- Language: C++20
- UI: WinUI 3 (Windows App SDK 1.8, C++/WinRT)
- Target: Windows 11 x64 (unpackaged desktop app)

## Build & Test

**IMPORTANT:** Always use the PowerShell wrapper below. Never use `cmd /c "..."` directly from bash
with MSBuild flags — bash interprets `\r` in `/restore` as an octal escape, producing the error
`'estore' is not recognized`. Bash heredocs create LF-only .bat files that cmd.exe misparsess.
The PowerShell single-quote approach avoids all of these escaping issues.

```bash
# Full build (NuGet restore + Debug x64 compile)
powershell -Command "cmd /c 'call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal'"

# Run tests
native/bin/x64/Debug/Tests/FileScout.Tests.exe
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
