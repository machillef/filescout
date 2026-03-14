# FileScout

A high-performance, open-source file explorer for Windows built with Rust and iced.

## Features (Planned)

- Dual-pane file management with tabs
- Command palette with fuzzy search
- Batch rename with pattern support
- File preview (images, text, PDF)
- AI-powered features (natural language search, smart organization)
- GPU-rendered UI via wgpu
- Dark and light themes

## Tech Stack

- **Language:** Rust
- **GUI:** iced (Elm-inspired, GPU-rendered)
- **Async:** tokio
- **Target:** Windows 10+ (x86-64)

## Building

```powershell
cargo build
cargo run
```

## Quality Gates

```powershell
cargo clippy -- -D warnings
cargo test
cargo fmt -- --check
```

## License

MIT
