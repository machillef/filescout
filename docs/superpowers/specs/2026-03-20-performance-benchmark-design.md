# FileScout Large-Folder First-Paint Performance — Design Spec

**Date:** 2026-03-20
**Scope:** Measured performance initiative focused on perceived load speed for `1k-5k` item folders. Improve first visible content and early interactivity before attempting broader scalability work.
**Baseline:** Existing WinUI 3 / C++/WinRT app in `native/FileScout.Native/`
**Target OS:** Windows 11 x64 (minimum, unpackaged)

---

## Summary of Decisions

| Area | Decision |
|---|---|
| Primary target | Large-folder load speed for `1k-5k` item folders |
| Optimization goal | Improve first paint and perceived responsiveness, not just total completion time |
| Approach | Measurement-first, staged load pipeline |
| First slice | Instrument the current load path, keep enumeration/sort unchanged, and split visible row publication into `first batch` plus `final publish` for the active pane only |
| First-batch rule | Publish the first `min(150, total_items)` rows once the full sorted/grouped item list is ready; publish the rest in one final follow-on step |
| Architecture posture | Reuse the current shell-backed load model and `load_generation` cancellation guard; do not change `ShellInteropService` behavior in phase 1 except if later measurements explicitly force a separate follow-up slice |
| Deep virtualization | Explicitly deferred unless measurements prove it is necessary |
| Non-goals | No unrelated feature work, no archive/batch-rename perf work, no broad rendering rewrite in this first slice |

---

## 1. Goal

This initiative is about **perceived folder-open performance** rather than generic optimization.

### Primary goal
- Make `1k-5k` item folders feel fast to open.
- The window should stay responsive while a folder loads.
- Initial rows should appear meaningfully sooner than they do today.
- The pane should stop feeling blank or frozen while the full load is still in progress.

### Success should be judged by user-visible behavior
- A loading transition begins immediately.
- A usable first batch of rows appears quickly.
- The user can visually orient in the folder before full completion.
- Remaining rows continue to arrive without stalling the app.

### Explicit non-goals for this initiative
- No worst-case `20k+` optimization target yet.
- No deep rendering/GPU investigation unless measurements force it.
- No feature expansion mixed into the slice.
- No broad rewrite of the item model, row visuals, or pane behavior beyond what first-paint performance requires.

---

## 2. Problem Framing

The current app already has one meaningful performance improvement in place:
- background directory enumeration
- per-pane `load_generation` cancellation for stale loads
- shell icon caching

That means the next performance win should not assume “everything is still synchronous.” The likely remaining problem is that the user still waits too long for **first visible content**, because too much work is completed before the first rows are shown.

The design target is therefore:
- measure the current load pipeline
- identify where the first paint is being delayed
- change the pipeline so visible results land earlier

This is a better first performance slice than jumping straight into another high-risk virtualization attempt.

### Phase 1 hard boundary
The first implementation slice is intentionally narrower than a general “optimize folder loading” effort:
- keep directory enumeration and item construction behavior unchanged
- keep sorting/grouping behavior unchanged
- do not change icon extraction strategy in this slice
- do not optimize inactive panes or cached tabs in this slice
- only split **visible row publication** for the active pane after the final sorted/grouped item list is already known

If the timings show enumeration or shell metadata extraction is the dominant cost, that becomes the next performance slice rather than being pulled into phase 1 opportunistically.

---

## 3. Likely Hotspots

Based on the current codebase, these are the most likely contributors to first-paint delay:

### A. Shell enumeration and metadata work
- directory enumeration may already be off the UI thread, but the total per-item shell/metadata work can still delay the first publish point
- phase 1 will measure this cost, not optimize it directly

### B. Icon extraction and icon-source shaping
- shell icon extraction is cached, but cache misses and icon-source creation may still be expensive during initial folder open
- phase 1 will measure this cost indirectly through the existing path, not redesign icon behavior

### C. Row materialization
- building the full WinUI row tree for all items before showing anything is a likely first-paint bottleneck

### D. Full-list publish behavior
- if the UI only updates after the entire folder result is ready, perceived performance will remain poor even if total load time is acceptable

### E. Extra work for non-visible state
- inactive pane/tab row rebuilding or secondary shaping work may be happening too early relative to what the user can actually see

### Optimization order
1. Measure the current phases.
2. Separate first-paint timing from full-completion timing.
3. Publish an initial visible batch as early as possible.
4. Defer non-critical work that is not required for first paint.
5. Reassess whether row creation, not enumeration, is the dominant cost before escalating to deeper architecture changes.

---

## 4. Recommended Approach

Three broad approaches were considered:

### 1. First-paint pipeline optimization
- Keep the current shell-backed architecture.
- Add timing around load phases.
- Render the first visible batch early.
- Continue loading the remainder in the background.

**Pros**
- Directly improves what the user feels.
- Lower risk than a broad refactor.
- Produces evidence about where the time is really going.

**Cons**
- May not fully solve very large worst-case folders.
- Requires careful staging so partial loads do not break selection or status behavior.

### 2. Faster full enumeration
- Focus on making the entire existing load pipeline cheaper end to end without changing publish behavior much.

**Pros**
- Simpler mental model.
- Could reduce total load time broadly.

**Cons**
- Might not improve perceived speed enough if the first publish point still happens late.

### 3. Aggressive virtualization/refactor
- Restructure data and row creation more deeply.

**Pros**
- Higher long-term upside.

**Cons**
- Higher risk.
- Recent history already shows one attempted virtualization path that had to be reverted.
- Too much architecture change for the first measured performance slice.

### Decision
Approach `1` is the right first move.

This initiative should optimize **time to first visible content** first, while also collecting enough timing evidence to decide whether a deeper refactor is justified later.

---

## 5. Implementation Shape

This should be executed as one measured first-paint slice.

### Phase 1 contract
- Keep the existing background enumeration and final sorted/grouped item list production path.
- Once that final ordered item list exists for the active pane, render only the first batch immediately.
- Define `first batch` as `min(150, total_items)` rows.
- After the first batch is visible, publish the remaining rows in **one** final follow-on step.
- Do **not** use chunked repeated appends in phase 1.
- Do **not** modify `ShellInteropService` in phase 1 unless the implementation cannot be instrumented without a tiny helper-only change; if deeper shell changes are needed, stop and re-scope.

### A. Add timing instrumentation
- Add lightweight timing around the active pane load pipeline.
- Capture at least:
  - folder load start
  - enumeration start/end
  - first visible batch publish time
  - full publish completion time
  - row materialization time if separable

### Canonical timing definition
- For this first slice, `time to first visible batch` is defined as the timestamp captured on the UI thread immediately after the active pane's first-batch row source has been assigned/populated for display.
- This is the required benchmark event for before/after comparison in phase 1.
- It is intentionally a first-paint proxy, not a GPU/frame-present measurement.
- `time to full completion` is defined as the timestamp captured on the UI thread immediately after the final remaining rows have been assigned/populated for the active pane.

### B. Split first-batch publish from full completion
- The active pane should no longer wait for all row UI elements to be built before painting visible content.
- Once the final ordered item list is ready, build and publish the first `min(150, total_items)` rows immediately.
- Publish the remaining rows afterward in one final completion step.
- The first slice is about splitting row materialization/publication, not redesigning enumeration.

### C. Preserve responsiveness and stale-load safety
- Keep the existing `load_generation` cancellation pattern.
- Partial publication must still be guarded so stale results cannot overwrite newer navigation.
- Status text may optionally note that loading is still in progress after first paint, but final completion remains the only required durable status update.

### D. Defer non-critical work where practical
- In phase 1, do not add extra metadata/icon staging logic unless it is required to support the first-batch row publish path cleanly.
- Do not widen this into a speculative rewrite of every row-building function.

### E. Limit the blast radius
- Keep the first slice focused on the visible active pane.
- Do not optimize unrelated flows such as archive browsing, batch rename, or offscreen tab restoration in this slice.
- Do not optimize inactive pane/tab rebuild work in this slice.

### Likely touched files
- `native/FileScout.Native/MainWindow.xaml.cpp`
- `native/FileScout.Native/MainWindow.xaml.h`

---

## 6. Metrics and Evidence

This initiative should not rely on “it feels faster” alone.

### Benchmark protocol
- Use one representative local folder in the `1k-5k` item range as the primary benchmark folder.
- Keep the same:
  - pane/view mode
  - sort/group mode
  - window size
  - app theme
- Capture `5` runs before and `5` runs after the implementation change.
- Report the median for:
  - time to first visible batch
  - time to full completion
- If practical, relaunch the app once before the `before` set and once before the `after` set so both sets start from a comparable process state.

### Required timing evidence
- Time to first visible batch
- Time to full completion
- A rough breakdown of the dominant phase(s) where possible

### Measurement intent
- Establish a baseline before the pipeline change
- Compare before/after in the same heavy folder scenarios
- Use the evidence to decide whether the next performance slice should target:
  - row creation
  - icon shaping
  - shell metadata extraction
  - deeper virtualization

### Acceptance threshold
- The slice passes if median **time to first visible batch** improves by at least `25%` or `150 ms`, whichever is larger, on the chosen benchmark folder.
- Median **time to full completion** must not regress by more than `10%`.
- If first-batch time fails to improve materially, the slice should not be called successful even if the code path is cleaner.

### Scope guard
- No external benchmark harness is required in this first slice unless the in-app timing proves too noisy or too incomplete

---

## 7. Risks and Guardrails

### Risks
- Partial publication can complicate selection restoration and row-state consistency.
- Appending rows incrementally may introduce visual churn if not staged carefully.
- Deferring too much detail work could make the first batch look obviously incomplete in a bad way.

### Guardrails
- Preserve current correctness over maximum aggressiveness.
- Do not regress navigation, selection, or status behavior in pursuit of first paint.
- Keep the UI polish intact; this is a performance slice, not a visual redesign.
- If measurements show the first bottleneck is elsewhere, stop and re-scope rather than forcing a preconceived implementation.

### Required correctness invariants
- The first batch must be a prefix of the **final sorted/grouped order**.
- The final publish must not reorder or replace already-visible first-batch rows with different content.
- Selection restoration remains authoritative only at final publish time.
- If a previously selected item is outside the first batch, it is acceptable for it not to appear selected until final completion.
- Stale-load cancellation must apply to both first-batch and final publish paths.
- The first-batch path must only apply to the visible active pane, never to inactive panes or cached tabs.
- Once the first batch is visible, any new user interaction on the still-active pane becomes authoritative over deferred restoration.
- That means final publish must preserve live user changes to selection, focus target, active row, and scroll position rather than clobbering them with pre-first-batch restoration state.

---

## 8. Success Criteria

### User-visible behavior
- [ ] A `1k-5k` item folder no longer feels blank or frozen while loading
- [ ] Initial rows appear sooner than in the baseline behavior
- [ ] The app remains responsive during folder load
- [ ] Remaining rows can continue loading after the first batch is already visible

### Engineering behavior
- [ ] Timing instrumentation exists for the active pane load phases
- [ ] Before/after timings can distinguish first-batch paint from full completion
- [ ] Existing stale-load cancellation remains correct
- [ ] No regression in pane selection or navigation correctness during staged loading

### Safety
- [ ] Solution build passes
- [ ] Existing tests pass
- [ ] No new warnings beyond the known exempt WinAppSDK warnings

---

## 9. Deferred Work

This initiative intentionally defers:
- deep UI virtualization
- worst-case `20k+` folder strategy
- generalized benchmark harnesses
- broader tab/pane/offscreen cache strategy redesign
- unrelated performance work for archive mode, batch rename, or other non-folder-load features

Those can become the next performance slice if the new timing evidence shows they are necessary.
