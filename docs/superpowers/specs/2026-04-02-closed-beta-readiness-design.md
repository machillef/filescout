# Closed Beta Readiness Design

Date: 2026-04-02
Repo: `C:\Users\machi\source\repos\filescout`
Target: FileScout closed beta on Windows 11 x64

## Goal

Prepare FileScout for a closed beta by completing five beta-blocking initiatives, each on its own feature branch, while keeping `master` as the continuously integrated source of truth.

This design explicitly excludes broad UI/UX polish work except where it is required to make the beta surface truthful, safe, or operationally supportable.

## Closed Beta Gate

Closed beta does not open until all five initiatives below are complete, merged to `master`, and then re-validated together on a final integration branch.

The five required initiatives are:

1. Distribution / installability
2. Beta surface truthfulness
3. Diagnostics and feedback
4. File-operation hardening
5. Session persistence

## Branch Model

### Source of truth

`master` remains the integration branch and must always reflect the newest merged work.

### Feature branches

Each readiness area gets its own feature branch:

- `codex/beta-distribution`
- `codex/beta-surface-truth`
- `codex/beta-diagnostics-feedback`
- `codex/beta-file-ops-hardening`
- `codex/beta-session-persistence`

### Final integration branch

After all five feature branches are merged into `master`, create:

- `codex/closed-beta-readiness`

This branch exists only for final end-to-end beta validation, release prep, tester instructions, and go/no-go review.

### Required branch rule

Every new feature branch must start from freshly updated `master`.

Required workflow:

1. `git checkout master`
2. `git pull --ff-only`
3. create the new `codex/...` branch from that updated `master`

If `master` changes while a feature branch is in progress, the working branch must re-sync from latest `master` before merge or before any dependent work continues.

No new readiness branch should be created from another in-progress readiness branch unless that dependency is explicitly approved.

## Initiative Breakdown

### 1. Beta Surface Truth

Branch: `codex/beta-surface-truth`

Purpose:
Define the exact closed-beta feature surface and make code, UI copy, docs, and tester expectations agree.

Scope:

- Decide which currently gated features remain out of beta
- Reconcile feature-gate behavior with README and in-app language
- Hide, relabel, or de-emphasize unfinished features
- Remove contradictory “can do today” messaging where code still gates behavior
- Ensure first-launch defaults match the intended beta promise

Definition of done:

- A tester cannot reasonably infer capabilities that the app does not actually ship
- README, app messaging, and feature gates all describe the same product

### 2. File-Operation Hardening

Branch: `codex/beta-file-ops-hardening`

Purpose:
Raise trust in copy, cut, paste, rename, delete, and related shell workflows before outside testers use the app on real files.

Scope:

- Harden failure reporting for copy/cut/paste/delete/rename
- Improve user-visible conflict, abort, and partial-failure messaging
- Review shell flags and interaction choices for destructive or silent behaviors
- Add manual verification checklist for real-file workflows
- Add or extend tests where logic can be verified headlessly

Definition of done:

- File operations fail clearly and predictably
- The app does not leave testers guessing whether an operation succeeded

### 3. Diagnostics And Feedback

Branch: `codex/beta-diagnostics-feedback`

Purpose:
Make tester failures observable and supportable without requiring developer intervention.

Scope:

- Surface app version / build identity
- Provide diagnostic export or clearly accessible logs
- Define log retention / discoverability behavior
- Add a feedback or issue-report pathway for testers
- Document exactly what a tester should send when reporting a problem

Definition of done:

- A tester can report a problem together with useful diagnostics in one pass
- Support requests do not depend on hidden knowledge of temp files or local setup

### 4. Session Persistence

Branch: `codex/beta-session-persistence`

Purpose:
Make FileScout feel dependable between launches instead of resetting to a cold state every time.

Scope:

- Persist and restore the relevant session state
- Decide persistence boundaries for:
  - active pane
  - pane layout
  - current paths
  - tabs
  - other beta-relevant workspace state
- Define recovery behavior when restored paths are missing or unavailable
- Add tests for persistence logic where practical

Definition of done:

- Relaunching the app preserves continuity instead of resetting the user to a generic home state

### 5. Distribution

Branch: `codex/beta-distribution`

Purpose:
Deliver FileScout in a form that closed-beta testers can install and run without a developer toolchain.

Scope:

- Decide and implement the closed-beta distribution mechanism
- Resolve Windows App Runtime dependency handling
- Define installer / packaging / signing path
- Define uninstall and upgrade expectations
- Produce a tester install guide

Definition of done:

- A non-developer tester can install, launch, and update or replace the build using documented steps

## Execution Order

Recommended sequence:

1. `codex/beta-surface-truth`
2. `codex/beta-file-ops-hardening`
3. `codex/beta-diagnostics-feedback`
4. `codex/beta-session-persistence`
5. `codex/beta-distribution`
6. `codex/closed-beta-readiness`

Rationale:

- Surface truth must come first because it defines the real product contract
- File-operation trust comes next because FileScout is a file manager
- Diagnostics follows so later beta validation is supportable
- Session persistence improves daily-test viability once core trust is in place
- Distribution lands last so installer/release work packages the actual agreed beta

## Final Integration Branch Scope

Branch: `codex/closed-beta-readiness`

Purpose:
Validate the merged product as a closed-beta candidate.

Scope:

- Full build and test verification on the merged result
- Final manual regression checklist
- Installer / launch / uninstall / upgrade smoke test
- Tester instructions
- Release notes
- Final go/no-go review

This branch should remain short-lived and must not become a shadow long-running integration branch.

## Testing And Verification

Each feature branch must include both:

- targeted automated verification for any new testable logic
- explicit manual validation steps for user-facing workflows

The final readiness branch must run:

- full repo build
- full test suite
- closed-beta manual acceptance checklist
- distribution smoke test on the installable artifact

## Error Handling And Operational Policy

- Any branch that uncovers a blocker in a previous merged initiative must reconcile with latest `master` before proceeding
- Readiness work should prefer small, reviewable merges into `master`
- The final readiness branch is for validation and release prep, not for hiding unfinished core work
- If one of the five initiatives expands beyond a single implementation plan, it must be decomposed into sub-plans inside that feature branch while preserving the branch boundary

## Non-Goals

- Broad new power-feature expansion unrelated to the five beta blockers
- Public beta launch planning
- Post-beta roadmap work
- Cosmetic polish that does not materially affect trust, supportability, installability, or product truth

## Deliverables

At the end of this design, the repo should eventually contain:

- one implementation plan per readiness branch
- one final implementation plan for `codex/closed-beta-readiness`
- a branch-start rule documented in every relevant plan
- final tester instructions and release notes on the integration branch
