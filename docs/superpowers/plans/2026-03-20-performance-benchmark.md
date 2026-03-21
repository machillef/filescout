# Large-Folder First-Paint Performance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `1k-5k` item folders feel faster to open by showing the first visible batch of rows sooner, while keeping the window responsive and preserving correctness in the active pane load path.

**Architecture:** This is a narrow phase-1 performance slice centered on `MainWindow`'s existing folder-load pipeline. `LoadDirectoryAsync()` remains responsible for background enumeration, sorting, and stale-load cancellation; the change is to instrument the path and split visible row publication into a first batch and a final publish for the active pane only. Enumeration behavior, shell metadata/icon strategy, inactive pane work, and deeper virtualization are explicitly deferred.

**Tech Stack:** WinUI 3, Windows App SDK 1.8, C++/WinRT, XAML, MSBuild Debug x64, GoogleTest, UI-thread timing instrumentation

**Spec:** `docs/superpowers/specs/2026-03-20-performance-benchmark-design.md`

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
| `native/FileScout.Native/MainWindow.xaml.cpp` | Add phase timing instrumentation, split active-pane row publication into first batch plus final publish, and preserve live user interaction state across staged publication |
| `native/FileScout.Native/MainWindow.xaml.h` | Add only the minimal helper declarations or small state required for staged publish and timing bookkeeping |
| `docs/ai/status.md` | Record the performance slice outcome, timings, benchmark folder used, and residual risks |
| `docs/ai/plan.md` | Update current focus / next-step summary after the phase-1 performance slice lands |

No XAML changes are expected in this slice. No `ShellInteropService` changes are planned in phase 1 unless a tiny helper-only change is required during implementation review.

---

## Task 1: Instrument the current active-pane load pipeline

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp`
- Modify if needed: `native/FileScout.Native/MainWindow.xaml.h`

This task adds lightweight timing to the existing `LoadDirectoryAsync()` path before the staged publish behavior changes. It should create a baseline and preserve the current load semantics.

- [ ] **Step 1: Inspect the existing load path boundaries**

Read:
- `MainWindow::LoadDirectoryAsync()`
- `MainWindow::UpdateRows()`
- `MainWindow::SyncSelectionFromListView()`
- `MainWindow::RestoreSelectionFromState()`

Confirm the current sequence:
1. clear active pane UI state
2. enumerate on background thread
3. sort items on UI thread
4. assign pane/tab state
5. build all row UI
6. publish final state

- [ ] **Step 2: Add a small perf-timing data shape**

Introduce the minimum timing fields needed to capture:
- load start
- enumeration end
- first-batch publish time
- final publish time

Keep the timing storage local to `MainWindow` / pane state. Do not create a new subsystem.

- [ ] **Step 3: Capture canonical UI-thread timing points**

Instrument the active pane load path so the code can report:
- `time to first visible batch`: UI-thread timestamp immediately after the first-batch row source is assigned/populated
- `time to full completion`: UI-thread timestamp immediately after the final remaining rows are assigned/populated

Do not swap in frame-present timing in phase 1.

- [ ] **Step 4: Log or surface the timing results in a bounded way**

Use existing lightweight logging/status patterns already in the repo where practical. The result needs to be observable during manual benchmark runs without introducing a new diagnostics framework.

- [ ] **Step 5: Capture the true baseline before changing behavior**

Choose and record one representative local folder in the `1k-5k` item range and keep the same:
- path
- approximate item count
- view mode
- sort/group mode
- window size
- theme

Run `5` baseline measurements against the unmodified behavior path and record the median for:
- time to first visible batch
- time to full completion

This must happen before Task 2 begins so the baseline reflects the pre-staged-publish behavior.

- [ ] **Step 6: Build before changing behavior**

Run the build command.

Expected:
- build succeeds
- only known `MSB8027` / `LNK4042` warnings remain

- [ ] **Step 7: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml.cpp native/FileScout.Native/MainWindow.xaml.h
git commit -m "feat(perf): instrument folder load timings"
```

---

## Task 2: Split visible row publication into first batch and final publish

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp`
- Modify if needed: `native/FileScout.Native/MainWindow.xaml.h`

This is the core phase-1 change. Keep enumeration, sorting, and grouped order intact; only split row materialization/publication for the active pane.

- [ ] **Step 1: Preserve the existing final ordered item list**

Keep the current background enumeration and UI-thread `SortItems(...)` behavior. Do not change shell enumeration behavior, archive detection, metadata extraction, or icon policy here.

- [ ] **Step 2: Define the phase-1 first-batch rule in code**

Use the approved contract:
- first batch size = `min(150, total_items)`
- first batch must be a prefix of the final sorted/grouped order
- phase 1 uses one first publish and one final publish only
- no repeated chunked append loop in this slice

- [ ] **Step 3: Refactor row building so partial publication is possible**

Extract or adapt the existing row creation path so the code can build:
- rows for a prefix batch
- rows for the full final set

Do not redesign the row visuals or the `CreateRowView(...)` pipeline beyond what is needed to support the staged publish.

- [ ] **Step 4: Publish the first batch for the active pane**

After the final ordered item list is ready:
- assign pane/tab item state as today
- build and publish only the first batch rows
- record the first-batch timing point

Do not apply this staged publish path to inactive panes or cached tabs.

- [ ] **Step 5: Publish the remainder in one final completion step**

After the first batch is visible:
- build/publish the remaining rows as one final full update
- record the final completion timing point

The final publish must not reorder or replace already-visible first-batch rows with different content.

- [ ] **Step 6: Keep stale-load cancellation authoritative**

Ensure `load_generation` still guards:
- first-batch publish
- final publish

Stale results must be dropped at both points.

- [ ] **Step 7: Build after staged publication changes**

Run the build command.

Expected:
- build succeeds
- only known `MSB8027` / `LNK4042` warnings remain

- [ ] **Step 8: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml.cpp native/FileScout.Native/MainWindow.xaml.h
git commit -m "feat(perf): stage active pane row publication"
```

---

## Task 3: Preserve live user interaction correctness during staged loading

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp`
- Modify if needed: `native/FileScout.Native/MainWindow.xaml.h`

The UI is supposed to stay responsive after first batch appears. That means live user interaction must win over deferred restoration state.

- [ ] **Step 1: Inspect current selection restoration and tab-switching behavior**

Review how these currently interact with row publication:
- `pane.selection`
- `selected_item_index`
- `tab_switching`
- `SyncSelectionFromListView()`
- `RestoreSelectionFromState(...)`

- [ ] **Step 2: Add the minimum state needed to detect post-first-batch user interaction**

Track whether the user has interacted with the still-active pane after the first batch became visible, using the smallest practical state marker.

This marker only needs to protect:
- live selection/focus changes
- active row changes
- live scroll-position changes after first batch becomes visible

- [ ] **Step 3: Make deferred restoration conditional**

Implement the approved invariant:
- final publish may restore pending state only if the user has not already taken control after first batch
- once the user interacts, live state becomes authoritative

Do not let final publish clobber live user changes.

- [ ] **Step 4: Preserve ordering and selection correctness**

Ensure:
- first batch remains a prefix of final order
- final publish does not scramble sort/group order
- if the original target selection is outside the first batch, it is acceptable for it to appear only at final publish time

- [ ] **Step 5: Run the focused test suite**

Run:

```cmd
native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no
```

Expected:
- all existing tests pass

If small pure helpers were introduced and can be unit-tested cheaply, add targeted tests. Do not widen this task into a new UI automation suite.

- [ ] **Step 6: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml.cpp native/FileScout.Native/MainWindow.xaml.h native/FileScout.Tests
git commit -m "fix(perf): preserve live pane state during staged loads"
```

---

## Task 4: Benchmark, validate, and record durable state

**Files:**
- Modify: `docs/ai/status.md`
- Modify: `docs/ai/plan.md`

This task proves the performance slice with the agreed benchmark protocol and records the outcome durably in repo docs.

- [ ] **Step 1: Capture the post-change timings**

Run the same folder and conditions for `5` post-change measurements.

Record the same metrics and compute medians.

- [ ] **Step 2: Verify acceptance thresholds**

The slice passes only if:
- median time to first visible batch improves by at least `25%` or `150 ms`, whichever is larger
- median time to full completion does not regress by more than `10%`

If the first-batch target is missed, record that explicitly rather than overstating success.

- [ ] **Step 3: Run the full solution build**

Run:

```cmd
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"
```

Expected:
- exit code `0`
- only known `MSB8027` / `LNK4042` warnings

- [ ] **Step 4: Run the test suite**

Run:

```cmd
native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no
```

Expected:
- all tests pass

- [ ] **Step 5: Do a manual runtime validation pass**

Validate in the running app:
- a `1k-5k` item folder shows first rows earlier
- the pane remains responsive while the rest of the rows complete
- no stale-load flashes occur during rapid navigation
- no obvious selection/focus clobber happens if you interact after first batch appears
- no obvious live scroll-position clobber happens if you scroll after first batch appears
- final ordering remains stable

- [ ] **Step 6: Update durable docs**

Update `docs/ai/status.md` with:
- what changed in the phase-1 performance slice
- the benchmark folder and median before/after results
- build/test/manual validation outcome
- any residual bottlenecks still visible

Update `docs/ai/plan.md` with:
- the new current state
- whether the next step should be deeper row-creation work, shell enumeration optimization, or a broader virtualization investigation

- [ ] **Step 7: Commit**

```bash
git add docs/ai/status.md docs/ai/plan.md
git commit -m "docs: record first-paint performance benchmark results"
```

---

## Guardrails

- Do not widen phase 1 into `ShellInteropService` optimization unless the implementation is blocked without a tiny helper-only change.
- Do not introduce repeated chunked row appends in this slice.
- Do not optimize inactive panes, cached tabs, archive mode, or batch rename here.
- Do not regress selection, focus, scroll position, or sort/group stability in pursuit of first paint.
- Do not claim success without median before/after timings.

---

## Final Verification Checklist

- [ ] Build succeeds with only known `MSB8027` / `LNK4042` warnings
- [ ] `FileScout.Tests.exe` passes
- [ ] Median first-batch time meets the agreed improvement threshold
- [ ] Median full-completion time does not regress beyond the allowed threshold
- [ ] Manual runtime validation confirms early rows plus responsive interaction
- [ ] `docs/ai/status.md` and `docs/ai/plan.md` are updated with measured results
