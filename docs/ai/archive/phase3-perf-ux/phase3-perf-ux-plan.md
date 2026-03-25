# Plan: phase3-perf-ux

## Objective

Fix the root cause of slow large-directory loading (XAML object creation without virtualization),
resolve 8 critical UX regressions, and redesign the sidebar with collapsible sections.

## Approach

Three sequential tracks:

1. **P-track** (P1–P4): MVVM architecture change — `FileRowViewModel` + XAML DataTemplates + ListView
   container virtualization. This is the foundation; all other tracks build on it.

2. **U-track** (U1–U8): Targeted UX fixes. Most are small (1-3 file changes). Run after P-track
   stabilizes so they're not redone during the row-creation refactor.

3. **S-track** (S1–S4): Sidebar redesign. Runs last as it's cosmetic/structural and independent
   of P-track's architectural changes.

## P-Track Detail

### P1 — FileRowViewModel
New WinRT runtime class. Defines the data contract for all row types.
Implement INPC, all display properties, `IconSource` get/set with `PropertyChanged`.

### P2 — DataTemplates
Pure XAML slice. Define `CardRowTemplate` and `ListRowTemplate` as `DataTemplate` resources with
`x:DataType="local:FileRowViewModel"`. Set `ItemTemplate` on both pane `ListView` instances.
No C++ changes.

### P3 — Wire ListView (split if needed)
The large surgical slice:
- Change `PaneContext.tab_rows` from `IObservableVector<IInspectable>` to
  `IObservableVector<FileRowViewModel>` (or keep as IInspectable but only store ViewModels)
- Update `UpdateRows()` to create ViewModels instead of XAML objects
- Update `RebuildAllTabRowsForPane()` accordingly
- Delete `CreateCardRow()`, `CreateListRow()`, `CreateRowView()`
- Fix `CreateSearchRowView()` path (search results may need a separate VM or adapter)
- Fix group-header path (group separators are not `FileRowViewModel` — handle carefully)

If P3 proves too large, split:
- **P3a**: Change vector type + update `UpdateRows` + confirm basic navigation works
- **P3b**: Remove old helpers + fix search path + fix grouped view

### P4 — Icon Backfill Rewrite
Update `BackfillIconsAsync`:
- Replace: `rows.SetAt(i, CreateRowView(items[i], listMode))`
- With: `auto vm = rows.GetAt(i).as<FileRowViewModel>(); vm.IconSource(bitmapSource);`
- `PropertyChanged` propagation via INPC handles the Image refresh automatically

## Execution Notes

- P3 is the highest-risk slice. Run full test suite after each sub-step.
- Group headers in grouped view are currently stored as separate rows in the same vector.
  After P3, they need a compatible representation. Preferred: keep group headers as a different
  type stored as `IInspectable` (cast check at render time), or temporarily disable grouping.
- `CreateSearchRowView` produces a different row layout. After P3, the search path needs its own
  lightweight ViewModel or can reuse `FileRowViewModel` with path as `TypeLabel`.
