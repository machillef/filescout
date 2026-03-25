# Design: large-dir-perf

> **Initiative:** large-dir-perf
> **Status:** Approved
> **Last updated:** 2026-03-25

---

## Part 1 — Requirements

### Problem Statement

Navigating to a large directory (System32, ~5120 items) causes a visible UI freeze because three
operations run sequentially on the UI thread in a single large dispatch:

1. `SortItems` — called inside the `TryEnqueue` lambda (5120 items, `_wcsicmp` comparisons)
2. `rows.Append(MakeFileRowViewModel(item))` — called 4970 times in one dispatch, firing 4970
   `VectorChanged::ItemInserted` events the ListView must bookkeep individually
3. `BackfillIconsAsync` — creates a `WriteableBitmap` for every row (5120 times) even though
   System32 has ≤30 unique extensions; ~5090 of those pixel-buffer allocations are identical

Current total time: ~2000 ms (from phase3-perf-ux retro log).
Target: < 400 ms on a warm navigate to System32.

### User Stories

1. As a user navigating to System32, I want all rows to appear without a UI freeze, so the app
   feels responsive
2. As a user, I want the first 150 rows to appear within ~150 ms of navigating, so I get
   immediate visual feedback
3. As a user, I want all rows fully populated within ~400 ms, so I can start working without
   waiting
4. As a user navigating Back/Forward/Refresh, I want the same performance as a fresh navigate
5. As a user making a second navigate to a directory with similar file types, I want icons to
   appear faster because the extension ImageSource cache is already warm
6. As a developer, I want an automated test that catches large-dir enumeration regressions before
   they ship

### Out of Scope

- Archive enumeration (`EnumerateArchiveDirectory`) — separate code path
- Search pipeline (`SearchService`, Filter mode) — not part of `LoadDirectoryAsync`
- Sidebar, favorites, clipboard, context menu, any UX changes
- Streaming enumeration (publish rows as `FindNextFileW` finds them) — requires abandoning sort
- Virtual/paginated scroll beyond what `ListView` already provides natively

---

## Part 2 — Design

### Architecture Overview

Four targeted changes to the existing `LoadDirectoryAsync` / `BackfillIconsAsync` pipeline.
No new files, no new classes. All changes confined to `MainWindow.xaml.cpp` and `MainWindow.xaml.h`.

```
Current pipeline (UI-thread hot path):
  BG: enumerate
  UI: sort + 150 Append
  UI: 4970 Append       ← one large dispatch, blocks UI thread
  UI: 5120 WriteableBitmap + IconSource()  ← one large dispatch, blocks UI thread

New pipeline:
  BG: enumerate → sort → build all N FileRowViewModels
  UI: Append first 150  ← fast first-paint (unchanged)
  UI: ReplaceAll(all N) ← single VectorChanged::Reset, O(1) UI work
  UI (chunked): IconSource() via session ImageSource cache ← ≤30 WriteableBitmap allocs total
```

---

### Change 1 — Sort + ViewModel pre-build on background thread

**Location:** `MainWindow::LoadDirectoryAsync` in `MainWindow.xaml.cpp`

Move `SortItems` and all `MakeFileRowViewModel` calls from inside the `TryEnqueue` dispatch
lambda to the background-thread section (after `co_await resume_background()`), before any
UI-thread dispatch.

**Thread safety:** `MakeFileRowViewModel` sets only `hstring` fields and a `bool` — no WinUI
types are constructed. `hstring` is an agile WinRT type; `winrt::make_self<FileRowViewModel>()`
allocates a COM object in-process without STA constraints at construction time. The resulting
`FileRowViewModel` objects are handed to the UI thread for binding and are not accessed from the
background thread after handoff.

**Coroutine restructure:** Replace the current nested `TryEnqueue`-inside-`TryEnqueue` pattern
with `co_await winrt::resume_foreground(m_dispatcherQueue)` calls directly in the coroutine
body. This makes the threading model explicit and removes one level of lambda nesting.

```
co_await winrt::resume_background()
    enumerate all items (FindFirstFileW / FindNextFileW)     ← unchanged
    SortItems(items, sort_col, sort_dir)                      ← MOVED from UI thread
    build std::vector<FileRowViewModel> prebuilt(items.size())
    for each item: prebuilt[i] = MakeFileRowViewModel(item)  ← MOVED from UI thread

co_await winrt::resume_foreground(m_dispatcherQueue)
    [stale-generation guard]
    update pane state (current_path, items, tabs, breadcrumbs, status bar)
    rows.Clear() + Append first 150 prebuilt rows            ← fast first-paint
    timing.first_batch_publish_tick = GetTickCount64()
    pane.user_interacted_after_first_batch = false

    if totalItems <= 150:
        // done — backfill icons
    else:
        // schedule final batch

co_await winrt::resume_foreground(m_dispatcherQueue)  ← second dispatch
    [stale-generation guard + awaiting_final_batch guard]
    rows.as<IReplaceAll<FileRowViewModel>>().ReplaceAll(prebuilt)  ← single Reset event
    pane.awaiting_final_batch = false
    timing.final_publish_tick = GetTickCount64()
    BackfillIconsAsync(...)
```

**Flags preserved (semantics unchanged):**

| Flag | Behaviour |
|---|---|
| `load_generation` | Incremented at start; checked in every dispatch — stale loads silently drop |
| `awaiting_final_batch` | Set `true` before first dispatch; cleared in final dispatch |
| `user_interacted_after_first_batch` | Set `false` in first dispatch; gates `selectAfter` in final |
| `group_by_sort_column` | Checked in `BackfillIconsAsync` — guard preserved unchanged |

---

### Change 2 — `ReplaceAll` for the final batch

**Location:** Second `co_await resume_foreground` dispatch in `LoadDirectoryAsync`

Replace the 4970-iteration `Append` loop with a single `ReplaceAll` call:

```cpp
auto replaceable = rows.as<
    winrt::Windows::Foundation::Collections::IReplaceAll<
        winrt::FileScoutNative::FileRowViewModel>>();
replaceable.ReplaceAll(winrt::array_view<
    winrt::FileScoutNative::FileRowViewModel const>(prebuilt));
```

**Why `ReplaceAll` is faster:** Fires one `VectorChanged::Reset` event. The WinUI 3 `ListView`
handles `Reset` by discarding its item-index bookkeeping and re-virtualizing in a single pass,
instantiating `DataTemplate` instances only for visible rows. 4970 individual `ItemInserted`
events require 4970 incremental bookkeeping updates before any render can occur.

**Scroll behaviour:** `Reset` scrolls the ListView back to top. This is acceptable because:
- The gap between first-batch and final-batch is ~50–100 ms (background VM build time); users
  have not yet scrolled
- The list already scrolls to top today when `rows.Clear()` fires at navigation start

**`selectAfter` guard:** The `user_interacted_after_first_batch` check gates `SelectItemByPath`.
If the user interacts between first and final batch, selection is not attempted — identical
to current behaviour.

---

### Change 3 — Session-persistent `ImageSource` cache

**Location:** `MainWindow.xaml.h` (new field), `MainWindow.xaml.cpp` (`BackfillIconsAsync`)

Add to `MainWindow`:
```cpp
std::unordered_map<std::wstring, winrt::Microsoft::UI::Xaml::Media::ImageSource>
    m_iconSourceCache;
```

Key = lowercase file extension (e.g. `L".exe"`, `L".dll"`) or `L"__folder__"` / `L"__file__"` for
the two-bucket fallback — mirrors the key scheme already used by `ShellInteropService::m_iconCache`.

**BackfillIconsAsync — chunked dispatch with cache:**

```
BG thread: resolve IconBitmap per item (ShellInteropService cache — mostly O(1) hits)

UI thread (chunked, 500 items per co_await):
    for each item:
        ext = lowercase extension key
        if ext not in m_iconSourceCache:
            m_iconSourceCache[ext] = BuildIconSource(item.icon)  // one WriteableBitmap per ext
        rows.GetAt(i).IconSource(m_iconSourceCache[ext])          // pointer assign + INPC
```

For System32 (~30 unique extensions): ≤30 `WriteableBitmap` allocations on first navigate.
On any subsequent navigate to a directory with overlapping extensions: zero new allocations.

**Cache invalidation:** None required. Icon pixel data per extension is immutable during a
session. Cache lives for the lifetime of `MainWindow`.

**Chunk size:** 500 items per `co_await resume_foreground` dispatch. At ~1 µs per INPC call
(cheap pointer assign + cached lookup), 500 items ≈ 0.5 ms per chunk — well within one 60 fps
frame. Spreading across frames ensures the icon population does not starve input events.

---

### Change 4 — `SortItems` static annotation

`SortItems` is currently a `const` member of `MainWindow` but reads no member state — it
operates only on its arguments. Mark it `static` in the `.cpp` implementation to make the
thread-safety contract explicit and allow the compiler to verify it.

No behavioural change.

---

### Data Flow — Regular Directory Navigate (post-change)

```
LoadDirectory(path)
  └─ LoadDirectoryAsync(path, paneIndex, selectAfter)  [fire_and_forget]
       │
       ├─ UI thread:  clear stale rows, reset breadcrumbs, set awaiting_final_batch=true
       │
       ├─ co_await resume_background()
       │   ├─ EnumerateDirectory(path)          → items: vector<ShellItemRef>
       │   ├─ SortItems(items, col, dir)        → sorted in-place
       │   └─ MakeFileRowViewModel per item     → prebuilt: vector<FileRowViewModel>
       │
       ├─ co_await resume_foreground(queue)     [First dispatch]
       │   ├─ stale-generation guard
       │   ├─ update PaneContext (path, items, tabs, breadcrumbs, status bar)
       │   ├─ rows.Clear() + Append(prebuilt[0..149])
       │   └─ timing.first_batch_publish_tick
       │
       ├─ co_await resume_foreground(queue)     [Second dispatch — large dir only]
       │   ├─ stale-generation guard
       │   ├─ rows.ReplaceAll(prebuilt[0..N])   ← single Reset event
       │   ├─ awaiting_final_batch = false
       │   └─ timing.final_publish_tick → BackfillIconsAsync(...)
       │
       └─ BackfillIconsAsync(paneIndex, tabIndex, generation, items)
            ├─ co_await resume_background()
            │   └─ GetItemIcon per item (ShellInteropService cache — O(1) for warm extensions)
            └─ co_await resume_foreground(queue) [chunked, 500/chunk]
                └─ IconSource(m_iconSourceCache[ext])  per item
```

---

### Error Handling

No new error surfaces introduced. Existing exception guard in `LoadDirectoryAsync` covers the
background enumeration phase. ViewModel pre-build on the background thread runs after
enumeration — if enumeration throws, it's caught by the existing handler before pre-build runs.
`ReplaceAll` is a synchronous WinRT call on the UI thread; failure would manifest as a C++/WinRT
exception, which propagates through the coroutine and is caught by the existing `catch (...)`.

---

### Testing Approach

**New test — `LargeDirectoryEnumerationBenchmark`** (add to `ShellInteropServiceTests.cpp`):

- Setup: create temp dir with 1000 files across 10 extensions (100 files per extension)
- Measure: `GetTickCount64` before/after `EnumerateDirectory`
- Assert: elapsed < 300 ms
- Assert: all 1000 items returned with `size_available = true`, `date_modified_available = true`

This provides a regression gate for the enumeration hot path on any machine. The UI-dispatch
improvements (ReplaceAll, ImageSource cache) require a running WinUI app to measure and are
validated by manual navigation to System32 with timing logs enabled.

**Regression gate:** All existing 90+ unit tests must pass unchanged. The ViewModel pre-build
and ReplaceAll changes are internal to `LoadDirectoryAsync` — no public test surface changes.

---

### Expected Performance Outcome

| Operation | Before | After |
|---|---|---|
| Sort | UI thread, ~5–15 ms | Background thread, 0 ms on UI |
| ViewModel creation | UI thread, O(N) | Background thread, 0 ms on UI |
| Final batch publish | 4970 `Append` events | 1 `Reset` event (`ReplaceAll`) |
| `WriteableBitmap` allocs / navigate | 5120 | ≤ 30 (first navigate), 0 (warm cache) |
| **Estimated System32 total time** | **~2000 ms** | **~300–400 ms** |
