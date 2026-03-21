# FileScout Downloads First Paint Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make mixed folders like `Downloads` feel faster to open by publishing the first visible batch of rows sooner while preserving the final sorted result and visible metadata completeness.

**Architecture:** This is a narrow active-pane load-path change in `MainWindow`. `LoadDirectoryAsync()` keeps background enumeration and full sorting unchanged, but row publication is split into an early first batch and a final completion publish. The Shell layer stays untouched in phase 1 unless implementation gets blocked, and stale-load cancellation remains authoritative at both publish points.

**Tech Stack:** WinUI 3, Windows App SDK 1.8, C++/WinRT, XAML, MSBuild Debug x64, GoogleTest, existing `PerfLoad` trace logging

**Spec:** `docs/superpowers/specs/2026-03-20-downloads-first-paint-design.md`

**Build command:**
```cmd
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"
```

**Test command:**
```cmd
native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no
```

---

## File Map

| File | What changes |
|---|---|
| `native/FileScout.Native/MainWindow.xaml.cpp` | Split active-pane row publication into a first batch and a final publish, keep timing instrumentation accurate, and preserve live interaction correctness across staged loads |
| `native/FileScout.Native/MainWindow.xaml.h` | Add only the minimal timing/state helpers needed for staged publication and cancellation bookkeeping |
| `docs/ai/status.md` | Record the slice outcome, benchmark folder, timing medians, and residual risks |
| `docs/ai/plan.md` | Update the current initiative state and the next likely perf follow-up after this slice lands |

No XAML change is expected. No `ShellInteropService` change is planned in phase 1 unless a tiny helper-only change is unavoidable.

---

## Task 1: Reconfirm the current load path and baseline the target workload

**Files:**
- Read: `native/FileScout.Native/MainWindow.xaml.cpp`
- Read: `native/FileScout.Native/MainWindow.xaml.h`
- Read: `native/FileScout.Native/Shell/ShellInteropService.cpp`

This task is about confirming the exact row-publication boundary and capturing a clean before baseline for a representative `Downloads` workload.

- [ ] **Step 1: Re-read the active-pane load path**

Inspect:
- `LoadDirectoryAsync()`
- `UpdateRows()`
- `SyncSelectionFromListView()`
- `RestoreSelectionFromState()`

Confirm the current sequence is still:
1. clear active pane UI state
2. enumerate on background thread
3. sort items on the UI thread
4. build all rows synchronously
5. publish final state

- [ ] **Step 2: Choose the benchmark folder**

Use the same local workload consistently for the whole slice:
- target folder: `C:\Users\machi\Downloads`
- verify its current item mix and keep it as the motivating mixed-folder workload

If `Downloads` turns out to be too unstable to reproduce manually, use another mixed folder of similar shape and document the fallback in `docs/ai/status.md`.

- [ ] **Step 3: Capture the true baseline**

Before changing behavior, measure the current implementation against the chosen folder and record:
- median time to first visible row under the current all-at-once publish behavior
- median time to full completion

Use the existing `PerfLoad` trace output and keep the same:
- folder
- window size
- theme
- view mode
- sort/group settings

Run `5` baseline measurements before Task 2 begins.

- [ ] **Step 4: Build before editing behavior**

Run:

```cmd
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"
```

Expected:
- build succeeds
- only the known `MSB8027` / `LNK4042` warnings remain

---

## Task 2: Split active-pane row publication into first batch and final completion

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp`
- Modify if needed: `native/FileScout.Native/MainWindow.xaml.h`

This is the core change. Keep enumeration and full sorting behavior intact, but split visible row publication so the active pane can show a prefix batch before the final completion publish.

- [ ] **Step 1: Preserve the existing sorted final result**

Keep the current background enumeration and UI-thread sort unchanged. Do not change icon policy, archive detection, or shell enumeration logic in this slice.

- [ ] **Step 2: Add the minimum state needed for staged publication**

Introduce only the smallest timing/state additions required to track:
- load start
- enumeration end
- first-batch publish
- final publish

Keep the state local to `MainWindow` / pane context. Do not add a new subsystem.

- [ ] **Step 3: Publish the first batch**

Refactor the row-materialization path so the active pane can publish:
- `min(150, total_items)` rows first
- then the remainder in one final pass

The first batch must be the exact prefix of the final sorted order and must contain complete visible metadata, not placeholders.

- [ ] **Step 4: Publish the final completion state**

After the first batch is visible, publish the remaining rows as one final completion step. Do not introduce a repeated chunk loop in this slice.

- [ ] **Step 5: Preserve stale-load cancellation**

Keep `load_generation` authoritative for both publish points. Stale results must be dropped before they can replace the active pane state.

- [ ] **Step 6: Build after the staged publication change**

Run:

```cmd
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"
```

Expected:
- build succeeds
- only the known `MSB8027` / `LNK4042` warnings remain

- [ ] **Step 7: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml.cpp native/FileScout.Native/MainWindow.xaml.h
git commit -m "perf: stage active pane row publication"
```

---

## Task 3: Preserve live interaction correctness after first paint

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp`
- Modify if needed: `native/FileScout.Native/MainWindow.xaml.h`

The UI must stay responsive after the first batch appears. Deferred restoration must not overwrite a real user action that happens during the staged load.

- [ ] **Step 1: Re-read selection and restoration flow**

Inspect:
- `pane.selection`
- `selected_item_index`
- `tab_switching`
- `SyncSelectionFromListView()`
- `RestoreSelectionFromState(...)`

Understand how these interact with row publication today.

- [ ] **Step 2: Add the smallest live-interaction guard**

Add only the minimum state needed to detect whether the user has interacted with the active pane after first paint.

The guard only needs to protect:
- selection
- focus
- scroll state

- [ ] **Step 3: Make final restoration conditional**

Allow final completion to restore pending state only when the user has not already taken control after first paint. Once the user interacts, live state wins.

- [ ] **Step 4: Keep ordering and selection stable**

Verify that:
- the first batch remains a prefix of the final order
- the final publish does not scramble sort/group order
- if a selected item is outside the first batch, it can appear only at final completion
- the first batch rows already contain the same visible metadata they would show in the final state

- [ ] **Step 5: Run the focused tests**

Run:

```cmd
native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no
```

Expected:
- all existing tests pass

If a cheap, isolated helper test becomes obvious, add it. Do not expand into a new UI automation suite here.

- [ ] **Step 6: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml.cpp native/FileScout.Native/MainWindow.xaml.h native/FileScout.Tests
git commit -m "fix(perf): preserve live pane state during staged loads"
```

---

## Task 4: Benchmark, validate, and record the result

**Files:**
- Modify: `docs/ai/status.md`
- Modify: `docs/ai/plan.md`

This task proves the slice against the same folder and timing protocol used for the baseline.

- [ ] **Step 1: Capture post-change timings**

Run the same `Downloads` workload for `5` post-change measurements and record the median:
- enumerate time
- first-batch time
- full-completion time

 - [ ] **Step 2: Check the acceptance thresholds**

Compare against the baseline from Task 1 and record whether the change materially improves first visible row latency without an obvious full-completion regression. Use the spec's acceptance language rather than inventing a new hard gate.

- [ ] **Step 3: Run the full build**

Run:

```cmd
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"
```

Expected:
- exit code `0`
- only the known `MSB8027` / `LNK4042` warnings

- [ ] **Step 4: Run the tests**

Run:

```cmd
native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no
```

Expected:
- all tests pass

- [ ] **Step 5: Manual runtime check**

Validate in the running app:
- `Downloads` shows visible rows sooner
- the pane remains responsive while the rest of the rows complete
- no stale-load flashes occur during rapid navigation
- no obvious selection or focus clobber happens after first paint
- final ordering remains stable
- the first visible rows already show complete visible metadata

- [ ] **Step 6: Update durable docs**

Update `docs/ai/status.md` with:
- the benchmark folder
- the median before/after timings
- the build/test/manual validation outcome
- any remaining bottlenecks

Update `docs/ai/plan.md` with:
- the new current state
- the next likely perf focus if this slice lands cleanly

- [ ] **Step 7: Commit**

```bash
git add docs/ai/status.md docs/ai/plan.md
git commit -m "docs: record downloads first-paint results"
```

---

## Guardrails

- Do not widen phase 1 into `ShellInteropService` optimization unless implementation is blocked.
- Do not introduce repeated chunked row appends.
- Do not optimize inactive panes or inactive tabs in this slice.
- Do not regress selection, focus, scroll position, or final order to improve first paint.
- Do not claim success without median before/after timings from the same workload.

---

## Final Verification Checklist

- [ ] Build succeeds with only known `MSB8027` / `LNK4042` warnings
- [ ] `FileScout.Tests.exe` passes
- [ ] Median first-batch time meets the agreed improvement threshold
- [ ] Median full-completion time does not regress beyond the allowed threshold
- [ ] Manual runtime validation confirms earlier rows and responsive interaction
- [ ] `docs/ai/status.md` and `docs/ai/plan.md` are updated with measured results
