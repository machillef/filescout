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
- **Out of scope:** content search, indexing, cross-pane search, network shares (local drives only), search history, saved queries

---

## Architecture

### New units

**`FuzzyMatcher`** (`native/FileScout.Native/Search/FuzzyMatcher.h`)
Pure C++ header, no WinRT dependency. Single function:

```cpp
// Returns score >= 0 if query matches filename, -1 if no match.
// Query length < 2 always returns -1.
// *, ?, [ treated as literals — no preprocessing.
int FuzzyScore(std::wstring_view query, std::wstring_view filename);
```

Scoring components (higher = better):
- **Prefix bonus** (+20): first query char at index 0 of filename
- **Consecutive bonus** (+15 per run): matched chars adjacent in filename
- **Word-boundary bonus** (+8): match starts after `.`, `_`, `-`, space, or lowercase→uppercase transition
- **Match ratio penalty**: proportional deduction when query very short relative to filename length

Stack-only — no heap allocation per call.

---

**`SearchService`** (`native/FileScout.Native/Search/SearchService.h/.cpp`)

```cpp
using PostFn = std::function<void(std::function<void()>)>;
// PostFn: takes a void() callback and schedules it on the UI thread.
// Production: caller constructs from DispatcherQueue at call site (see MainWindow.xaml.cpp).
// Tests: caller provides a synchronous direct-call lambda — dispatcher is never needed.
// SearchService does not accept a DispatcherQueue parameter; PostFn is the only
// dispatch mechanism. This removes the dual-path ambiguity entirely.

struct SearchResult {
    std::wstring full_path;
    std::wstring filename;
    int          score;
};

class SearchService {
public:
    using ResultCallback   = std::function<void(std::vector<SearchResult>)>;
    using CompleteCallback = std::function<void(bool cap_reached)>;

    // Signals old walk (set *m_cancelled_flag=true, bump m_generation),
    // detaches old thread (safe: thread owns shared_ptr to its cancelled flag),
    // allocates new m_cancelled_flag, resets m_cap_reached to false, spawns new thread.
    // Non-blocking. Safe to call from the UI thread.
    void Start(std::wstring root, std::wstring query,
               ResultCallback on_results, CompleteCallback on_complete,
               PostFn post_fn);

    // Sets *m_cancelled_flag=true, bumps m_generation. Non-blocking.
    void Cancel();

    // Sets flag, joins m_thread if joinable (runs at MainWindow teardown).
    ~SearchService();

private:
    std::shared_ptr<std::atomic<bool>> m_cancelled_flag;
    std::atomic<uint64_t>              m_generation{ 0 };
    bool                               m_cap_reached{ false };
    std::thread                        m_thread;
};
```

**Thread lifetime contract:**
1. `Start`: signal old (set flag, bump gen), detach if joinable, allocate new `m_cancelled_flag`, reset `m_cap_reached = false`, spawn fresh `m_thread`.
2. `Cancel`: set flag, bump gen. Returns immediately.
3. `~SearchService`: set flag, join `m_thread` if joinable.
4. Each `PostFn` lambda captures the generation value at post time; silently no-ops on the UI thread if generation no longer matches.

**Walk:**
- `WalkDir(path, uint32_t current_depth)` uses `directory_iterator` (not `recursive_directory_iterator`). Depth is always known. Descent skipped at `current_depth >= k_search_max_depth` (= 12).
  - *Stack depth note:* 12 frames × one `directory_iterator` handle per frame is well within the default Windows stack limit.
- `directory_iterator` with `directory_options::skip_permission_denied`. Access-denied entries are silently skipped by the iterator — no `try`/`catch` per entry needed. The `skip_permission_denied` flag is the sufficient guard; no additional exception wrapper is required.
- Symlinks not followed (default iterator behaviour).
- Top-200 min-heap `{score, path}`. Displacement only on strictly higher score. `m_cap_reached = true` on first displacement.
  - *Tie edge case:* 201+ entries at equal top-score → extras silently dropped; `m_cap_reached` stays `false`. Documented and tested.
- Result batches pushed every 50 ms via `PostFn`. Each push replaces ListView wholesale.
- Cancellation checked: (a) before each entry, (b) before each subdirectory descent.

---

**Address bar state machine** (`PaneContext` in `MainWindow.xaml.h`)

```
None ──Ctrl+F──► Filter ──Enter──► Recursive
                   │                   │
                Escape              Escape
                   │                   │
                   └──────► None ◄──────┘

Ctrl+F while in Filter: ignored (already in filter).
Ctrl+F while in Recursive: ignored.
```

**Ctrl+F wiring:** A `KeyboardAccelerator` (Ctrl+F) is added directly to `MainWindow` — it is **not** routed through `SetupKeyboardAccelerators` / `ExecuteContextCommand`. The `Invoked` handler checks `ActivePane().search_mode`:
- `None` → transition to Filter, clear TextBox, save snapshot
- `Filter` or `Recursive` → `args.Handled(true)`, do nothing

`AddressTextBox_KeyDown` handles mode-specific Enter and Escape:
- In `Filter` mode, `Enter` → set `args.Handled(true)` and transition to Recursive (prevents normal path-navigation from firing)
- In `Filter` or `Recursive` mode, `Escape` → set `args.Handled(true)` and exit search → None

- `SearchService` is **per-pane** (field in `PaneContext`). Two panes search independently.
- Tab switch while in Filter/Recursive: exit search → None before switch.
- **Pane-focus switch** (`SetActivePaneIndex`): if the outgoing pane is in Filter or Recursive mode, exit its search → None before activating the new pane. The shared `AddressTextBox` is then updated to the incoming pane's current path. Without this, the address bar retains the outgoing pane's search text while the incoming pane is active.
- Navigation events (sidebar, back/forward, path Enter in Normal mode) cancel search → None.

---

## UX Behaviour

### SearchFilter mode

- Ctrl+F: clear TextBox, placeholder `Search in <folder_name>…`
- **Snapshot on entry:** `PaneContext::search_snapshot` = `std::vector<ShellItemRef>` copied by value from `pane.tabs[active_tab_index].items`. This snapshot is the authoritative item list for the duration of the Filter session — both `TextChanged` scoring and Escape restoration use `search_snapshot`, not live `pane.items`.
  - *Staged-load guard:* Ctrl+F is **disabled** (the `Invoked` handler no-ops) when `pane.awaiting_final_batch == true` (i.e., the background second-batch dispatch is still pending). This prevents taking a snapshot of an incomplete item list. The address bar Ctrl+F becomes active again once `awaiting_final_batch` clears (at the next `TryEnqueue` callback when the final batch lands).
- `TextChanged`:
  - Query length < 2: rebuild ListView from `search_snapshot` unfiltered
  - Else: score all entries in `search_snapshot` via `FuzzyScore`; sort descending; take top 200; rebuild ListView
- Matched chars bold (inline `Run` in `TextBlock`)
- Escape: restore path text; rebuild ListView from `search_snapshot`; mode → None

### SearchRecursive mode

- Address bar: `Search: <query>` (read-only — `TextBox.IsReadOnly = true`)
- **Per-pane** indeterminate `ProgressBar` placed inside each pane's XAML column, between the address bar row and the TabView. Visibility toggled independently per pane. A search in pane 0 shows only pane 0's ProgressBar.
- `SearchService::Start` called with active pane's root path and a `PostFn` constructed at the call site:
  ```cpp
  auto post = [dq = m_dispatcherQueue](auto fn) { dq.TryEnqueue(std::move(fn)); };
  pane.search_service.Start(root, query, on_results, on_complete, post);
  ```
- Each result batch replaces ListView; rows: filename (bold-highlighted) + relative path from root as subtitle
- Status during walk: `<N> matches`
- On `on_complete`:
  - `cap_reached == false`: `<N> matches in <root>`
  - `cap_reached == true`: `Showing top 200 matches — refine your query to narrow results`
- Escape: `Cancel()` (non-blocking), hide ProgressBar, clear ListView, mode → None

### Result row

Existing list-mode row view. Search mode: bold highlights, relative-path subtitle, existing icon.

### Selecting a result

1. `SearchService::Cancel()` (non-blocking)
2. `LoadDirectory(parent_path, selectAfter)` — calls the two-argument public wrapper (which captures `m_activePaneIndex` synchronously on the UI thread), consistent with all other navigate-then-select flows in the codebase. This is **not** a direct call to `LoadDirectoryAsync` — using the wrapper avoids passing the wrong pane index. The `selectAfter` optional defers selection until the load completes.
3. Mode → None

---

## Safety Guards

| Guard | Mechanism |
|---|---|
| Result cap (hard) | Top-200 min-heap; `m_cap_reached` on first displacement |
| Correct cap message | `on_complete(cap_reached)` — true only on actual displacement |
| Tie truncation | Silent drop; `m_cap_reached` stays false; documented + tested |
| Minimum query length | `FuzzyScore` returns –1 for length < 2 |
| No wildcard expansion | No preprocessing; `*`, `?`, `[` are literals in sequence scan |
| Depth limit | `WalkDir` helper; skip descent at depth >= 12 |
| Permission errors | `directory_options::skip_permission_denied` on iterator |
| Symlink cycles | Symlinks not followed |
| Thread memory safety | Shared_ptr for cancellation flag; valid after detach |
| Non-blocking cancel | `Cancel()` signals only; UI thread never joins |
| Stale callbacks | PostFn lambdas capture generation; no-op on mismatch |
| Navigation/tab cancel | Check `search_mode != None`, call `Cancel()` |
| Pane-focus switch | `SetActivePaneIndex` exits outgoing pane's search → None before switch |
| Enter disambiguation | `AddressTextBox_KeyDown` sets `args.Handled(true)` in Filter mode |
| Ctrl+F during load | Disabled when `awaiting_final_batch == true`; snapshot deferred until full list available |
| Per-pane isolation | `SearchService` per `PaneContext` |
| Per-pane ProgressBar | Separate `ProgressBar` in each pane column; toggled independently |

**Implementation note — Ctrl+F and TextBox focus:** In some WinUI 3 SDK versions, `TextBox` consumes `Ctrl+F` before root-level `KeyboardAccelerator` handlers fire. If the accelerator fails to trigger, handle it via `ProcessKeyboardAccelerators` override on the TextBox instead of relying solely on the `Invoked` event. Test early in R17c.

---

## Files Touched

| File | Change |
|---|---|
| `Search/FuzzyMatcher.h` | New — pure C++ scorer |
| `Search/SearchService.h` | New — interface |
| `Search/SearchService.cpp` | New — walk implementation |
| `MainWindow.xaml.h` | Add to `PaneContext`: `SearchMode search_mode`, `std::wstring search_query`, `std::vector<ShellItemRef> search_snapshot`, `SearchService search_service` |
| `MainWindow.xaml` | Add per-pane `ProgressBar` in each pane column (above TabView); add Ctrl+F `KeyboardAccelerator` to Window |
| `MainWindow.xaml.cpp` | Ctrl+F `Invoked` handler, state machine transitions, `TextChanged` filter against `search_snapshot`, `AddressTextBox_KeyDown` Enter/Escape guards, result row building, nav/tab cancel guards, `LoadDirectoryAsync(parent, selectAfter)` for result selection |
| `FileScout.Native.vcxproj` | Add `Search\FuzzyMatcher.h`, `Search\SearchService.h`, `Search\SearchService.cpp` |
| `FuzzyMatcherTests.cpp` | New |
| `SearchServiceTests.cpp` | New (synchronous `PostFn`) |

---

## Testing

### FuzzyMatcher unit tests

- Prefix match: `doc` scores higher vs `document.txt` than `readme.doc`
- Consecutive bonus: `abc` scores higher in `abcdef` than `axbxcx`
- No match: –1 when a query char not found in sequence
- Short query: length-0 and length-1 → –1
- Wildcard positive: query `a*b` vs synthetic filename `a*b_file.txt` (contains literal `*`) → score ≥ 0
- Wildcard negative: query `a*b` vs `ab_file.txt` (no `*`) → –1 (char `*` not found; no expansion applied)
- Empty query → –1

### SearchService unit tests (synchronous PostFn injection)

- Cancel before first result: zero `ResultCallback` calls
- Cancel mid-walk (deterministic): use a `std::latch(1)` in the `PostFn`; decrement it on first batch; test thread waits on the latch, then calls `Cancel()`. Assert no `ResultCallback` invocations occur after the `Cancel()` call.
- Cap enforcement: 500 synthetic entries all scored above threshold → ≤ 200 delivered; `cap_reached == true`
- Exact 200, no cap: 200 entries, no displacement → `cap_reached == false`
- Tie truncation: 201 entries all with identical score → `cap_reached == false` (silent drop documented)
- Depth limit: 15-level synthetic tree → no entries at depth > 12
- Memory safety: Start → Cancel → Start sequence completes without crash (validates shared_ptr + detach contract)
- Permission skip: covered by `skip_permission_denied` on the iterator; no per-unit test needed. Manual validation against `C:\Windows\System32` (contains restricted subdirs) is sufficient.

### Integration smoke test

- Filter `ntdll` against loaded `C:\Windows\System32` items → ≥1 result in < 10 ms
- SearchRecursive cancel: `Start` + immediate `Cancel()` → destructor joins within 500 ms

### Acceptance criteria

- All 57 existing tests pass; all new unit tests pass; zero new build warnings
- Manual: Ctrl+F activates filter (both panes independently); Enter escalates; Escape exits each state cleanly; double-click result navigates and selects via `selectAfter`; cap message fires only on displaced results; tab switch exits search; Ctrl+F during Recursive/Filter is a no-op; ProgressBar appears in correct pane only

---

## Implementation Slices

| Slice | Description |
|---|---|
| R17a | `FuzzyMatcher` header + unit tests (pure C++, no UI) |
| R17b | `SearchService` background walk + cap/cancel/depth/tie/memory tests (synchronous PostFn) |
| R17c | `SearchFilter` mode — Ctrl+F accelerator, state machine, `TextChanged` against `search_snapshot`, Escape restore |
| R17d | `SearchRecursive` mode — Enter escalation, streaming results, per-pane ProgressBar, `cap_reached` status bar |
| R17e | Result row polish — bold match highlights, relative-path subtitle, navigate-on-select via `selectAfter` |
