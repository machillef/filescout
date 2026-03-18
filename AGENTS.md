# FileScout

## Stack

- Legacy prototype: Rust (edition 2024) + iced 0.14 + tokio
- Active rewrite: C++20 + C++/WinRT + WinUI 3
- Legacy target: Windows 10+ x86-64
- Active rewrite target: Windows 11 x64

## Build & Test

```powershell
cargo build
cargo run
cargo test
cargo clippy -- -D warnings
cargo fmt -- --check

cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true"
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"
```

## Structure

```
filescout/
├── native/               # Parallel Windows-native rewrite (C++/WinRT + WinUI 3)
├── crates/
│   ├── filescout-app/    # Entry point, iced app shell
│   ├── filescout-core/   # File system ops, models, business logic
│   ├── filescout-ui/     # Reusable iced widgets
│   └── filescout-ai/     # AI features (trait-based, swappable backends)
├── assets/               # Icons, themes
└── tests/                # Integration tests
```

## Architecture

- iced Elm pattern: State (model) -> Message -> Update -> View
- Async-first I/O via tokio (never block UI thread)
- Command pattern for undoable file operations
- AI features behind `AiProvider` trait for backend swapping
- Native rewrite uses a WinUI 3 shell layer, a Windows shell integration layer,
  and app-owned domain contracts for Explorer-parity behavior
