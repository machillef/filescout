# Fuzzy Search Design Spec

**Feature:** R17 — Fuzzy File Search
**Date:** 2026-03-21
**Status:** Approved for implementation

---

## Overview

Replace Windows Search (stale index, often disabled) with a fast, fd-find-style fuzzy search integrated directly into the FileScout address bar. Typing filters the current pane instantly; pressing Enter escalates to a background recursive walk of the current folder tree.

---

## Scope

- **In scope:** in-pane filter (current directory), recursive tree search (current root), filename-only matching, fzf-style scored ranking, result cap, cancellation, address bar UX
- **Out of scope:** content search, indexing, cross-pane search, network shares, search history, saved queries

---

## Architecture

### New units

**`FuzzyMatcher`** (`native/FileScout.Native/Search/FuzzyMatcher.h`)
Pure C++ header, no WinRT dependency. Single function:

```cpp
// Returns a score >= 0 if query matches filename, -1 if no match.
// Query length < 2 always returns -1.
// Wildcard characters (*, ?, [) are treated as literals.
int FuzzyScore(std::wstring_view query, std::wstring_view filename);
```

Scoring components (higher = better match):
- **Prefix bonus** (+20): query characters begin at index 0 of filename
- **Consecutive bonus** (+15 per run): matched characters are adjacent
- **Word-boundary bonus** (+8): match starts after `.`, `_`, `-`, space, or a lowercase→uppercase transition
- **Match ratio penalty**: subtract proportionally when query is very short relative to filename length

All computation is stack-only — no heap allocation per call.

**`SearchService`** (`native/FileScout.Native/Search/SearchService.h/.cpp`)
Owns exactly one `std::thread` and one `std::atomic<bool> m_cancelled`. Public interface:

```cpp
struct SearchResult {
    std::wstring full_path;   // absolute path
    std::wstring filename;    // display name (last component)
    int          score;
};

class SearchService {
public:
    // Callback is invoked on the UI thread via DispatcherQueue::TryEnqueue.
    // results is a snapshot of the current top-N heap, sorted score-descending.
    using ResultCallback = std::function<void(std::vector<SearchResult>)>;

    void Start(winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher,
               std::wstring root, std::wstring query,
               ResultCallback on_results, std::function<void()> on_complete);
    void Cancel(); // sets m_cancelled, joins thread — never leaves a dangling thread
};
```

Walk behaviour:
- `std::filesystem::recursive_directory_iterator` with `skip_permission_denied`
- `follow_directory_symlink = false` — no symlink cycle risk
- Max depth: 12 levels (constant `k_search_max_depth = 12`)
- Maintains a **top-200 min-heap** of `{score, path}`. A new candidate displaces the heap minimum only when its score is higher. Heap never exceeds 200 entries — memory is hard-bounded regardless of tree size.
- Result batches pushed every 50 ms via `TryEnqueue`. Each push replaces the ListView wholesale (heap re-ranks as walk progresses).
- `m_cancelled` is checked after each directory entry and after each subdirectory descent.

**Address bar state machine** (in `MainWindow`)
Three states stored as `enum class SearchMode { None, Filter, Recursive }` in `PaneContext`:

```
None ──Ctrl+F──► Filter ──Enter──► Recursive
                   │                   │
                Escape              Escape
                   │                   │
                   └──────► None ◄──────┘
```

Any navigation event (sidebar click, path typed + Enter, back/forward) while in Filter or Recursive state cancels search and transitions to None.

---

## UX Behaviour

### SearchFilter mode

- Ctrl+F clears the address bar TextBox; placeholder text: `Search in <folder_name>…`
- On each `TextChanged` (debounce: 0 ms — scoring is synchronous and instant):
  - If query length < 2: show all `pane.items` unfiltered
  - Else: score all `pane.items` via `FuzzyScore`; sort descending; take top 200; rebuild ListView
- Matched characters in each result row's filename are **bold** (inline `Run` elements in a `TextBlock`)
- Escape: restore original path text and reload `pane.items` snapshot (no re-enumeration needed — snapshot was stored on mode entry)

### SearchRecursive mode

- Address bar shows `Search: <query>` (read-only)
- An indeterminate `ProgressBar` appears below the toolbar for the duration of the walk
- `SearchService::Start` is called with the active pane's current root path
- Each result batch replaces the ListView content; rows show filename (bold-highlighted) + relative path from root as subtitle
- Status bar text: `<N> matches` while walking; on completion:
  - If N < 200: `<N> matches in <root>`
  - If N == 200: `Showing top 200 matches — refine your query to narrow results`
- Escape: calls `SearchService::Cancel()`, `ProgressBar` hidden, ListView cleared, mode → None

### Result row in search mode

Reuses the existing list-mode row view. In search mode:
- Primary text: filename with matched characters bold
- Secondary text (subtitle): relative path from search root (e.g. `src\components\`)
- Icon: existing shell icon

### Selecting a result

- Double-click or Enter on a result: navigates to the file's parent folder and selects the file (same as current `NavigateTo` + `SelectItemByPath` flow)
- This clears search mode and returns to Normal

---

## Safety Guards

| Guard | Mechanism |
|---|---|
| Result cap | Top-200 min-heap in `SearchService`; heap never grows beyond 200 |
| Minimum query length | `FuzzyScore` returns –1 for `query.size() < 2`; no walk started |
| No wildcard expansion | `*`, `?`, `[` treated as literal characters in scorer |
| Depth limit | Walk aborts descent beyond `k_search_max_depth = 12` |
| Permission errors | `skip_permission_denied` flag on iterator |
| Symlink cycles | `follow_directory_symlink = false` (iterator default) |
| Dangling thread | `Cancel()` always calls `join()` before returning |
| Stale callback | `SearchService` captures a generation counter; callbacks dropped if generation mismatches active search |
| Navigation cancels | Any `NavigateTo` call checks `pane.search_mode != None` and calls `SearchService::Cancel()` first |

---

## Files Touched

| File | Change |
|---|---|
| `native/FileScout.Native/Search/FuzzyMatcher.h` | New — pure C++ scorer |
| `native/FileScout.Native/Search/SearchService.h` | New — service interface |
| `native/FileScout.Native/Search/SearchService.cpp` | New — walk implementation |
| `native/FileScout.Native/Domain/PaneContext.h` | Add `SearchMode search_mode`, `std::wstring search_query`, snapshot of `items` on entry |
| `native/FileScout.Native/MainWindow.xaml` | Add `ProgressBar` element below toolbar; address bar TextBox already present |
| `native/FileScout.Native/MainWindow.xaml.h` | Add `SearchService m_search_service` member; Ctrl+F handler |
| `native/FileScout.Native/MainWindow.xaml.cpp` | State machine transitions, `TextChanged` filter, result row building, `NavigateTo` guard |
| `native/FileScout.Native/FileScout.Native.vcxproj` | Add new .cpp/.h files to build |
| `native/FileScout.Tests/FuzzyMatcherTests.cpp` | New — unit tests for scorer |
| `native/FileScout.Tests/SearchServiceTests.cpp` | New — unit tests for service |

---

## Testing

### `FuzzyMatcher` unit tests

- Exact prefix: `doc` scores higher against `document.txt` than `readme.doc`
- Consecutive bonus: `abc` scores higher in `abcdef` than `axbxcx`
- No match: returns –1 when a query character is not found in sequence
- Short query guard: length-1 query returns –1
- Wildcard literal: `*.txt` returns –1 for `report.txt` (no `*` in filename)
- Empty query: returns –1

### `SearchService` unit tests

- Cancel before first result: zero calls to callback
- Cancel mid-walk (after 100 entries): no callback invocations after cancellation
- Cap enforcement: 500 synthetic entries → callback receives ≤ 200
- Depth limit: 15-level synthetic tree → entries at depth > 12 absent from results
- Permission skip: iterator that throws on one entry — walk continues, no crash

### Integration smoke test

Against `C:\Windows\System32` (~4500 files, always present):
- SearchFilter: query `ntdll` returns ≥1 result; completes in < 10 ms wall time
- SearchRecursive cancel: start walk, cancel after 100 ms — thread joins in < 500 ms (no hang)

### Acceptance criteria

- All 57 existing tests pass
- All new unit tests pass
- Build: zero new warnings (MSB8027/LNK4042 pre-existing exceptions unchanged)
- Manual: Ctrl+F activates filter; Enter escalates to recursive; Escape exits cleanly; double-click result navigates to file

---

## Implementation Slices

| Slice | Description |
|---|---|
| R17a | `FuzzyMatcher` header + unit tests (pure C++, no UI) |
| R17b | `SearchService` background walk + cap/cancel/depth tests |
| R17c | `SearchFilter` mode — address bar state machine + in-pane live filter |
| R17d | `SearchRecursive` mode — escalation on Enter, streaming results, ProgressBar, status bar cap message |
| R17e | Result row polish — bold match highlights, relative-path subtitle, navigate-on-select |
