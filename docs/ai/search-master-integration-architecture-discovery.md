# Architecture Discovery: search-master-integration

## Discovery Method

Validated directly against current `master` plus targeted reads from `feature/r17-fuzzy-search`
commit `ea49541`. No delegated sub-agent was used in this session.

## Current `master` Search Architecture

### UI shell

- `MainWindow.xaml` already exposes a dedicated `SearchTextBox` separate from the breadcrumb and
  address overlay.
- Per-pane search progress bars already exist in XAML for left and right panes.
- Search is part of the broader current `master` chrome, which also includes the command palette,
  dual-pane controls, and theme UI.

### Per-pane state

`PaneContext` already contains:
- `search_mode`
- `search_query`
- `search_snapshot`
- `filtered_items`
- `search_results`
- `search_service`

This means the branch already has the right state shape for the integration.

### Current search flow on `master`

- `SearchTextBox_TextChanged` performs local filter ranking inline using `FuzzyScore`.
- `SearchTextBox_KeyDown` already escalates Enter from `Filter` to `Recursive`.
- Recursive search already uses `SearchService` and existing per-pane progress bars.
- `FileRowsListView_DoubleTapped` already special-cases recursive results and navigates to the
  parent folder.

## Gaps Versus `ea49541`

### Missing pure helpers

`master` does not currently contain:
- `Search/SearchFilter.h`
- `Search/SearchPresentation.h`

These helpers in `ea49541` carry behavior that is currently duplicated inline on `master`.

### Older `SearchService` contract

Current `master` `SearchService` is older in several ways:
- complete callback carries only `cap_reached`
- generation ownership still reads member state from detached-thread-adjacent code
- no explicit worker-failure completion path
- recursion and flush logic are not yet aligned with the hardened feature-branch version

### Older tests

Current `master` search tests are behind the feature branch:
- no `SearchFilterTests`
- no `SearchPresentationTests`
- `SearchServiceTests` still reflect the older callback shape and narrower behavior contract

## Implications For Implementation

1. The merge problem is architectural drift, not missing search state.
2. The best integration seam is the pure helper layer first, then service hardening, then UI glue.
3. `MainWindow` is not the first file to change; it is the last major integration file once helper
   contracts are stable.
4. Historical top-level `docs/ai` files should not be treated as the active source of truth for this
   work because they do not describe the actual forward-port effort.
