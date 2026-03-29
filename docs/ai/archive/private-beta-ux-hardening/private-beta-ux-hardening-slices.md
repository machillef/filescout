# Slices: private-beta-ux-hardening

## U1: Wave-1 surface gating and first-launch defaults

**Goal:** Lock the wave-1 beta surface and first-launch experience before the heavier visual redesign.

**Touched area:**
- main window chrome and first-launch layout defaults
- product-level visibility/gating for sub-beta features

**User stories:** 1, 2, 5

**Done when:**
- single-pane is the wave-1 first-launch default
- archive mode, batch rename, and command palette are gated or clearly off the wave-1 path
- search is either explicitly kept or explicitly demoted based on the same quality bar
- the subtle `Beta` marker placement is implemented in the current alpha-marker area

---

## U2: View-mode semantics and visual identity

**Goal:** Make `Cards`, `List`, and `Group` feel intentionally different and worth using.

**Touched area:**
- row templates
- mode-specific spacing, structure, headers, and selection presentation

**User stories:** 1, 3

**Done when:**
- each mode has a clear visual role
- differences are structural, not just small padding changes
- selection state remains obvious in all included modes

---

## U3: Core explorer chrome and pane language

**Goal:** Improve the overall calm-but-opinionated first impression across the core explorer loop.

**Touched area:**
- title bar area
- sidebar
- pane hierarchy and surfaces
- row/pane state harmony

**User stories:** 1, 2

**Done when:**
- the app feels clearly not-Explorer but still familiar
- the first 5 minutes of use read as visually coherent end-to-end
- no core explorer surface visually breaks the story

---

## U4: Interaction hardening and manual acceptance

**Goal:** Keep the stronger UX from weakening the core interaction loop, then lock the result with a fixed acceptance checklist.

**Touched area:**
- blank-space input ownership
- empty-folder behavior
- custom chrome disabled-state behavior
- context surfaces and scrollbar discoverability
- manual acceptance checklist

**User stories:** 4, 5

**Done when:**
- the redesign preserves the interaction invariants from `next-phase-design-inputs.md`
- a fixed manual checklist exists and is run before closeout
- the initiative is ready for retro, learning consolidation, and merge
