# FileScout Requirements

## Vision

Open-source, high-performance Windows file explorer alternative inspired by File Pilot.
GPU-rendered, AI-enhanced, built with Rust + iced.

## Core Requirements

### File Management
- Browse directories with sortable columns (name, size, date, type)
- Copy, move, delete (to Recycle Bin), rename files and folders
- Undo/redo for all file operations
- Drag-and-drop support
- Clipboard operations (cut, copy, paste)

### Navigation
- Address bar with path editing and breadcrumbs
- Back/forward history navigation
- Drive/volume selector
- GoTo dialog with filesystem autocompletion
- Recent folders list

### Dual Pane & Tabs
- Split view with two independent panes
- Multiple tabs per pane
- Drag tabs between panes

### Command Palette
- Global command search (Ctrl+Shift+P)
- Fuzzy matching with scoring
- Hotkey assignment and aliases
- Numpad shortcuts

### Search
- Fuzzy file name search in current directory
- Recursive search across subdirectories
- File extension filtering
- Real-time results as you type

### File Preview
- Preview panel for images, text files, PDFs
- File inspector showing metadata

### Batch Rename
- Pattern-based multi-file rename
- Date, sequence, unique ID options
- Preview before applying

### Theming
- Dark and light themes
- Customizable font size, spacing
- Color scheme editor

### AI Features (v2)
- Natural language file search
- Smart organization suggestions
- AI-assisted batch rename patterns

## Non-Functional Requirements

- Sub-100ms startup time
- Instant directory listing (<50ms for 10K files)
- <50MB memory for typical usage
- <5MB binary size (release, stripped)
- Windows 10+ x86-64 support
