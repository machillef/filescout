# Slices: large-dir-perf

## L1: Sort + ViewModel pre-build on background thread

**Goal:** Move `SortItems` and all `MakeFileRowViewModel` calls from inside the `TryEnqueue`
dispatch lambda to the background-thread section of `LoadDirectoryAsync`. Restructure the
nested-lambda pattern to use `co_await resume_foreground(m_dispatcherQueue)` directly.

**Touched files:**
- `native/FileScout.Native/MainWindow.xaml.cpp` — `LoadDirectoryAsync` restructure

**TDD required:** Yes — existing tests must remain green; add test to verify `LoadDirectory`
does not regress on a small directory (< 150 items, single dispatch path).

**Done criteria:**
- `SortItems` call is on the background thread (before any `co_await resume_foreground`)
- `MakeFileRowViewModel` calls are on the background thread
- `LoadDirectoryAsync` uses `co_await resume_foreground(m_dispatcherQueue)` instead of nested
  `TryEnqueue` lambdas
- All existing tests pass (90+)
- Both binaries build clean
- Manual: navigate to a small directory (Downloads), confirm rows appear and sort works

**What remains unverified:** Final-batch publish improvement (L2), icon cache (L3)

**Validation:** `native/bin/x64/Debug/Tests/FileScout.Tests.exe`

---

## L2: ReplaceAll for the final batch

**Goal:** Replace the 4970-iteration `Append` loop in the second dispatch of `LoadDirectoryAsync`
with a single `rows.as<IReplaceAll<FileRowViewModel>>().ReplaceAll(prebuilt)` call.

**Touched files:**
- `native/FileScout.Native/MainWindow.xaml.cpp` — second dispatch in `LoadDirectoryAsync`

**TDD required:** Yes — add `LargeDirectoryEnumerationBenchmark` test (1000 synthetic files,
assert enumeration < 300 ms, assert count + metadata). This is the regression gate.

**Done criteria:**
- Final batch uses `ReplaceAll` (single `VectorChanged::Reset`) not a `for` loop
- `awaiting_final_batch`, `user_interacted_after_first_batch`, `selectAfter` guards all preserved
- `LargeDirectoryEnumerationBenchmark` test added and GREEN
- All existing tests pass
- Both binaries build clean
- Manual: navigate to System32 — confirm all rows appear with no visible freeze; check timing
  log (`TraceLog`) shows `final_publish_ms` < 400

**What remains unverified:** Icon backfill performance (L3)

**Validation:** `native/bin/x64/Debug/Tests/FileScout.Tests.exe`

---

## L3: Session-persistent ImageSource cache + chunked BackfillIconsAsync

**Goal:** Add `m_iconSourceCache: unordered_map<wstring, ImageSource>` to `MainWindow`.
Rewrite `BackfillIconsAsync` to use the cache (one `WriteableBitmap` per unique extension per
session) and chunk UI-thread dispatches at 500 items.

**Touched files:**
- `native/FileScout.Native/MainWindow.xaml.h` — add `m_iconSourceCache` field
- `native/FileScout.Native/MainWindow.xaml.cpp` — `BackfillIconsAsync` rewrite

**TDD required:** No new testable logic — `BuildIconSource` and `IconSource()` are WinUI calls
not reachable from the unit test binary. Regression: existing tests must pass unchanged.

**Done criteria:**
- `m_iconSourceCache` field exists in `MainWindow`
- `BackfillIconsAsync` looks up cache before calling `BuildIconSource`; inserts on miss
- UI dispatch chunked at 500 items per `co_await resume_foreground`
- `group_by_sort_column` guard preserved
- All existing tests pass
- Both binaries build clean
- Manual: navigate to System32 twice in a row — second navigate shows icons without any
  noticeable icon-backfill stutter; TraceLog shows "BackfillIcons" completes quickly

**What remains unverified:** Nothing — L3 completes the initiative.

**Validation:** `native/bin/x64/Debug/Tests/FileScout.Tests.exe`

---

## L4: SortItems static annotation (cleanup)

**Goal:** Mark `SortItems` as `static` in the implementation. Zero behaviour change.

**Touched files:**
- `native/FileScout.Native/MainWindow.xaml.h` — change `void SortItems(...)` declaration
- `native/FileScout.Native/MainWindow.xaml.cpp` — add `static` or change to free function

**TDD required:** No — compile-only verification.

**Done criteria:**
- `SortItems` has no implicit `this` access
- Both binaries build clean with no new warnings

**Validation:** Build only.
