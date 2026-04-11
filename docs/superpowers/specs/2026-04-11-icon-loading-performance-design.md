# Icon Loading Performance Design (Parallel Approach)

**Supersedes:** `2026-04-09-icon-loading-performance-design.md`

## Goal

Reduce icon backfill time from 2-3 seconds to under 500ms for large folders (e.g., Downloads with 200+ items), with uniform completion — all icons appearing at roughly the same time.

## Problem

`BackfillIconsAsync` processes icons serially:

```cpp
for (auto& item : items)
    item.icon = m_shellInterop.GetItemIcon(item.path, item.is_folder);
```

For 200 items where each `SHGetFileInfoW` call takes ~10-50ms, this results in 2-3 seconds total before ANY icons reach the UI.

## Approach: Thread Pool Parallel Loading

Replace the serial loop with parallel `std::async` tasks. Multiple workers extract icons concurrently, then all icons dispatch to the UI together.

### Why This Over Deduplication

The prior spec (April 9th) proposed grouping by cache key to reduce duplicate work. However:
- The existing `m_iconCache` in `ShellInteropService` already caches by key
- First call for each unique extension still requires shell work
- `.exe` files require per-item extraction (no dedup possible)
- The serial loop itself is the bottleneck

Parallel loading directly attacks the serial bottleneck. For 4 workers, a 2-second serial job becomes ~500ms.

## Design

### Concurrency Model

```cpp
// In BackfillIconsAsync, after co_await resume_background():
auto const workerCount = (std::min)(std::thread::hardware_concurrency(), 8u);
auto const chunkSize = (items.size() + workerCount - 1) / workerCount;

std::vector<std::future<void>> futures;
for (uint32_t w = 0; w < workerCount; ++w)
{
    auto start = w * chunkSize;
    auto end = (std::min)(start + chunkSize, items.size());
    if (start >= items.size()) break;
    
    futures.push_back(std::async(std::launch::async, [&items, start, end, this]() {
        for (auto i = start; i < end; ++i)
            items[i].icon = m_shellInterop.GetItemIcon(items[i].path, items[i].is_folder);
    }));
}

for (auto& f : futures)
{
    try { f.get(); }
    catch (...) { /* log, continue */ }
}

// ...existing chunked UI dispatch unchanged...
```

### Worker Count

`min(std::thread::hardware_concurrency(), 8)` — typically 4-8 workers on modern machines.

### Work Distribution

Static partitioning: divide items evenly among workers. Each worker processes a disjoint slice of the items vector.

### Thread Safety

Already handled:
- `GetItemIcon` uses `m_cacheMutex` to protect `m_iconCache`
- Each worker writes to non-overlapping vector indices
- `SHGetFileInfoW` has existing fallback for shell extension STA issues

### Error Handling

`GetItemIcon` never throws — returns empty `IconBitmap` on failure. The `future::get()` call is wrapped in try-catch to handle unexpected exceptions gracefully.

## Files Changed

| File | Change |
|------|--------|
| `MainWindow.xaml.cpp` | `BackfillIconsAsync` — replace serial loop with parallel pattern |
| `MainWindow.xaml.cpp` | `BackfillSearchResultIconsAsync` — same pattern, respecting `cancelToken` |

**Total: 1 file, 2 functions.**

## Files Unchanged

- `ShellInteropService.cpp` — already thread-safe
- `IconCacheKey.h` — pure functions
- `MainWindow.xaml.h` — no signature changes

## Testing

### Unit Test

Extend `RowIconStateTests.cpp`:
- Verify parallel loading produces same results as serial (determinism)
- Verify cache key behavior unchanged

### Manual Verification

1. Open Downloads folder with 100+ items
2. Measure time from navigation to all icons visible
3. Target: <500ms (down from 2-3s)

## Success Criteria

- [ ] Large folders complete icon backfill in <500ms
- [ ] All icons appear at roughly the same time (uniform completion)
- [ ] `.exe` per-item icons remain correct
- [ ] Existing tests pass
- [ ] No visible UI thread stutter during icon application

## Risks

### SHGetFileInfoW threading issues

Some shell extensions expect STA context. Mitigated by existing fallback to extension-based lookup when literal-path extraction fails.

### Diminishing returns on high-core machines

Capped at 8 workers to avoid thread overhead exceeding benefit.

## Future Optimization

The April 9th deduplication approach (group by cache key, extract once per unique key) could be combined with parallel loading for additional gains. However, parallel loading alone should achieve the <500ms target.
