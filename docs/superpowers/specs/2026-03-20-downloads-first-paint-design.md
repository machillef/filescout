# FileScout Downloads First Paint — Design Spec

**Date:** 2026-03-20
**Scope:** Improve perceived load speed for `Downloads`-style mixed folders by publishing visible rows earlier without changing folder semantics or visible metadata completeness.
**Baseline:** Existing WinUI 3 / C++/WinRT app in `native/FileScout.Native/`
**Target:** Active-pane folder loads, with `Downloads` as the motivating workload and a generic implementation.

---

## Summary of Decisions

| Area | Decision |
|---|---|
| Primary target | `Downloads`-style mixed file folders |
| Implementation shape | Generic active-pane improvement, not path-specific hardcoding |
| Main technique | Staged row publication |
| Enumeration | Keep current enumeration behavior for this slice |
| Sorting | Keep current full-list sort behavior for this slice |
| First paint content | Real rows with complete visible metadata, not placeholders |
| Publish count | One first-batch publish and one final publish |
| First batch size | `min(150, total_items)` |

---

## 1. Scope

This slice improves perceived load speed by changing **when rows are materialized and published**, not by changing the final folder result.

### Included
- Active-pane first-batch row publication
- Full final row publication after the initial batch
- Timing capture for first-batch and final completion
- Preservation of current visible columns in the first batch

### Explicitly not included
- No path-specific `Downloads` special case
- No placeholder rows
- No archive capability rewrite
- No metadata deferral in this slice
- No deep list virtualization rewrite
- No inactive-pane or inactive-tab changes

The design goal is simple: mixed folders like `Downloads` should stop feeling blank or stuck while the app is still building the full row set.

---

## 2. Behavior

### Load flow
1. Enumerate directory contents on the background thread exactly as today.
2. Sort the full item list once on the UI thread exactly as today.
3. Materialize and publish the first batch of rows immediately.
4. Record first-batch timing when that publish completes.
5. Materialize and publish the full final row set.
6. Record final-completion timing when that publish completes.

### First batch rule
- First batch size is `min(150, total_items)`.
- The first batch must be the exact prefix of the final sorted order.
- Visible columns in the first batch must already contain the same metadata they show today.

### Correctness constraints
- Apply this only to the active pane load path.
- Guard both first and final publish with the existing `load_generation` stale-load cancellation.
- Final publish must not clobber live interaction that happens after the first batch appears.

### Publish model
- Exactly two publishes in this slice:
  - first batch
  - final full set
- No repeated chunk loop in this slice.

---

## 3. Implementation Shape

### Current bottleneck
Today the load path sorts the full item list, then synchronously builds every row before anything renders:
- `LoadDirectoryAsync(...)` in `native/FileScout.Native/MainWindow.xaml.cpp`
- `UpdateRows(...)` in `native/FileScout.Native/MainWindow.xaml.cpp`

That is a bad fit for mixed folders like `Downloads`, where the final row creation still blocks visible content even after enumeration is done.

### Planned change
- Keep `EnumerateDirectory(...)` intact for this slice.
- Keep full sorting intact for this slice.
- Refactor row publication so the active pane can:
  - build and publish a first batch
  - then build and publish the final full set

### Expected touched files
- `native/FileScout.Native/MainWindow.xaml.cpp`
- `native/FileScout.Native/MainWindow.xaml.h`

No Shell layer changes are required for this slice unless implementation proves blocked.

---

## 4. Verification

### Functional verification
- Build passes
- Tests pass
- Manual runtime check on `Downloads`
- First visible rows appear sooner than before
- Final visible state matches the existing sorted result

### Timing verification
- Use existing `PerfLoad` trace instrumentation
- Capture:
  - enumerate time
  - first-batch time
  - full-completion time

### Acceptance target
- First-batch timing improves materially on the target mixed folder workload
- Full-completion timing does not regress unacceptably

---

## 5. Success Criteria

- [ ] `Downloads` feels faster to open because visible rows appear earlier
- [ ] First batch is published without placeholder metadata
- [ ] Final published order matches the current sorted behavior
- [ ] No stale-load regressions
- [ ] No selection/focus clobber after first paint
- [ ] Build passes
- [ ] Tests pass

