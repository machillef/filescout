# Next Phase Design Inputs

Date: 2026-03-28
Source: `ui-polish` retro + final manual acceptance pass

## Closed In ui-polish

- Sidebar icon priority is now correct for known folders, drives, and pinned bookmarks.
- Theme persistence and live theme switching are working and should be treated as stable
  platform capability, not as open design scope.
- The final manual pass cleared these interaction checks:
  - background menu opens between rows, below the last row, and in empty folders
  - selected rows are visibly obvious in both `Cards` and `List`
  - disabled back/forward buttons no longer act like title-bar clicks
  - bookmarks expose `Unpin`
  - pane and sidebar scrollbars remain discoverable without hover

## Reuse These Patterns

- **Theme updates:** mutate existing theme brushes in place for live repainting; do not rely
  on replacing the merged dictionary object once controls already exist.

- **Row rebuilds:** after rebuilding rows, explicitly restore transient state from caches
  instead of assuming the row factory recomputes everything.

- **Blank-space interaction:** put row spacing on the item container, not inside the row
  content, so gaps belong to the pane background and can own pane-level context actions.

- **Custom title bar safety:** refresh interactive regions when enabled/disabled state
  changes; disabled controls still need to stay inside the non-client passthrough mapping.

- **View-mode distinction:** make modes different by structure first, then by spacing.
  Group headers, row surfaces, icon tiles, and selection overlays matter more than small
  padding deltas.

## Design Constraints For The Next Phase

- Do not reopen theme infrastructure unless the new phase explicitly adds new palettes,
  settings, or accessibility modes.

- Treat interaction invariants as first-class acceptance criteria:
  - blank-space right-click
  - empty-folder behaviour
  - selected-state visibility
  - disabled custom chrome buttons
  - context-menu ownership
  - scrollbar discoverability

- Any slice that changes WinUI templates, row containers, or pointer routing should include
  a manual-validation gate before being marked complete.

- Keep new logic in pure domain helpers where possible. Template-only visual changes may not
  justify tests, but sorting, caching, mapping, and persistence logic should continue to be
  extracted and covered.

## Candidate Next-Phase Framing

Recommended initiative shape: `view-modes-and-interactions`

Goal:
Make `Cards`, `List`, and `Group` feel intentionally different while hardening pane
interaction ownership so common file-manager actions stay reliable across empty, sparse,
and dense directories.

Suggested slice themes:

1. **Mode semantics audit**
Define what each mode is for, what visual cues must be unique, and what should stay shared.

2. **Template clarity**
Tighten row/card/group visuals so users can identify the active mode immediately.

3. **Interaction ownership**
Lock blank-space, row-gap, empty-folder, and context-menu behaviour to the correct hosts.

4. **Custom chrome regression pass**
Retest title-bar passthrough, history buttons, flyout anchoring, and focus edge cases.

5. **Manual acceptance**
Run a fixed checklist and only then mark the initiative complete or ready for archive.

## Open Questions To Answer Before Bootstrapping

- Should `Group` remain a modifier on top of the active base view, or become a distinct mode
  with its own stronger layout?

- Is the next goal primarily **clarity** (users can tell modes apart immediately) or
  **density/productivity** (fit more information without losing scanability)?

- Do we want a lightweight UI automation enhancement for repeatable screenshots / state
  checks, or is manual acceptance still the right tradeoff for these WinUI interactions?
