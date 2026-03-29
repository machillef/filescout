# Status: private-beta-ux-hardening

## Initiative State

- **Status:** Complete
- **Goal:** Deliver the noticeable redesign + interaction hardening pass that makes the core explorer loop feel beta-worthy
- **Branch:** `codex/private-beta-ux-hardening`
- **Worktree:** `C:\Users\machi\source\repos\filescout\.worktrees\private-beta-ux-hardening`

## Slice Tracker

| Slice | Name | Status |
|---|---|---|
| U1 | Wave-1 surface gating and first-launch defaults | Complete |
| U2 | View-mode semantics and visual identity | Complete |
| U3 | Core explorer chrome and pane language | Complete |
| U4 | Interaction hardening and manual acceptance | Complete |

## Bootstrap Notes

- Initiative classified as **medium**.
- Seed material: `docs/ai/next-phase-design-inputs.md`
- Current baseline verified in this worktree:
  - canonical Debug x64 build: PASS
  - full test suite: PASS (`134` tests)

## Slice U1: Wave-1 surface gating and first-launch defaults
Status: Complete
Last updated: 2026-03-28

### What was implemented
- Added `native/FileScout.Native/Domain/Wave1FeatureGate.h` so wave-1 feature availability is explicit and testable.
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` to block command palette launch, block archive browsing/extract flows, keep multi-select rename from opening batch rename, and prevent hidden-pane switching.
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` to default first launch to single-pane while keeping a secondary dual-pane toggle available in chrome.
- Updated `native/FileScout.Native/MainWindow.xaml` to replace the loud `ALPHA` pill with a subtler `Beta` marker and add the dual-pane toggle button.
- Added `native/FileScout.Tests/tests/Wave1FeatureGateTests.cpp` and registered it in `native/FileScout.Tests/FileScout.Tests.vcxproj`.

### What was validated
- `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"`: PASS
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no`: PASS (`134` tests)
- Manual host pass in the worktree build: PASS for first-launch single-pane, dual-pane toggle visibility/interaction, gated wave-1 feature affordances, and the subtler `Beta` marker placement.

### What remains unverified
- None.

### Design notes from manual check
- The current `List` presentation leaves too little obvious pane-owned blank space for actions like `New folder`; the rows visually occupy too much of the available width, so right-clicking the pane background requires moving to the small free area near the top.
- Treat this as a redesign note for U3/U4 rather than a U1 bug: investigate a clearer trailing gutter, lighter full-width row coverage, and slightly tighter row/icon-tile proportions so blank-space ownership stays obvious.

### Blockers
- None

### Next recommended step
- Continue with U2 to make `Cards`, `List`, and `Group` read as intentionally different presentations.

## Slice U2: View-mode semantics and visual identity
Status: Complete
Last updated: 2026-03-28

### What was implemented
- Updated `native/FileScout.Native/MainWindow.xaml` so `Cards` now read as larger summary cards with separated name/date/detail structure instead of a denser table row.
- Updated `native/FileScout.Native/MainWindow.xaml` so `List` stays the compact tabular option with flatter surfaces, tighter icon treatment, and clearer “column row” behavior.
- Added a dedicated `GroupRowTemplate` and `GroupRowItemStyle` in `native/FileScout.Native/MainWindow.xaml` so grouped results render as visibly sectioned lanes rather than the old list/cards templates with only injected headers.
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` so grouped mode now swaps to the dedicated grouped template, the header toggles treat `Group` like a first-class presentation, and returning to `Cards` or `List` exits grouped mode cleanly while preserving the remembered base mode.
- Followed up on manual feedback in `native/FileScout.Native/MainWindow.xaml` by moving more width ownership back to the pane with explicit trailing item-container margins for `Cards`, `List`, and `Group`.
- Followed up on manual feedback in `native/FileScout.Native/MainWindow.xaml` and `native/FileScout.Native/Domain/FileRowViewModel.h` by making grouped rows more clearly sectioned with a stronger header band, deeper nested inset, and slightly taller group-header allocation.
- Fixed grouped-navigation state restoration in `native/FileScout.Native/MainWindow.xaml.cpp` so fresh loads and search restores now reapply grouped headers and cached icons before async icon backfill continues.
- Followed up again in `native/FileScout.Native/MainWindow.xaml` by increasing the pane-owned trailing gutter more aggressively in `List` and `Group` so blank-space targeting is more obvious.

### What was validated
- `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"`: PASS
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no`: PASS (`134` tests)
- Manual host validation: PASS for `Cards` vs `List` vs `Group` visual contrast, grouped-mode folder navigation/back restoration, and selected-row clarity in all three presentations.

### What remains unverified
- None.

### Manual feedback follow-up (2026-03-28)
- Reported by: User report during host validation.
- Observed issue: `Group` had its own styling, but the presentation still was not distinct enough to read immediately as a different mode.
- Observed issue: row surfaces still consumed too much pane width, so the intended extra right-side blank space was not yet obvious.
- Scope call: collapsible group sections would be a worthwhile enhancement, but they are a new interaction behavior rather than a U2 bug fix; keep that idea for later unless the next manual pass shows it is required for basic mode clarity.

### Regression follow-up: grouped navigation + blank-space targetability (2026-03-28)
- Reported by: User report during host validation.
- Observed issue: when `Group` was active, navigating into a folder and then back could leave grouped mode visually selected while row icons and group-header treatment were no longer restored consistently.
- Root cause: fresh navigation/search restore paths rebuilt row view-models without reapplying grouped header state or cached icon sources, and grouped mode also skipped the async icon backfill path entirely.
- Fix: updated `native/FileScout.Native/MainWindow.xaml.cpp` so all fresh row rebuild paths funnel through the same pane-state reapplication logic and grouped mode no longer bails out of icon backfill; updated `native/FileScout.Native/MainWindow.xaml` so grouped/list rows leave a materially larger trailing pane gutter.
- Tests added: none practical for this private `MainWindow` navigation path without dragging WinUI window-loading code into the test binary; retained the existing `FileRowViewModel` grouped-header assertion and re-ran the full suite.
- Validated: `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"`: PASS; `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no`: PASS (`134` tests)

### Manual host pass follow-up (2026-03-28)
- Reported by: Codex host validation in the worktree build.
- Observed result: `Cards` reads as the spacious summary presentation, `List` reads as the dense tabular mode, and `Group` reads as its own grouped lane treatment rather than a light overlay on top of `List`.
- Observed result: with grouped mode active, opening `%NOTEPAD++_DIR%` and returning via Back preserved the grouped header band, row icons, and grouped visual treatment in `C:\Users\machi`.
- Observed result: selected rows remained obvious in `Cards`, `List`, and `Group`; the lavender selection fill plus outline stayed readable against the darker pane surface in each presentation.
- Evidence: worktree host screenshots captured for `Cards`, `List`, `Group`, grouped inside-folder navigation, grouped return state, and selected-row states for each mode. One foreground-desktop capture attempt accidentally picked up another app and was discarded; the retained selection-state captures were re-run with explicit `filescout_uia.ps1 screenshot -WindowHandle ...` app-window capture.

### Blockers
- None

### Next recommended step
- Continue with U3 to refine the title bar, sidebar, pane hierarchy, and overall chrome coherence now that the three view modes are visually locked.

## Slice U3: Core explorer chrome and pane language
Status: Complete
Last updated: 2026-03-28

### What was implemented
- Updated `native/FileScout.Native/MainWindow.xaml` so the title-bar chrome reads calmer and less chunky: flatter toolbar buttons, quieter `Beta` marker treatment, stronger spacing rhythm, and a lighter breadcrumb/search/theme strip.
- Updated `native/FileScout.Native/MainWindow.xaml` so the sidebar reads as its own quieter shell with softened framing, calmer section rhythm, and less shouty selection preview language.
- Updated `native/FileScout.Native/MainWindow.xaml` so pane shells, sort-header surfaces, and the status bar share a more coherent rounded-card language instead of the earlier harder-edged mixed treatment.
- Preserved the U2 mode semantics while making the surrounding explorer surfaces feel visually related rather than like separate design passes.

### What was validated
- `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"`: PASS
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no`: PASS (`134` tests)
- Review pass 1, code quality / de-sloppify: PASS after fixing one stale XAML comment that still described the active-pane border as `2-px` after the calmer `1px` chrome shift.
- Review pass 2, spec compliance: PASS; the slice stayed inside the approved U3 scope of title bar, sidebar, pane hierarchy, and row/pane state harmony with no new interaction behavior added.
- Review pass 3, risk / regressions: PASS; no active-pane routing, theme persistence, or row-template regressions were introduced by the visual-only XAML changes.
- Manual host validation: PASS for calmer single-pane first impression, coherent dual-pane coexistence, and consistent visual language across title bar, sidebar, pane shells, and status bar.

### What remains unverified
- No dedicated narrow-window or light-theme manual pass yet; keep that as optional coverage unless U4 acceptance exposes a real issue.

### Manual host pass follow-up (2026-03-28)
- Reported by: Codex host validation in the worktree build.
- Observed result: the top chrome now reads quieter and more deliberate, with the breadcrumb bar carrying the main emphasis instead of the buttons competing for attention.
- Observed result: the sidebar framing feels calmer and more intentional, and the pane shells now feel part of the same system rather than separate surfaces bolted together.
- Observed result: enabling dual-pane still preserves the calmer hierarchy; the active pane remains clear without making the whole layout feel heavy.
- Evidence: explicit app-window screenshots captured to `C:\Users\machi\AppData\Local\Temp\filescout-u3-20260328-2016` (`single-pane.png`, `dual-pane-selected.png`).

### Blockers
- None

### Next recommended step
- Continue with U4 to retest blank-space ownership, empty-folder behavior, custom-chrome disabled-state behavior, context-surface ownership, scrollbar discoverability, and the fixed manual acceptance checklist.

## Slice U4: Interaction hardening and manual acceptance
Status: Complete
Last updated: 2026-03-28

### What was implemented
- Updated `native/FileScout.Native/MainWindow.xaml.h` so filter-mode rows keep a dedicated `filtered_items` backing vector and the window can resolve interactions against the currently displayed item set instead of the full pane item list.
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` so filter-mode search rebuilds keep `filtered_items` in sync on entry, query changes, empty-query restore, recursive-search promotion, and normal directory navigation exits.
- Updated `native/FileScout.Native/MainWindow.xaml.cpp` so double-click, right-click, context-command execution, selection synchronization, row-selection visuals, multi-selection detail text, and path-to-row lookup all resolve against the currently displayed filter results instead of stale hidden rows.
- Added `docs/ai/private-beta-ux-hardening-manual-acceptance-checklist.md` as the fixed U4 checklist and recorded the latest manual acceptance evidence there.

### What was validated
- `.\agents\skills\build-verify-windows\scripts\restore-build-debug.ps1` + `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no`: PASS x3 (`134/134` tests on all three passes)
- Review pass 1, correctness / code quality: PASS; the filter-mode row backing fix now follows the displayed row source through selection and command paths, and no new stale-index path remained inside the U4 changes.
- Review pass 2, slice/scope compliance: PASS; the work stayed inside U4 interaction hardening and checklist closeout rather than reopening the U2/U3 presentation work.
- Review pass 3, regression / adjacent-risk review: PASS; no active-pane routing or non-filter navigation regressions were introduced. Residual adjacent risk remains that recursive-search selection/details still rely on older paths outside this U4 fix.
- Manual host validation: PASS for filtered-search visible-row navigation, sparse-folder blank-space menu behavior, row-gap targetability, empty-folder background menu behavior, item-aware context menu ownership, and disabled custom-chrome navigation buttons.
- Scrollbar discoverability validation: PASS with note; reduced-height screenshots were captured and the live visibility configuration remained intact, although WinUI UI Automation did not expose the rendered bars as visible elements during the probe.

### What remains unverified
- No additional U4 blockers remain. The only adjacent note is the existing recursive-search selection/details path, which was not widened into this slice.

### Blockers
- None

### Next recommended step
- Run retro, consolidate any reusable search/selection mapping lessons, then archive the initiative docs if no new scope is being added.

## Next Recommended Step

- Run retro for `private-beta-ux-hardening`, then archive the initiative docs if no follow-on slice is being added.
