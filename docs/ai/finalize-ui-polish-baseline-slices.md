# Slices: finalize-ui-polish-baseline

## F1: Hostile audit of the `ui-polish` batch

**Goal:** Verify that the current dirty `ui-polish` code and tests are strong enough to become the
beta-prep baseline.

**Touched area:**
- existing `ui-polish` code and tests
- no new feature scope

**User stories:** 1

**Done when:**
- canonical Debug x64 build passes
- full test suite passes
- no baseline blockers remain in the audited batch

---

## F2: Normalize docs and archive `ui-polish`

**Goal:** Convert the completed `ui-polish` initiative from active docs into archived docs and make
the repo’s durable next-step inputs explicit.

**Touched area:**
- `docs/ai/ui-polish-*`
- `docs/ai/archive/ui-polish/`
- `docs/ai/next-phase-design-inputs.md`
- `docs/ai/retro-log.md`

**User stories:** 2, 3

**Done when:**
- `ui-polish-*` docs are archived
- `next-phase-design-inputs.md` is part of the baseline commit
- the repo clearly distinguishes completed work from upcoming work

---

## F3: Merge-ready baseline commit

**Goal:** Produce the committed baseline that future beta-prep work can safely build on.

**Touched area:**
- all verified baseline files from `ui-polish`
- Initiative 1 docs

**User stories:** 1, 2, 3

**Done when:**
- the branch has one coherent baseline commit
- Initiative 1 docs reflect completion
- the branch is ready to merge to `master`
