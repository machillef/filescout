# Design: private-beta-ux-hardening

> **Initiative:** private-beta-ux-hardening
> **Status:** Approved
> **Last updated:** 2026-03-28

---

## Part 1 — Requirements

### Goal

Make FileScout’s wave-1 beta feel polished enough to keep using by improving the core explorer
loop and the app’s first impression, while keeping interaction behavior stable and familiar.

### Locked Product Decisions

- Beta promise is **core explorer loop + UI/UX first impression**
- First launch should feel **not Explorer, but safely familiar**
- Default first impression is **single-pane**
- Dual-pane remains part of the product but is not the first-launch layout
- UX may be more opinionated than Explorer, but navigation concepts stay familiar
- Archive mode, batch rename, and command palette are outside the wave-1 beta surface by default
- Search can stay visible only if it clears the same quality bar as the core explorer loop
- Wave-1 differentiated features should be **actually gated**, not just visually buried
- A subtle always-visible **Beta** marker should live where the current alpha marker sits

### User Stories

1. As a first-time beta user, I want the app to feel clearly better than Explorer without needing to relearn file-manager basics.
2. As a normal user, I want the first-launch layout to feel calm and understandable, so the app does not look like a power-user tool first.
3. As a user switching between `Cards`, `List`, and `Group`, I want each mode to feel intentionally different, so I can tell why I would use it.
4. As a user right-clicking, selecting, scrolling, and navigating, I want core interactions to stay reliable while the visuals get stronger.
5. As a maintainer, I want sub-beta features hidden or de-emphasized, so the private beta is judged on its strongest surface.

### Out of Scope

- Release-bundle creation and beta install documentation
- Signed distribution
- Wider beta or post-beta planning

---

## Part 2 — Design

### Experience Target

The redesign should push hardest on:

- visual hierarchy
- row and pane presentation
- view-mode identity
- first-launch coherence

It should **not** push hardest on:

- new navigation metaphors
- unfamiliar file-manager interactions
- exposing more advanced features by default

The app should look like a modern, deliberate alternative to Explorer, not a clone and not an experimental power tool.

### Wave-1 Surface Definition

Wave-1 visible surface includes:

- app launch chrome
- sidebar
- breadcrumb / pathing surface
- single active pane
- selection and row states
- `Cards`, `List`, and `Group`
- context interactions that belong to the core explorer loop

Wave-1 excludes by default:

- archive mode
- batch rename
- command palette

Search is conditional and stays visible only if it supports the same first-impression quality bar.

### Architectural Direction

This initiative should preserve the current theme system and continue using pure-domain helpers
wherever logic can be isolated from WinUI templates. The main implementation work should stay in:

- XAML for visual structure, spacing, and state presentation
- `MainWindow.xaml.cpp` for feature gating, first-launch defaults, and interaction routing
- small domain/view-model helpers only when the redesign needs stable state or derived values

### Workstreams

1. **Wave-1 gating and first-launch defaults**
   - set single-pane as the default first-launch experience
   - gate or de-emphasize non-wave-1 features at the product level
   - add the subtle always-visible beta marker in the current alpha-label area

2. **View-mode semantics**
   - define a clear role for `Cards`, `List`, and `Group`
   - make mode differences structural, not just denser or looser spacing

3. **Core explorer visual language**
   - improve row surfaces, pane hierarchy, spacing rhythm, and selection affordances
   - keep the result calm and familiar rather than loud

4. **Interaction hardening**
   - preserve or improve blank-space context ownership, empty-folder behavior, selection visibility,
     disabled custom-chrome controls, and scrollbar discoverability

5. **Manual acceptance**
   - treat the manual checklist as a real slice and beta gate, not an appendix

### Optional Figma Use

Figma is optional and view-only for this initiative. Use it only if a visual decision becomes
ambiguous and a mockup or screenshot comparison would materially improve the design. Do not block
the initiative on Figma.
