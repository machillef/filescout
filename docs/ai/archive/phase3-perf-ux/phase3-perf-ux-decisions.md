# Decisions: phase3-perf-ux

## Decision 1: MVVM DataTemplate approach for performance fix

**Date:** 2026-03-24
**Decision:** Use XAML DataTemplates + `FileRowViewModel` (C++/WinRT INPC) as the performance fix.
**Alternatives considered:**
- Incremental: reduce XAML object count per row (still creates ~40k objects)
- VirtualizingStackPanel explicit config (already default in WinUI 3 ListView — can't help when rows are pre-built)
- ISupportIncrementalLoading (complex, unclear benefit over DataTemplate approach)
**Rationale:** DataTemplates are the canonical WinUI 3 pattern. ListView container recycling reduces
XAML object count from ~50k to ~500 for any directory size. This is the correct architectural fix
for the root cause, not an optimization of a broken approach.

## Decision 2: UX scope — critical bugs only (not all 24 issues)

**Date:** 2026-03-24
**Decision:** Address 8 critical UX regressions in this initiative; defer 16 non-critical issues.
**Rationale:** User confirmed "critical only" scope. Prevents the initiative from becoming
unmanageable. Non-critical issues can be addressed in a future UI polish initiative.

## Decision 3: Include sidebar redesign

**Date:** 2026-03-24
**Decision:** Include sidebar redesign (Quick Access, This PC, Bookmarks) in this initiative.
**Rationale:** User confirmed this as part of the scope. The sidebar is a significant visual gap
vs. modern file managers and affects first impressions.

## Decision 4: P-track executes before U-track and S-track

**Date:** 2026-03-24
**Decision:** Complete P1–P4 before starting U1–U8 or S1–S4.
**Rationale:** U-track UX changes that affect row layout (U7, U8) could conflict with P3's row
creation overhaul. Running P-track first ensures all subsequent slices build on a stable VM-based
row architecture.
