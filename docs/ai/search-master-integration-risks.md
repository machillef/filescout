# Risks: search-master-integration

## R1: `master` already contains partial R17 behavior, so forward-porting may duplicate logic

Probability: High
Impact: High

Mitigation:
- Treat `master` as the baseline and port only helper-level behavior that is missing or older.
- Prefer extraction and replacement of inline logic over wholesale file replacement.

## R2: `MainWindow` search flow has many cancellation entry points

Probability: High
Impact: High

Mitigation:
- Centralize search reset behavior as part of the integration work.
- Keep manual acceptance focused on `Escape`, navigation, pane switch, and tab switch paths.

## R3: `SearchService` contract changes may ripple into current UI handlers

Probability: Medium
Impact: High

Mitigation:
- Introduce callback-shape changes in a narrow service slice before broader UI rewiring.
- Cover the new callback contract with focused tests before touching the main search UI.

## R4: Search row presentation may diverge from the `FileRowViewModel` layout contract

Probability: Medium
Impact: Medium

Mitigation:
- Keep search presentation in pure helpers and adapt it to existing view-model factories.
- Do not reintroduce legacy search-row XAML objects or alternative item-source types.

## R5: Historical top-level docs can mislead implementation if treated as live source of truth

Probability: Medium
Impact: Medium

Mitigation:
- Keep all new planning in `search-master-integration-*` docs only.
- Do not modify top-level `docs/ai/plan.md`, `docs/ai/slices.md`, or `docs/ai/status.md` during
  bootstrap.

## R6: Manual-only behaviors may pass unit tests but still fail in WinUI host

Probability: Medium
Impact: Medium

Mitigation:
- Reserve a manual acceptance slice at the end.
- Explicitly verify search focus, progress visibility, and recursive-result activation in the real
  app after implementation.
