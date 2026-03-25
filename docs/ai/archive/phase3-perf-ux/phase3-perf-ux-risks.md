# Risks: phase3-perf-ux

## R1: P3 scope too large to implement safely in one slice

**Probability:** High
**Impact:** High — merge conflicts, difficult to debug failures
**Mitigation:** Split P3 into P3a (type change + UpdateRows) and P3b (remove old helpers + fix
search/group paths) if first 2 hours of implementation haven't converged.

## R2: x:Bind code generation incompatible with FileRowViewModel

**Probability:** Medium
**Impact:** Medium — compile errors in XAML generated code
**Mitigation:** Ensure `FileRowViewModel.idl` is correctly included in project before P2 begins.
If `x:DataType` causes issues, fall back to `{Binding}` (reflection-based, slower but functional)
for P2, then investigate x:Bind issues as a separate step.

## R3: Group headers (non-ViewModel rows) break after P3

**Probability:** Medium
**Impact:** Medium — grouped view crashes or shows wrong items
**Mitigation:** Group headers are currently inserted as separate `IInspectable` rows. After P3,
they must remain compatible with the new `ObservableVector<FileRowViewModel>`. Options:
(a) use a discriminated union ViewModel; (b) keep a separate `IObservableVector<IInspectable>` for
group view only; (c) disable grouping temporarily and restore in P3b.

## R4: PCH invalidation on FileRowViewModel.h

**Probability:** Medium
**Impact:** Low (build error, easily fixed)
**Mitigation:** See `gotchas/winrt-pch-invalidation-on-header-change.md`. After adding
`FileRowViewModel.h` to project, do a full clean build for first compile.

## R5: IconSource BitmapSource creation fails on background thread

**Probability:** Low
**Impact:** Medium — icons silently not shown
**Mitigation:** `WriteableBitmap` and `SoftwareBitmapSource` must be created on the UI thread.
`BackfillIconsAsync` already dispatches to UI thread for row updates — ensure `BitmapSource`
creation also happens in the UI-thread lambda, not the background section.

## R6: Sidebar Expander control not available or styled incorrectly

**Probability:** Low
**Impact:** Low — can use custom header + StackPanel instead
**Mitigation:** `winrt::Microsoft::UI::Xaml::Controls::Expander` is available in WinUI 3 1.4+.
Confirm availability before S1; if unavailable, use a `Button` + `Visibility` toggle pattern.
