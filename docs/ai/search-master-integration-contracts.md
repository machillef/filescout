# Contracts: search-master-integration

## `SearchFilter::BuildFilterResults`

```cpp
std::vector<::FileScoutNative::Domain::ShellItemRef const*> BuildFilterResults(
    std::wstring_view query,
    std::span<::FileScoutNative::Domain::ShellItemRef const> snapshot,
    std::size_t max_results = 200);
```

Contract:
- Returns pointers into `snapshot`; caller owns snapshot lifetime.
- Query length `< 2` returns the snapshot order truncated to `max_results`.
- Query length `>= 2` returns only fuzzy matches ordered by descending score.
- Equal scores keep snapshot order.
- Pure helper; no WinRT, COM, or filesystem access.

Consumers:
- `MainWindow::SearchTextBox_TextChanged`

## `SearchPresentation`

Expected helpers:

```cpp
std::wstring BuildSearchProgressStatus(std::size_t count);
std::wstring BuildSearchCompleteStatus(
    std::size_t count,
    std::filesystem::path const& root,
    bool cap_reached);
std::wstring BuildSearchResultSubtitle(
    std::filesystem::path const& result_path,
    std::filesystem::path const& root);
std::vector<std::size_t> FindSearchHighlightPositions(
    std::wstring_view query,
    std::wstring_view filename);
std::optional<SearchOpenTarget> TryBuildSearchOpenTarget(
    std::filesystem::path const& result_path);
std::vector<::FileScoutNative::Domain::ShellItemRef> BuildSearchResultItems(
    std::span<SearchResult const> results,
    std::filesystem::path const& root);
```

Contract:
- Presentation helpers are deterministic and side-effect free.
- Relative-path subtitles return empty for root-level results.
- Highlight positions return the matched filename indices in fuzzy-subsequence order.
- `TryBuildSearchOpenTarget` returns `nullopt` for empty or parentless paths.

Consumers:
- `MainWindow::SearchTextBox_KeyDown`
- `MainWindow::FileRowsListView_DoubleTapped`
- Search-row view-model creation helpers

## `SearchService`

Target callback contract:

```cpp
using ResultCallback = std::function<void(std::vector<SearchResult>)>;
using CompleteCallback = std::function<void(bool cap_reached, bool failed)>;
using PostFn = std::function<void(std::function<void()>)>;
```

Behavioral contract:
- `Start` cancels any prior run and begins a new generation.
- `Cancel` is non-blocking and prevents further publications from the cancelled generation.
- Result batches are posted on the caller-supplied `PostFn`.
- Completion is posted at most once per successful generation.
- Unsupported roots, worker failure, or unexpected exceptions must still terminate the active UI
  state cleanly through the completion callback.

Search constraints:
- Maximum result heap: 200
- Depth bound: 12 recursive levels
- Filename-only fuzzy scoring
- Skip symlink and junction recursion
- Do not search non-local roots

Consumers:
- `MainWindow` per-pane recursive search flow
- `SearchServiceTests`

## `MainWindow` Search State

Current `PaneContext` fields remain the source of truth:

```cpp
SearchMode search_mode;
std::wstring search_query;
std::vector<ShellItemRef> search_snapshot;
std::vector<ShellItemRef> filtered_items;
std::vector<SearchResult> search_results;
SearchService search_service;
```

Contract:
- `search_snapshot` is captured once when entering filter mode and is used for restore.
- `filtered_items` stays aligned with the currently displayed filter rows.
- `search_results` stays aligned with the currently displayed recursive rows.
- Any path that leaves the active search context must clear or replace these vectors before the
  pane becomes interactive again.

## Row Rendering Contract

`master` stays on the `FileRowViewModel` row pipeline.

Contract:
- Search integration does not restore legacy XAML row creation.
- Filter-mode rows are built from `ShellItemRef` snapshots and published through existing
  `FileRowViewModel` helpers.
- Recursive-mode rows may use a search-specific view-model factory, but they must still return
  `FileRowViewModel` instances consumable by the current `ListView`.
