# Plan: search-master-integration

## Objective

Forward-port the missing fuzzy-search behavior from `feature/r17-fuzzy-search` commit `ea49541`
onto current `master` while preserving `master`'s newer search UX shell.

## Approach

This initiative runs in four sequential phases:

1. Helper extraction
   Add the pure helper modules that are absent on `master` and cover them with focused tests.

2. Service hardening
   Reconcile `SearchService` with the safer behavior from the feature branch so recursive search has
   a stable contract before the UI consumes it.

3. `MainWindow` integration
   Replace inline ranking and status formatting with helper calls, then align recursive-mode row
   publication and result activation with the current `master` event flow.

4. Manual acceptance and closeout
   Verify the real WinUI host behavior and then reconcile the initiative status docs.

## Phase Detail

### Phase 1 - Helper extraction

- Add `Search/SearchFilter.h`
- Add `Search/SearchPresentation.h`
- Add `SearchFilterTests.cpp`
- Add `SearchPresentationTests.cpp`
- Keep `MainWindow` behavior unchanged except for the minimum glue needed to compile helpers in later slices

### Phase 2 - Service hardening

- Update `SearchService` generation ownership so detached work does not rely on raw `this` state
- Add deterministic completion on failure
- Enforce local-root-only recursive starts
- Keep the batch publication and depth/cap behavior under test

### Phase 3 - UI integration

- Replace inline filter ranking in `SearchTextBox_TextChanged` with `BuildFilterResults`
- Replace inline progress/completion strings with `SearchPresentation` helpers
- Replace ad hoc recursive-result subtitle logic with `BuildSearchResultItems` and presentation helpers
- Use `TryBuildSearchOpenTarget` for recursive-result activation
- Keep row publication on `FileRowViewModel`

### Phase 4 - Manual acceptance and docs

- Run the canonical build and full test suite
- Launch the app and verify the current `master` UX plus the integrated search behavior
- Update initiative status to complete only after the live host pass succeeds

## First Safe Slice

`SMI1 - Add SearchFilter and SearchPresentation helpers to master`

Why first:
- Small blast radius
- Pure logic
- Proves the branch can absorb the feature-branch search behavior without touching the largest UI file first

Validation:
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=SearchFilter*:*SearchPresentation*`

## TDD

Mandatory from Slice 1 for all behavioral changes.
