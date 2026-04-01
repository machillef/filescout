# Decisions: search-master-integration

## Decision 1: Treat this as a new integration initiative

Date: 2026-03-29

Decision:
Bootstrap a new `search-master-integration` initiative instead of trying to continue the historical
`R17` docs on `master`.

Rationale:
`master` has diverged materially from the branch that produced `ea49541`. Reusing the old doc set
would hide the real integration work and create false confidence about merge safety.

## Decision 2: Preserve `master`'s dedicated search UX

Date: 2026-03-29

Decision:
Keep the current `SearchTextBox`, breadcrumb, and address-shell model on `master`.

Rationale:
This is the current UX direction and already exists in code. Reverting to the feature branch's
older shared-address-bar flow would be a regression, not an integration.

## Decision 3: Port behavior, not ownership

Date: 2026-03-29

Decision:
Port the missing helper modules and hardened behavior from `ea49541`, but do not replace
`MainWindow` or other files wholesale.

Rationale:
Whole-file replacement is what caused the merge collapse. The correct unit of integration is the
search behavior that is missing on `master`.

## Decision 4: Keep search rows on `FileRowViewModel`

Date: 2026-03-29

Decision:
All search results remain on the current `FileRowViewModel` row pipeline.

Rationale:
`master` has already standardized on this model. Introducing a second row-rendering path would
recreate the architectural split that later work just removed.

## Decision 5: Start with pure helpers

Date: 2026-03-29

Decision:
Make `SMI1` a pure-helper slice before touching the larger `MainWindow` integration.

Rationale:
It is the safest proof that the feature-branch search behavior can be absorbed on `master` with a
small blast radius and direct test coverage.

## Decision 6: Keep recursive icon hydration off the UI thread

Date: 2026-03-29

Decision:
Recursive search results should publish rows first and backfill file icons asynchronously with a
search-specific generation guard, instead of resolving icons synchronously in the UI callback.

Rationale:
The normal directory path already uses chunked background icon extraction to keep WinUI responsive.
Recursive search needs the same discipline or a result batch can stall the window long enough to
surface `Not Responding`.
