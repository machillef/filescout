# FileScout

## Stack

- Language: Rust (edition 2024)
- GUI: iced 0.14 (Elm-inspired, GPU-rendered via wgpu)
- Async: tokio
- Target: Windows 10+ x86-64

## Build & Test

```powershell
cargo build
cargo run
cargo test
cargo clippy -- -D warnings
cargo fmt -- --check
```

## Structure

```
filescout/
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
