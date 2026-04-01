# Design: search-master-integration

## Part 1 - Requirements

### Problem Statement

Forward-port the completed fuzzy-search behavior from `feature/r17-fuzzy-search` commit `ea49541`
onto current `master` without regressing the latest UX architecture already present on `master`.

### Approved User Stories

1. As a FileScout user on current `master`, I want to start fuzzy search from the current search box,
   so that I keep the latest breadcrumb and address-bar UX.
2. As a FileScout user browsing a folder, I want typing in the search box to apply local fuzzy
   filtering first, so that nearby results appear immediately.
3. As a FileScout user with a local filter query active, I want Enter to escalate into recursive
   search for the current folder, so that I can expand the search scope without changing tools.
4. As a FileScout user running recursive search, I want results to stream into the active pane,
   so that the app feels responsive before the scan completes.
5. As a FileScout user running recursive search, I want visible progress and deterministic
   completion or cancellation behavior, so that the search never feels stuck.
6. As a FileScout user switching panes, tabs, or folders, I want search state to reset cleanly,
   so that results from an old context do not leak into the new one.
7. As a FileScout user selecting a recursive result, I want FileScout to open the parent folder and
   reselect the matching item, so that I land directly on the file I searched for.
8. As a maintainer, I want search presentation to flow through the current `FileRowViewModel`
   rendering path, so that search integration does not fork the row UI architecture again.
9. As a maintainer, I want search-service tests on `master` to cover cancellation, local-root
   constraints, depth handling, and result shaping, so that forward-port regressions are caught.

### Out Of Scope

- Reverting to the legacy shared-address-bar search UX
- Non-search redesign work in the latest `master` chrome
- Packaging or dependency-distribution changes
- Broader historical docs cleanup outside this initiative's file set

## Part 2 - Design

### Architecture Summary

The integration keeps `master`'s existing UI shell intact and ports the missing behavior as a small
set of search-focused modules:

1. `SearchFilter.h`
   A pure helper that converts a snapshot of `ShellItemRef` rows into a ranked top-200 filtered
   result set using `FuzzyScore`.

2. `SearchPresentation.h`
   A pure helper that centralizes search-specific display rules: progress text, completion text,
   relative-path subtitles, fuzzy highlight positions, and recursive-result open targets.

3. `SearchService`
   The existing worker remains the recursive engine, but its contract is hardened to match the
   forward-port behavior: safe generation ownership, periodic batch flushes, local-root-only
   execution, symlink/junction skipping, bounded depth, and deterministic completion on failure.

4. `MainWindow`
   `MainWindow` stays responsible for pane transitions and row publication, but it stops owning the
   search ranking and search-string formatting logic inline. It becomes an adapter that:
   - snapshots the current pane for filter mode
   - delegates ranking to `BuildFilterResults`
   - delegates status text and subtitles to `SearchPresentation`
   - keeps row publication on the current `FileRowViewModel` path

### Component Responsibilities

#### `SearchFilter`

- Input: `query`, `search_snapshot`, optional `max_results`
- Output: ordered pointers into the snapshot, preserving snapshot order for score ties
- Rules:
  - query length `< 2` returns the original snapshot order up to `max_results`
  - only filename matching is scored
  - no WinRT, UI, or threading dependencies

#### `SearchPresentation`

- Input: recursive `SearchResult` batches, search root, query string
- Output:
  - status text for in-progress and completion states
  - relative-path subtitles for nested results
  - highlight positions for filename emphasis
  - `SearchOpenTarget` for navigate-on-select
  - optional `ShellItemRef` conversion for recursive result rows
- Rules:
  - root-level recursive results show no subtitle
  - highlight computation follows fuzzy subsequence order
  - open-target construction is filesystem-only and side-effect free

#### `SearchService`

- Input: `root`, `query`, UI-thread post function, result callback, complete callback
- Output:
  - periodic ranked result batches
  - a single terminal completion callback carrying `cap_reached` and `failed`
- Rules:
  - local filesystem roots only
  - recursion skips symlinks and junction-like entries
  - files deeper than the agreed depth boundary are not traversed
  - stale generations and cancelled searches must never publish late UI updates

#### `MainWindow`

- Maintains `SearchMode`, per-pane snapshots, filtered items, recursive results, and progress-bar
  visibility
- Converts helper outputs into `FileRowViewModel` rows through the current row pipeline
- Owns transitions for:
  - `SearchTextBox_TextChanged`
  - `SearchTextBox_KeyDown`
  - `FileRowsListView_DoubleTapped`
  - navigation, pane switch, and tab switch cancellation paths

### Data Flow

#### Filter Mode

1. User focuses the existing `SearchTextBox`.
2. `SearchTextBox_TextChanged` enters `Filter` mode on the active pane and snapshots the current tab.
3. `BuildFilterResults` ranks the snapshot.
4. `MainWindow` publishes the filtered items through `MakeFileRowViewModel` and the existing
   row-state helpers.
5. The pane stays fully interactive; clearing the query restores the snapshot.

#### Recursive Mode

1. User presses Enter while a filter query of length `>= 2` is active.
2. `MainWindow` validates the root, locks the search box for the active recursive run, shows the
   per-pane progress bar, and starts `SearchService`.
3. `SearchService` batches ranked `SearchResult` vectors back to the UI thread.
4. `SearchPresentation` shapes status text and relative-path metadata.
5. `MainWindow` stores the latest `search_results`, converts them into current row models, and
   publishes them in the active pane.
6. Completion or cancellation hides the progress bar and returns the pane to a consistent state.

#### Result Activation

1. User double-clicks a recursive result row.
2. `TryBuildSearchOpenTarget` computes the parent folder and the selected target path.
3. `MainWindow::LoadDirectory` navigates to the parent folder using the existing master flow and
   reselects the target item.

### Error Handling

- Invalid or unsupported recursive roots do not start a background walk.
- Worker exceptions do not leave the pane stuck in recursive mode; completion is posted with
  `failed = true` so the UI can hide progress and show a failure-safe status.
- Cancelled or stale searches do not publish result batches or completion messages after the pane
  has changed context.
- Snapshot-based filtering ensures local filter mode is insulated from concurrent pane mutations.

### Testing Approach

- Pure-helper tests:
  - `SearchFilterTests` for ranking, tie order, match dropping, and result cap
  - `SearchPresentationTests` for status strings, subtitle shaping, highlight positions, and
    open-target construction
- Recursive engine tests:
  - keep `SearchServiceTests` focused on observable behavior: cancellation, cap handling, depth,
    failure completion, and local-root constraints
- Integration verification:
  - search-focused test filter after each slice
  - full native test suite and canonical build at the end
- Manual acceptance after implementation:
  - live filter in the current search box
  - Enter escalation to recursive search
  - progress bar visibility and completion text
  - Escape, pane switch, tab switch, and navigation cancellation
  - double-click navigate-on-select for recursive results

### Why This Design

This plan forward-ports the feature branch as behavior, not as file ownership. The pure helpers
make the ranking and presentation rules portable, the service hardening stays behind a small
interface, and `MainWindow` keeps control of current `master` UX transitions. That gives us a
viable merge path without discarding the newer shell architecture.
