# Requirements: search-master-integration

## Problem Statement

Forward-port the completed fuzzy-search behavior from `feature/r17-fuzzy-search` commit `ea49541`
onto current `master` without regressing the latest UX architecture already present on `master`.

## Decision Branches

1. Search entry model
   Recommendation: keep the current dedicated `SearchTextBox` and breadcrumb/address interactions on
   `master`; do not restore the older shared-address-bar search model from the feature branch.

2. Integration strategy
   Recommendation: port only behavior and helpers that are missing or older on `master`
   (`SearchFilter`, `SearchPresentation`, and any `SearchService` hardening), not entire files.

3. Search rendering contract
   Recommendation: keep all visible search rows on the current `FileRowViewModel` pipeline and adapt
   helper output to that model rather than reintroducing legacy row-building code.

4. Recursive search scope
   Recommendation: keep recursive fuzzy search limited to local filesystem roots and preserve the
   bounded-depth and cancellation guarantees from the feature branch.

5. Cancellation and state reset
   Recommendation: search must cancel cleanly on `Escape`, navigation, pane switch, and tab switch,
   and the active pane must return to normal non-search state without stale results or read-only UI.

6. Result activation
   Recommendation: recursive-result activation should navigate to the parent folder and reselect the
   matching item using the current `master` navigation flow.

7. Documentation strategy
   Recommendation: track this work as a new integration initiative instead of rewriting the
   historical top-level `R17` docs during bootstrap.

## User Stories

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

## Out Of Scope

- Reverting to the legacy shared-address-bar search UX
- Non-search redesign work in the latest `master` chrome
- Packaging or dependency-distribution changes
- Broader historical docs cleanup outside this initiative's file set

## Approval Checkpoint

If these requirements are approved, the next step is Part 2: write the design doc and then create
the full large-initiative doc set (`design`, `contracts`, `risks`, `plan`, `slices`, `status`,
`decisions`, and `architecture-discovery`) for implementation on `master`.
