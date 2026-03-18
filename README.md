# FileScout

FileScout is a Windows file explorer project currently in transition.

## Current State

- `crates/` contains the original Rust + `iced` prototype
- `native/` contains the active Windows 11 native rewrite in C++/WinRT + WinUI 3

The Rust prototype proved that FileScout can be fast. It did **not** meet the
product bar for Explorer-class Windows parity, especially around shell
integration and context-menu fidelity. The native rewrite is now the primary
implementation track.

## Native Rewrite Goals

- Windows 11 native windowing and focus behavior
- Explorer-parity shell semantics for filesystem-backed views
- Win11-style context menu with shell-backed command overflow
- Native clipboard, drag/drop, icons, and properties integration
- Tabs, dual-pane workflows, and FileScout-specific power features after parity

## Repo Layout

- [native/README.md](/Users/machi/source/repos/filescout/native/README.md): native rewrite overview and build commands
- `crates/`: legacy Rust prototype
- `docs/ai/`: initiative history, learnings, and pivot notes

## Legacy Prototype Commands

```powershell
cargo build
cargo run
cargo test
cargo clippy -- -D warnings
cargo fmt -- --check
```

## Native Rewrite Commands

```powershell
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true"
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"
```

## License

MIT
