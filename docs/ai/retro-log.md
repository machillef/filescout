# FileScout Initiative Retrospectives

---

## large-dir-perf — 2026-03-25

### Metrics

| Metric | Value |
|---|---|
| Slices planned | 4 (L1–L4) |
| Slices completed | 4 |
| Slices blocked | 0 |
| Completion rate | 100% |
| Total commits | 4 (3 `perf`, 1 `refactor`) |
| Decisions made | 0 formal (no decisions.md) |
| Debugging escalations | 0 |
| Post-acceptance fixes | 0 |
| Design deviations | 2 (same root cause — `resume_foreground` incompatibility) |
| Learnings extracted | 2 new gotchas added to execution-loop |

### Key Learnings

- **`winrt::resume_foreground` is UWP-only.** WinUI 3 uses `Microsoft::UI::Dispatching::DispatcherQueue`, which has no `operator co_await` in Windows App SDK 1.8. Design docs that call for `co_await resume_foreground(m_dispatcherQueue)` must use `TryEnqueue` instead. This applied to both L1 and L3 and was already saved to project memory.
- **`IReplaceAll<T>` does not exist.** The correct QI target is `IVector<T>` and `ReplaceAll` is a method on it. The slices.md design doc referenced the wrong interface name. Saved to execution-loop gotchas (L2).
- **`std::min` / `std::max` break under `<windows.h>` macros.** Use `(std::min)(a, b)` to suppress macro expansion. Saved to execution-loop gotchas (L3).
- **PCH rebuild required after adding headers.** Adding `#include <unordered_map>` to `pch.h` caused a C1076 "compiler limit: internal heap limit reached" error on incremental build. A full `/t:Rebuild` cleared it. This is a MSBuild/PCH artefact, not a code error.

### What Went Well

- All 4 slices completed on first attempt — zero debugging escalations or post-acceptance fixes.
- The `TryEnqueue`-based approach (L1 deviation) was validated immediately and reused correctly in L3 without re-discovering the issue.
- `std::shared_ptr` for cross-chunk item sharing in L3 was clean and safe — no lifetime bugs.
- L2 benchmark test (`LargeDirectoryEnumerationBenchmark`) provided a concrete regression gate that served the test suite beyond this initiative.
- Narrow scope discipline held throughout: archive path (`IsArchivePath`) untouched in L1 per spec; `group_by_sort_column` and `rows.Size() != total` guards preserved in L3 without friction.

### What Could Improve

- **Design docs contained two incorrect API references** (`resume_foreground`, `IReplaceAll<T>`). Future slices.md files for WinUI 3 work should have a reviewer pass for WinUI 3 API accuracy before implementation begins.
- **L4 was "Not Started" when `/retro` was invoked** (it was trivial but still incomplete). A better retro trigger is: all slices in status.md showing Complete, not just the last substantive slice. The execution-loop End of Plan block should check for this explicitly.
- **Manual validation deferred entirely.** All three substantive slices left "manual navigate to System32" unverified. The Definition of Done criteria 1–3 and 5 require a running app — future initiatives with runtime-only verification should note this as a hard prerequisite for DoD signoff, not a footnote.

### Skill Improvements Made

- `gotchas/winrt-ireplaceall-does-not-exist.md` — created during L2
- `gotchas/windows-min-max-macro.md` — created during L3

### Recommendations for Next Initiative

- When designing slices for WinUI 3: verify all API calls against `winrt/Microsoft.UI.Dispatching.h` and Windows App SDK 1.8 headers before writing slice definitions; do not copy UWP patterns verbatim.
- For initiatives where the Definition of Done requires running the app manually: schedule a manual validation session as a dedicated slice or pre-commit gate, not an optional post-completion step.
- `(std::min)` / `(std::max)` should be used consistently in all new Windows code in this project (or add `NOMINMAX` to pch.h permanently).

---

## phase3-perf-ux — 2026-03-25

### Metrics

| Metric | Value |
|---|---|
| Slices planned | 16 (P1–P4, U1–U8, S1–S4) |
| Slices completed | 16 |
| Slices blocked | 0 |
| Completion rate | 100% |
| Total commits | 16 (12 source + 4 docs-only) |
| Decisions made | 4 formal (decisions.md) |
| Debugging escalations | 0 |
| Post-acceptance fixes | 4 (S4 context menu; U1 breadcrumbs ×2; S3 ThisPC expand) |
| Slices requiring fixes | 3 (S4, U1, S3) |
| Zero-code-change slices | 6 (P4, U2, U3, U4, U6, U7, U8 — 44% of U-track) |
| Tests: start → end | 82 → 110 (+28 new, all GREEN) |
| Learnings extracted | 2 new gotchas (see below) |

### What Went Well

- **P3 (highest-risk slice) first-attempt pass.** Replacing 500 lines of programmatic
  row creation with DataTemplate-backed MVVM had a rollback plan, but needed zero
  iterations. Careful P1/P2 staging paid off — by the time P3 ran, the IDL, ViewModel,
  and templates were fully in place.

- **P4 was a no-op.** BackfillIconsAsync was already rewritten in P3 (INPC SetAt
  pattern emerged naturally from P3's redesign). Well-designed upstream work making
  a downstream slice zero-effort is a green flag.

- **44% of U-track slices required no new code.** U2, U3, U4, U6, U7, U8 all had
  existing implementations. Pre-flight investigation before coding is vindicated:
  read the codebase before writing. These slices closed in minutes.

- **Decision 4 (P-track before U/S) was correct.** P3's ListView/DataTemplate refactor
  automatically resolved U6 (scroll-jump on right-click), U7 (column overlap), and U8
  (arrow key passthrough) without additional work. Architectural changes have leverage.

- **Sidebar S-track built cleanly slice-by-slice.** S1 (structure) → S2 (recents) →
  S3 (drives) → S4 (bookmarks) with no cross-slice regressions. The Expander/Panel
  pattern held up across all four slices.

### What Went Wrong

- **S4 context menu anchoring.** `ShowAt(FrameworkElement)` anchors to the element's
  layout origin (0, 0 relative to the window), not the pointer position. Required a
  post-acceptance fix. Step 9 re-assessment missed it because there was no manual
  test trigger during verification. **This is a recurring WinUI 3 gotcha.**

- **U1 breadcrumb sizing took 3 passes.** Each pass applied a conservative 20% increase
  (12→14→16→18px). The incremental approach wasted 2 extra fix cycles. When a visual
  size change is reported as too small, increase by 40–50% rather than 15–20%.

- **TextBox Escape swallowed by WinUI 3 internal processing.** `KeyDown` on TextBox
  fires after the control's own Escape handling (undo-buffer cancel), so Escape never
  reached our handler. Required switching to `PreviewKeyDown` + adding `LostFocus` for
  click-outside dismissal. Neither was obvious from the WinUI 3 docs. **New gotcha.**

- **Default-collapsed Expander conflicted with user expectation.** S3's `ThisPCExpander`
  was designed collapsed (consistent-collapse policy), but the user wanted drives visible
  on launch. Design decisions about initial visibility should be validated in the slice
  description or immediately after first launch.

- **"docs: mark slice as Needs Fix" commit is still happening.** The previous sprint
  retro flagged this as noise. It happened again here (7a76f9e). Consider collapsing
  the "Needs Fix" doc update into the fix commit.

### Skill Improvements Made

- **New gotcha: WinUI 3 TextBox consumes Escape in KeyDown.** Use `PreviewKeyDown`
  (not `KeyDown`) to intercept Escape/navigation keys on WinUI 3 TextBox controls.
  Add `LostFocus` for click-outside-to-dismiss. Saved to:
  `~/.claude/plugins/marketplaces/claude-code-bootstrap-commands/skills/execution-loop/gotchas/winui3-textbox-keyboard-event-routing.md`

- **New gotcha: WinUI 3 ShowAt() without FlyoutShowOptions anchors to layout origin.**
  Always pass `FlyoutShowOptions` with `Position(args.GetPosition(element))` +
  `ShowMode(Transient)` for pointer-triggered flyouts. Saved to:
  `~/.claude/plugins/marketplaces/claude-code-bootstrap-commands/skills/execution-loop/gotchas/winui3-flyout-showat-position.md`

### Recommendations for Next Initiative

- **Visual sizing: go bold on first pass.** If a breadcrumb reads "too small", double
  the delta — not 15%. One overshooting pass that gets dialed back is better than
  3 conservative undershoot iterations.

- **WinUI 3 pointer events: always check FlyoutShowOptions.** Any new context menu or
  flyout triggered by a pointer event must use `ShowAt(element, options)` with
  `Position(args.GetPosition(element))`.

- **Add "initial visibility" as a validation criterion for Expander-based UI.** Any
  slice that sets `IsExpanded` should include "Confirm user expectation: expanded or
  collapsed?" in the manual-test checklist.

- **Performance sprint for large-directory load speed.** System32 performance is
  better after P3 but not near-instant. A dedicated perf investigation sprint on
  directory enumeration, icon loading heuristics, and batch-publish thresholds is
  the right next step.

---

## ui-ux-polish — 2026-03-22

### Metrics

| Metric | Value |
|---|---|
| Slices planned | 4 (U1–U4) |
| Slices code-complete | 3 (U1, U2, U3) |
| Slices not started | 1 (U4) |
| Slices blocked | 0 |
| Completion rate (code) | 75% |
| Total commits (native/) | 1 bulk (`165978f`) — U1/U2/U3 bundled with R16/perf/R12 |
| Decisions made | 0 formal (all implicit) |
| Debugging escalations | 0 |
| Learnings extracted | 1 (`winrt-pch-invalidation-on-header-change.md`) |

### Key Learnings

- **Visual slices can't self-close.** U1, U2, and U3 are all stuck at "In Progress"
  because agentic sessions cannot validate WinUI visual output — manual host launch
  is the only path to "Complete". These slices accumulate as validation debt.
  For future visual-heavy initiatives, schedule explicit host-validation sessions
  rather than leaving them open-ended.

- **PCH gotcha captured during U3.** Touching `ExplorerCommand.h` (a widely-included
  domain header that flows through `pch.h`) with an incremental build caused `C1853`.
  Documented in `gotchas/winrt-pch-invalidation-on-header-change.md`. Applies to any
  domain header change, not just this one.

- **Bulk commits obscure initiative boundaries.** U1/U2/U3 were committed in a single
  `165978f` commit alongside R16, perf, and R12 work. Makes `git bisect` and
  git-blame harder. Future initiatives should commit per-slice immediately after
  verification, not batch at the end of a development period.

- **TDD exception is valid for pure XAML.** U1 and U2 had no testable logic — only
  XAML layout and style changes. Skipping TDD was correct and kept slices narrow.
  U3 correctly added 3 unit tests for the one testable behavior (`is_destructive`).

- **StatusIconTextBlock intentionally deferred.** The XAML element was created in U3
  but no call sites were wired. This is an explicit deferral (documented), not
  missed work — the pattern of "structure now, wire later" is appropriate when the
  follow-up slice is already planned.

### Skill Improvements Made

- `gotchas/winrt-pch-invalidation-on-header-change.md` added to execution-loop
  gotchas during U3 implementation.

### Recommendations for Next Initiative (U4 + R17)

- **Close U1–U3 before starting U4.** Run a combined host visual pass. If any slice
  needs tweaks, fix and recommit before U4 scope is opened. Don't let validation
  debt span multiple slices.
- **Commit per slice, not per session.** One atomic commit per slice makes the
  history navigable and retro metrics accurate.
- **R17 visual slices (R17c–R17e) will have the same manual-validation gap.** Plan
  for explicit host-validation sessions as first-class milestones in the R17 plan,
  not as afterthoughts in "What remains unverified."
- **Consider a `pending-manual-validation` status** distinct from `In Progress` in
  future status files — "In Progress" implies active work, but these slices are
  actually waiting for a human to launch the app.

---

## UX+Perf Sprint Round 2 — 2026-03-24

### Metrics

| Metric | Value |
|---|---|
| Slices planned | 7 (S1–S7) |
| Slices completed | 7 |
| Slices blocked | 0 |
| Completion rate | 100% |
| Total commits | 15 (10 source + 5 docs-only) |
| Decisions made | 0 formal |
| Debugging escalations | 0 |
| Post-acceptance fixes | 3 (S4×2 rounds, S6×1 round) |
| Slices requiring fixes | 2 (S4, S6) |
| Learnings extracted | 0 new skill files (patterns captured in retro) |

### Key Learnings

- **Perf slices: analyse the full hot path before coding.** S6 fixed `FindFirstFileW`
  per-spec, but `FormatShellDateTime` (8 NLS/timezone calls per item) became the residual
  bottleneck and needed a follow-up commit. Pattern: after identifying the target bottleneck
  in a perf slice, scan the remainder of the hot path before closing scope — especially
  any formatting or locale call that runs per-item.

- **Visual effects need runtime validation before first commit.** S4 went through 3 source
  commits because border-flash behaviour (thickness, brush opacity, timer interaction) can't
  be evaluated in headless tests. The final fix removed the flash entirely. Future slices
  that introduce visual animations or effects should be flagged as requiring a host-launch
  validation step _before_ the slice is accepted, not after.

- **"Delete the feature" is a clean resolution when visual calibration fails.** S4 ended by
  removing the border flash: 3 commits, net code reduction. When a visual effect is
  consistently rejected in manual testing, removing it and keeping the non-visual fallback
  (status bar text + scroll) is better than another iteration. This is a valid slice outcome.

- **docs-only "Needs Fix" commits add noise.** 5 of 15 sprint commits were docs-only
  (`docs: mark S4 as Needs Fix`, `docs: mark S6 as Needs Fix`). These are useful state
  signals but inflate the commit count. Alternative: fold the "Needs Fix" docs update into
  the fix commit's message body rather than pre-empting it with a separate commit.

- **Generation-guard + grouped-view skip is the correct backfill pattern.** S7's
  `BackfillIconsAsync` established: (a) single `tab_switching` wrap around entire `SetAt`
  loop (not per-item), (b) `load_generation` check to drop stale backfills, (c) skip when
  `group_by_sort_column` is true (row count ≠ item count in grouped views). Reuse this
  pattern for any future deferred row-update operation.

- **S7 was the cleanest slice of the sprint.** Complex coroutine + dispatcher pattern,
  first-attempt pass, no fixes. Root cause: the spec was precise about guards
  and the implementation closely followed it.

### Skill Improvements Made

- None (no new gotcha files created — patterns captured here for future skill extraction)

### Recommendations for Next Initiative

- **Gate perf slices on full hot-path audit.** Before accepting a perf slice as spec-ready,
  list _all_ expensive operations in the path and confirm the slice addresses the dominant
  one(s). Use TraceLog output from a real run as evidence.

- **Mark visual-effect slices as "manual-validation required" in slices.md.** Add a
  `Validation: manual host launch required before accept` note to any slice that involves
  animations, brush changes, or thickness transitions.

- **Collapse "Needs Fix" into fix commits.** Instead of `docs: mark S4 as Needs Fix` +
  `fix(ux): ...`, do a single `fix(ux): ... (was: Needs Fix — <reason>)`. Keeps history
  navigable without the noise.

- **Next validation milestone.** All S1–S7 runtime behaviours are unverified. Schedule a
  manual host-launch session to close S1–S7's "What remains unverified" lists before
  starting the next initiative.
