# Icon Loading Performance (Parallel) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Supersedes:** `2026-04-09-icon-loading-performance.md` (deduplication approach)

**Goal:** Reduce icon backfill time from 2-3 seconds to <500ms for large folders by parallelizing icon extraction across multiple threads.

**Architecture:** Replace the serial `for` loop in `BackfillIconsAsync` with parallel `std::async` tasks. Each worker processes a disjoint slice of items. After all workers complete, the existing chunked UI dispatch applies icons to rows.

**Tech Stack:** C++20, C++/WinRT, WinUI 3, `std::async`/`std::future`, MSBuild/v143, GoogleTest

---

## File Map

| File | Change |
|------|--------|
| `native/FileScout.Native/pch.h` | Add `#include <future>` |
| `native/FileScout.Native/MainWindow.xaml.cpp` | Parallelize `BackfillIconsAsync` and `BackfillSearchResultIconsAsync` |

**Total: 2 files**

---

### Task 1: Add Future Header to PCH

**Files:**
- Modify: `native/FileScout.Native/pch.h:33`

- [ ] **Step 1: Add the include**

Open `native/FileScout.Native/pch.h` and add `#include <future>` after `#include <thread>` (line 33):

```cpp
#include <thread>
#include <future>
```

- [ ] **Step 2: Verify build still passes**

Run:
```bash
powershell -Command "cmd /c 'call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal'"
```

Expected: Build succeeds with no errors.

- [ ] **Step 3: Commit**

```bash
git add native/FileScout.Native/pch.h
git commit -m "chore: add <future> header for parallel icon loading"
```

---

### Task 2: Parallelize BackfillIconsAsync

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp:3337-3338`

- [ ] **Step 1: Replace serial loop with parallel pattern**

In `BackfillIconsAsync`, replace the serial loop (lines 3337-3338):

```cpp
for (auto& item : items)
    item.icon = m_shellInterop.GetItemIcon(item.path, item.is_folder);
```

With parallel loading:

```cpp
// Parallel icon extraction: split items across worker threads.
auto const itemCount = items.size();
if (itemCount > 0)
{
    auto const workerCount = (std::min)(
        static_cast<size_t>(std::thread::hardware_concurrency()),
        static_cast<size_t>(8));
    auto const chunkSize = (itemCount + workerCount - 1) / workerCount;

    std::vector<std::future<void>> futures;
    futures.reserve(workerCount);

    for (size_t w = 0; w < workerCount; ++w)
    {
        auto const start = w * chunkSize;
        auto const end = (std::min)(start + chunkSize, itemCount);
        if (start >= itemCount) break;

        futures.push_back(std::async(std::launch::async,
            [this, &items, start, end]()
            {
                for (size_t i = start; i < end; ++i)
                    items[i].icon = m_shellInterop.GetItemIcon(
                        items[i].path, items[i].is_folder);
            }));
    }

    for (auto& f : futures)
    {
        try { f.get(); }
        catch (...) { /* Worker failed — icons remain empty, continue */ }
    }
}
```

- [ ] **Step 2: Verify build passes**

Run:
```bash
powershell -Command "cmd /c 'call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal'"
```

Expected: Build succeeds.

- [ ] **Step 3: Run existing tests**

Run:
```bash
native/bin/x64/Debug/Tests/FileScout.Tests.exe
```

Expected: All 194 tests pass.

- [ ] **Step 4: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml.cpp
git commit -m "perf: parallelize BackfillIconsAsync icon extraction

Use std::async to distribute icon extraction across multiple threads.
For a folder with 200 items, this reduces backfill time from ~2-3s
(serial) to ~500ms (parallel with 4-8 workers)."
```

---

### Task 3: Parallelize BackfillSearchResultIconsAsync

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp:3398-3406`

- [ ] **Step 1: Replace serial loop with parallel pattern (respecting cancelToken)**

In `BackfillSearchResultIconsAsync`, replace the serial loop (lines 3398-3406):

```cpp
for (auto& item : items)
{
    if (cancelToken && cancelToken->load(std::memory_order_relaxed))
    {
        co_return;
    }

    item.icon = m_shellInterop.GetItemIcon(item.path, item.is_folder);
}
```

With parallel loading that checks cancelToken:

```cpp
// Parallel icon extraction with cancellation support.
auto const itemCount = items.size();
std::atomic<bool> anyWorkerCancelled{ false };

if (itemCount > 0 && !(cancelToken && cancelToken->load(std::memory_order_relaxed)))
{
    auto const workerCount = (std::min)(
        static_cast<size_t>(std::thread::hardware_concurrency()),
        static_cast<size_t>(8));
    auto const chunkSize = (itemCount + workerCount - 1) / workerCount;

    std::vector<std::future<void>> futures;
    futures.reserve(workerCount);

    for (size_t w = 0; w < workerCount; ++w)
    {
        auto const start = w * chunkSize;
        auto const end = (std::min)(start + chunkSize, itemCount);
        if (start >= itemCount) break;

        futures.push_back(std::async(std::launch::async,
            [this, &items, start, end, cancelToken, &anyWorkerCancelled]()
            {
                for (size_t i = start; i < end; ++i)
                {
                    if (cancelToken && cancelToken->load(std::memory_order_relaxed))
                    {
                        anyWorkerCancelled.store(true, std::memory_order_relaxed);
                        return;
                    }
                    items[i].icon = m_shellInterop.GetItemIcon(
                        items[i].path, items[i].is_folder);
                }
            }));
    }

    for (auto& f : futures)
    {
        try { f.get(); }
        catch (...) { /* Worker failed — continue */ }
    }
}

if (anyWorkerCancelled.load(std::memory_order_relaxed))
{
    co_return;
}
```

- [ ] **Step 2: Verify build passes**

Run:
```bash
powershell -Command "cmd /c 'call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal'"
```

Expected: Build succeeds.

- [ ] **Step 3: Run existing tests**

Run:
```bash
native/bin/x64/Debug/Tests/FileScout.Tests.exe
```

Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml.cpp
git commit -m "perf: parallelize BackfillSearchResultIconsAsync

Apply same parallel pattern to search result icon loading.
Respects cancelToken for early termination when search is cancelled."
```

---

### Task 4: Manual Verification

**Files:** None (manual testing)

- [ ] **Step 1: Build Release configuration**

Run:
```bash
powershell -Command "cmd /c 'call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Release /p:Platform=x64 /nologo /v:minimal'"
```

- [ ] **Step 2: Launch FileScout**

Run:
```bash
native/bin/x64/Release/FileScout.Native/FileScout.Native.exe
```

- [ ] **Step 3: Navigate to Downloads folder**

1. Open Downloads folder (or another folder with 100+ items)
2. Observe icon loading time
3. **Target:** All icons should appear within ~500ms (down from 2-3s)
4. **Verify:** `.exe` files show their unique application icons (not generic)

- [ ] **Step 4: Test search results**

1. Run a recursive search that returns 50+ results
2. Observe icon loading in search results
3. **Verify:** Icons load quickly, cancellation works (start new search mid-load)

- [ ] **Step 5: Document results**

If performance target met, plan is complete. If not, consider:
- Increasing worker count cap
- Adding the deduplication optimization from the April 9th spec

---

## Success Criteria Checklist

- [ ] Large folders complete icon backfill in <500ms
- [ ] All icons appear at roughly the same time (uniform completion)
- [ ] `.exe` per-item icons remain correct
- [ ] Existing tests pass (194 tests)
- [ ] No visible UI thread stutter during icon application
