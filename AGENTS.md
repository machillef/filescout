# FileScout — Agent Instructions

## Stack

- Language: C++20
- UI: WinUI 3 (Windows App SDK 1.8, C++/WinRT)
- Build system: MSBuild v143 (Visual Studio 2022 Build Tools)
- Test framework: GoogleTest (NuGet `Microsoft.googletest.v140.windesktop.msvcstl.dyn.rt-dyn.1.8.1.7`)
- Target: Windows 11 x64, unpackaged desktop app

## Build Commands

**Full build (restore NuGet + compile):**
```
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"
```

**Run tests:**
```
native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no
```

**Run eval grader (build + tests + warnings):**
```
bash .claude/evals/scripts/grade.sh
```

**Tests only (skip build):**
```
bash .claude/evals/scripts/grade.sh --tests-only
```

## Project Structure

```
filescout/
├── native/
│   ├── FileScout.Native/        # Main WinUI 3 app
│   │   ├── Shell/               # COM/WinRT wrappers (IShellItem, IFileOperation, IContextMenu)
│   │   ├── Domain/              # Pure C++ structs — NO WinRT/COM includes
│   │   └── MainWindow.xaml.*    # UI host; all pane ops go through ActivePane()
│   └── FileScout.Tests/         # GoogleTest integration tests (shared-source approach)
│       └── tests/               # Test sources
├── docs/ai/                     # Initiative plans, slices, status
└── .claude/                     # Harness (commands, evals, hooks)
```

## Architecture Constraints (Critical)

### 1. Active-pane routing
ALL pane operations in `MainWindow` MUST go through `ActivePane()`. Never access
`m_panes[0]` or `m_panes[1]` directly. The active pane is `std::array<PaneContext, 2>`
indexed by a tab selection.

### 2. Shell/Domain boundary
- `Shell/` classes: use `winrt::com_ptr<IShellItem>`, `winrt::com_ptr<IFileOperation>`, etc.
- `Domain/` structs (`ShellItemRef`, `TabState`, `PaneContext`): pure C++ — zero `#include <winrt/*>` or COM headers allowed.

### 3. Build invocation
ALWAYS use `cmd /c "VsDevCmd.bat ... && MSBuild ..."`.
Never use `pwsh -c MSBuild` — the WinRT code generation step requires the full VS environment
that `VsDevCmd.bat` sets up.

### 4. COM error handling
Every COM call MUST go through `winrt::check_hresult()`. Never ignore an HRESULT.
Use `winrt::com_ptr<T>` — never raw `T*` for COM objects.

### 5. Test project linker requirements
When a new production `.cpp` that uses `winrt::com_ptr` is added to `FileScout.Tests`,
the vcxproj `AdditionalDependencies` must include:
- `runtimeobject.lib` — provides `RoOriginateLanguageException` resolved via `/alternatename` in `winrt/base.h`
- `$(NugetPackageDirectory)\Microsoft.Windows.CppWinRT.2.0.250303.1\build\native\lib\x64\cppwinrt_fast_forwarder.lib`

The include path must contain `$(ProjectDir)..\FileScout.Native\Generated Files` for `winrt/base.h`.

## Eval Gates

A change passes when ALL hold:
1. `MSBuild FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64` exits 0
2. `FileScout.Tests.exe` reports `[  PASSED  ]` with 0 failures
3. No new MSBuild warnings beyond the two exempt WinAppSDK patterns:
   - `MSB8027.*WindowsAppRuntimeAutoInitializer`
   - `LNK4042.*WindowsAppRuntimeAutoInitializer`

See `.claude/evals/filescout-core.md` for full eval spec and pass@k targets.
