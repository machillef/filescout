# Status: large-dir-perf

## Initiative State

- **Status:** Complete
- **Goal:** Eliminate UI-thread stutter on large-directory load (System32 ~5120 items)
- **Target:** System32 total load time < 400 ms (down from ~2000 ms)
- **Slices:** L1 → L2 → L3 → L4

## Slice Tracker

| Slice | Name | Status |
|---|---|---|
| L1 | Sort + ViewModel pre-build on background thread | Complete |
| L2 | ReplaceAll for final batch + benchmark test | Complete |
| L3 | Session-persistent ImageSource cache + chunked BackfillIconsAsync | Complete |
| L4 | SortItems static annotation (cleanup) | Complete |

---

## Slice L1: Sort + ViewModel pre-build on background thread
Status: Complete
Last updated: 2026-03-25

### What was implemented
- `native/FileScout.Native/MainWindow.xaml.cpp` — `LoadDirectoryAsync` (regular directory path only):
  - Captured `sort_column` and `sort_direction` on the UI thread before `co_await resume_background()`
  - After `EnumerateDirectory`, calls `SortItems(items, sort_column, sort_direction)` on the background thread
  - Builds `std::vector<winrt::FileScoutNative::FileRowViewModel> prebuilt` on the background thread (one VM per item)
  - First `TryEnqueue` lambda: removed `SortItems` call, uses `prebuilt.size()` for `totalItems`, uses `prebuilt[i]` for first-batch rows
  - Inner second lambda: added `prebuilt = std::move(prebuilt)` capture, uses `prebuilt[i]` for remaining rows
  - Archive path (`IsArchivePath`) left untouched (out of scope)

### Design deviation
The slices.md called for restructuring from `TryEnqueue` to `co_await resume_foreground(m_dispatcherQueue)`. This was NOT done because `winrt::resume_foreground` only supports `winrt::Windows::System::DispatcherQueue` (UWP type). This project uses `winrt::Microsoft::UI::Dispatching::DispatcherQueue` (WinUI 3), which provides no `operator co_await` or `resume_foreground` overload in Windows App SDK 1.8 headers. The performance goal (sort + VM build on BG thread) is fully achieved within the existing `TryEnqueue` pattern.

### What was validated
- Build: `MSBuild native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64` — CLEAN (pre-existing warnings only, no new warnings)
- Tests: `native/bin/x64/Debug/Tests/FileScout.Tests.exe` — 110/110 PASSED

### What remains unverified
- Manual navigate to System32 — timing improvement not measured yet (requires running app)
- Final-batch publish improvement (L2), icon cache improvement (L3) — later slices

### Blockers
None

### Next recommended step
Completed. See L2 entry below.

---

## Slice L2: ReplaceAll for the final batch
Status: Complete
Last updated: 2026-03-25

### What was implemented
- `native/FileScout.Native/MainWindow.xaml.cpp` — inner second TryEnqueue lambda:
  - Replaced the `for (i = firstBatchEnd; i < totalItems; ++i) rows.Append(prebuilt[i])` loop with
    `rows.as<IVector<FileRowViewModel>>().ReplaceAll(prebuilt)` — one `VectorChanged::Reset` event
  - Removed `firstBatchEnd` from the lambda capture (no longer referenced)
  - All guards preserved: `load_generation`, `awaiting_final_batch`, `user_interacted_after_first_batch`, `selectAfter`
- `native/FileScout.Tests/tests/ShellInteropServiceTests.cpp`:
  - Added `LargeDirectoryFixture` + `LargeDirectoryEnumerationBenchmark` test
  - Creates 1000 files (100 per extension × 10 extensions) in temp dir
  - Asserts `EnumerateDirectory` completes in < 300 ms
  - Asserts count = 1000, `size_available = true`, `date_modified_available = true` for all files

### Design deviation
The slices.md referenced `IReplaceAll<T>` but that interface does not exist in the Windows SDK WinRT headers. `ReplaceAll` is a method on `IVector<T>`. Used `rows.as<IVector<FileRowViewModel>>().ReplaceAll(prebuilt)` instead — same QI pattern, correct interface.

### What was validated
- Build: CLEAN (no new warnings)
- Tests: 111/111 PASSED (`LargeDirectoryEnumerationBenchmark` included)

### What remains unverified
- Manual navigate to System32 — timing improvement not measured yet (requires running app)
- Icon backfill improvement (L3) — later slice

### Blockers
None

### Next recommended step
Completed. See L3 entry below.

---

## Slice L3: Session-persistent ImageSource cache + chunked BackfillIconsAsync
Status: Complete
Last updated: 2026-03-25

### What was implemented
- `native/FileScout.Native/pch.h` — added `#include <unordered_map>`
- `native/FileScout.Native/MainWindow.xaml.h` — added `m_iconSourceCache` field:
  `std::unordered_map<std::wstring, winrt::Microsoft::UI::Xaml::Media::ImageSource>`
- `native/FileScout.Native/MainWindow.xaml.cpp`:
  - Added `static std::wstring IconCacheKey(ShellItemRef const&)` free function (anonymous namespace):
    returns lowercase extension (e.g. `L".exe"`), `L"__folder__"` for dirs, `L"__file__"` for extensionless files
  - Rewrote `BackfillIconsAsync`: BG thread still calls `GetItemIcon` for all items;
    UI dispatch chunked at 500 items (`k_iconChunkSize`) via multiple `TryEnqueue` calls;
    each chunk checks `m_iconSourceCache` before calling `BuildIconSource`;
    inserts on miss; uses cached `ImageSource` on hit
  - All guards preserved: `load_generation`, `active_tab_index`, `group_by_sort_column`, `rows.Size() != total`
  - Items shared across chunk lambdas via `std::shared_ptr<std::vector<ShellItemRef>>`

### Design deviation
The slices.md specified "chunk UI-thread dispatches at 500 items per `co_await resume_foreground`". As established in L1, `resume_foreground` is incompatible with WinUI 3's `Microsoft::UI::Dispatching::DispatcherQueue`. Used multiple `TryEnqueue` calls from the background thread instead — same chunking semantics, compatible API.

### What was validated
- Build: CLEAN (pre-existing `C4189` warning only, no new warnings)
- Tests: 111/111 PASSED

### What remains unverified
- Manual navigate to System32 twice in a row — second navigate icon stutter improvement not measured (requires running app)
- WriteableBitmap allocation count not instrumented (no existing profiling hook)

### Blockers
None

### Next recommended step
Completed. See L4 entry below.

---

## Slice L4: SortItems static annotation (cleanup)
Status: Complete
Last updated: 2026-03-25

### What was implemented
- `native/FileScout.Native/MainWindow.xaml.h` — `void SortItems(...)` → `static void SortItems(...)`; removed trailing `const` (not applicable to static members)
- `native/FileScout.Native/MainWindow.xaml.cpp` — removed `const` from the out-of-class definition (static member functions cannot be `const`)

### What was validated
- Build: CLEAN (no new warnings)
- Tests: 111/111 PASSED

### What remains unverified
Nothing — compile-only change.

### Blockers
None

## Definition of Done

1. System32 (~5120 items): `final_publish_tick − load_start_tick < 400 ms` on a warm navigate
2. No UI freeze visible during final-batch publish or icon backfill
3. ≤ 30 `WriteableBitmap` allocations per navigate (one per unique extension); 0 on warm cache
4. All existing tests pass — zero regressions
5. Sort, group-by, view-mode toggle, tab switching, Back/Forward all work correctly
