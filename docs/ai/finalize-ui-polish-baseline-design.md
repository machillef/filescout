# Design: finalize-ui-polish-baseline

> **Initiative:** finalize-ui-polish-baseline
> **Status:** Approved
> **Last updated:** 2026-03-28

---

## Part 1 — Requirements

### Goal

Turn the dirty `ui-polish` implementation batch into a clean, hostile-audited baseline that can
be merged to `master` before any private-beta UX hardening starts.

### User Stories

1. As a maintainer, I want the current `ui-polish` batch audited before merge, so beta-prep does
   not stack on top of unverified local state.
2. As a maintainer, I want completed `ui-polish` docs archived after verification, so `docs/ai/`
   reflects active work rather than stale active state.
3. As the next initiative, I want the `next-phase-design-inputs` document checked into git, so
   `private-beta-ux-hardening` has a durable seed instead of a local-only note.

### Out of Scope

- Reopening the `ui-polish` feature scope for optional redesign
- Starting `private-beta-ux-hardening`
- Packaging or private-beta install work

---

## Part 2 — Design

### Audit Strategy

Treat the current workspace as Initiative 1's worktree and audit the whole `ui-polish` batch as
hostile by default:

- build with the canonical VS environment command
- run the full test suite
- review the repo state for stray artifacts and docs drift
- accept baseline fixes only if they are blockers for trust or merge-readiness

The batch already claims a clean manual pass in `ui-polish-status.md`. Initiative 1 uses that
recorded pass as the current manual acceptance evidence; it does not silently invent new design
scope under the banner of “baseline cleanup.”

### Repo Normalization

After the audit passes:

- exclude `.brainstorm/` and any other scratch artifacts from the commit
- force-track `docs/ai/next-phase-design-inputs.md`
- archive the completed `ui-polish-*` docs under `docs/ai/archive/ui-polish/`
- add a compact Initiative 1 retro entry capturing the baseline-normalization learnings

### Merge Boundary

Initiative 1 closes only when this branch is:

- committed
- docs-clean
- retro-complete
- merge-ready for `master`

No Initiative 2 branch or worktree should start until that merge happens.
