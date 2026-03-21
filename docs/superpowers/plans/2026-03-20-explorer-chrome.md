# Explorer Chrome Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make FileScout feel materially closer to Windows Explorer by integrating the top chrome into a custom title bar, flattening the shell styling, and restyling the left sidebar without moving pane tabs or adding new search UI.

**Architecture:** This is one focused UI slice centered on the existing `MainWindow` shell. `MainWindow.xaml` will own the visual restructuring of the top chrome and sidebar surfaces, while `MainWindow.xaml.cpp` will add bounded title-bar wiring plus Explorer-inspired sidebar icon handling. Existing pane tabs, file-row shell icons, and core navigation behavior remain in place.

**Tech Stack:** WinUI 3, Windows App SDK 1.8, C++/WinRT, XAML, Windows 11 custom title bar APIs, MSBuild Debug x64, GoogleTest

**Spec:** `docs/superpowers/specs/2026-03-20-explorer-chrome-design.md`
**Visual sources:** `C:\Users\machi\source\repos\filescout\.superpowers\brainstorm\2171-1773962558\full-design-v2.html`, `C:\Users\machi\Pictures\windows-explorer-icons.png`

**Build command:**
```cmd
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"
```

**Test command:**
```cmd
native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no
```

---

## File Map

| File | What changes |
|---|---|
| `native/FileScout.Native/MainWindow.xaml` | Replace detached top toolbar card with integrated title-bar region, add named drag region, flatten top shell styling, restyle left sidebar rows/surfaces while keeping lower selection/details structure intact |
| `native/FileScout.Native/MainWindow.xaml.cpp` | Add bounded custom title-bar integration wiring, ensure interactive controls stay hit-test safe, replace Fluent-first sidebar icon rule with Explorer-inspired shell-backed behavior, tune sidebar row/drive styling construction |
| `native/FileScout.Native/MainWindow.xaml.h` | Add small helper declarations or cached title-bar state only if required by the WinUI title-bar wiring |
| `docs/ai/status.md` | Record Explorer-chrome slice completion status and verification results |
| `docs/ai/plan.md` | Update active initiative state / next-step summary after implementation |

No new product features. No project file changes expected.

---

## Task 1: Add bounded custom title-bar integration plumbing

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml`
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp`
- Modify if needed: `native/FileScout.Native/MainWindow.xaml.h`

The app already uses `MicaBackdrop`, but the top shell still sits below the system frame. This task adds only the plumbing required to integrate the existing top region into the title bar and keep interactive controls working correctly.

- [ ] **Step 1: Inspect the current window configuration path**

Read `MainWindow::ConfigureWindow()` and the window-construction path in `MainWindow.xaml.cpp`. Confirm where `MicaBackdrop` is applied and identify the right place to add title-bar integration after the HWND and XAML root exist.

- [ ] **Step 2: Name the future draggable title-bar host in XAML**

Add a dedicated named top-level element in `MainWindow.xaml` for the future integrated title bar, for example `AppTitleBarHost`. This should wrap the top shell only, not the pane `TabView` controls.

- [ ] **Step 3: Add a small helper for title-bar setup**

In `MainWindow.xaml.cpp`, add a focused helper such as `ConfigureTitleBar()` or equivalent local logic that:
- extends app content into the title bar
- registers the named XAML region as the drag surface
- preserves caption button coexistence
- keeps text boxes and buttons inside that region interactive rather than draggable

Do not widen this helper into unrelated window-management work.

- [ ] **Step 4: Reserve safe space for caption buttons**

Adjust the top-shell layout so the integrated title-bar region does not render active content underneath the caption buttons. Use the WinUI/AppWindow title-bar insets or an equivalent bounded approach to reserve trailing space on the top row for minimize, maximize, and close buttons.

Expected:
- top controls do not visually overlap caption buttons
- pointer interaction on caption buttons remains native and unaffected

- [ ] **Step 5: Add header declarations only if required**

If the chosen wiring needs a helper declaration or cached state, add the minimum necessary declaration in `MainWindow.xaml.h`. Do not add speculative members.

- [ ] **Step 6: Keep pane tabs out of the title bar**

Verify the title-bar drag region includes only the top chrome. The left and right pane `TabView` controls must remain in their existing content-area positions.

- [ ] **Step 7: Build immediately after wiring**

Run the build command.

Expected:
- build succeeds
- no new warnings beyond known `MSB8027` / `LNK4042`

- [ ] **Step 8: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml native/FileScout.Native/MainWindow.xaml.cpp native/FileScout.Native/MainWindow.xaml.h
git commit -m "feat(ui): integrate custom title bar shell"
```

---

## Task 2: Restructure the top chrome to feel Explorer-inspired

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml`

This task removes the detached toolbar-card feel and replaces it with integrated shell styling. It does not move the pane tabs and does not introduce a new search box.

- [ ] **Step 1: Remove the bordered floating-card treatment from the top region**

Update the top row in `MainWindow.xaml` so it is no longer a rounded bordered panel sitting below the frame. Reduce or eliminate the visible outer border, lower the corner emphasis, and rely more on spacing and tonal contrast than card outlines.

- [ ] **Step 2: Keep the current top behaviors but flatten the visual language**

Retain the existing Back, Forward, address field, and Refresh controls. Restyle them so they read as flatter Explorer-inspired shell controls rather than standalone pills.

- [ ] **Step 3: Quiet the brand block**

Keep `FileScout` and the `ALPHA` badge, but reduce their visual weight so they no longer dominate the top-left area.

- [ ] **Step 4: Keep the address field as an address field**

Do not add a separate search box. The existing address field should remain the only main text-entry control in the top chrome.

- [ ] **Step 5: Implement theme-aware top-shell brushes**

Use theme-aware brushes or explicit per-theme values so the top chrome works in both dark mode and light mode. Do not hard-code a dark-only palette into the integrated title bar.

- [ ] **Step 6: Preserve pane content layout**

Leave the left and right pane `TabView` controls in place. This task must not restructure pane rows, file lists, or the two-pane layout.

- [ ] **Step 7: Build after the XAML restructure**

Run the build command.

Expected:
- XAML compiles cleanly
- no new warnings beyond known `MSB8027` / `LNK4042`

- [ ] **Step 8: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml
git commit -m "feat(ui): restyle top chrome to match explorer shell"
```

---

## Task 3: Restyle the left sidebar with a fixed Explorer-inspired icon rule

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml`
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp`

This task applies the approved sidebar rule:
- favorites and known places use shell-backed or native-style icons when available
- drives use shell-style drive icons
- the lower selection/details block keeps its current icon behavior and structure

- [ ] **Step 1: Inspect current sidebar construction helpers**

Read the existing sidebar-building helpers in `MainWindow.xaml.cpp`, including the functions that currently build favorite/place icons, drive glyphs, and drive-usage rows.

- [ ] **Step 2: Replace the Fluent-first sidebar icon path with the approved fixed rule**

Update sidebar icon selection so:
- known places/favorites prefer shell-backed or native-style icons
- drives use shell-style drive icons
- the selection/details block continues using its existing icon path

Do not widen this into a global icon rewrite.

- [ ] **Step 3: Flatten sidebar row styling in XAML and/or code-built elements**

Tune sidebar colors, selection fill, spacing, dividers, and drive-card weight so the left column reads closer to Explorer and less like app chrome.

- [ ] **Step 4: Keep sidebar theme-aware in both dark and light modes**

Use theme-aware brushes or explicit per-theme overrides so the sidebar remains coherent in both dark mode and light mode. Validate that the Explorer-inspired treatment does not assume dark mode only.

- [ ] **Step 5: Keep the lower selection/details block structurally unchanged**

Only harmonize colors or border treatment if needed. Do not redesign its layout, reorder fields, or replace its current preview/detail behavior.

- [ ] **Step 6: Preserve file-row icons**

Do not modify the shell-extracted file-row icon pipeline. Confirm that only sidebar icon behavior changed.

- [ ] **Step 7: Build after sidebar updates**

Run the build command.

Expected:
- build succeeds
- no new warnings beyond known `MSB8027` / `LNK4042`

- [ ] **Step 8: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml native/FileScout.Native/MainWindow.xaml.cpp
git commit -m "feat(ui): bring sidebar chrome closer to explorer"
```

---

## Task 4: Verify runtime behavior and record durable state

**Files:**
- Modify: `docs/ai/status.md`
- Modify: `docs/ai/plan.md`

This task verifies that the new chrome feels more native without regressions, then records the slice outcome in the repo’s durable state.

- [ ] **Step 1: Run the full build**

Run:

```cmd
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"
```

Expected:
- exit code `0`
- only known `MSB8027` / `LNK4042` warnings

- [ ] **Step 2: Run the GoogleTest suite**

Run:

```cmd
native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no
```

Expected:
- all tests pass

- [ ] **Step 3: Run a manual runtime validation pass**

Launch the app and verify:
- the white top-frame separation is gone or clearly reduced
- the top reads as integrated chrome rather than a toolbar card under the frame
- top controls do not overlap caption buttons
- pane tabs remain in the content area
- no new search box appears
- dark mode feels coherent and Explorer-inspired
- light mode remains coherent and native-looking
- sidebar icons feel closer to Explorer
- the lower selection/details block remains structurally unchanged
- file-row icons still use shell/native icons
- Back, Forward, Address, and Refresh remain functional after title-bar integration
- address-box text interaction still works normally inside the draggable top region

If available during implementation, using `filescout-ui-automation` for screenshots is acceptable, but manual visual validation is still required.

- [ ] **Step 4: Update durable docs**

Update `docs/ai/status.md` with:
- what changed in the Explorer-chrome slice
- build/test/manual validation results
- any residual UI edge cases

Update `docs/ai/plan.md` with:
- the new current state
- whether UI polish is considered complete enough to move to performance work
- any small follow-up polish items left open

- [ ] **Step 5: Review diff before handoff**

Run:

```bash
git diff -- native/FileScout.Native/MainWindow.xaml native/FileScout.Native/MainWindow.xaml.cpp native/FileScout.Native/MainWindow.xaml.h docs/ai/status.md docs/ai/plan.md
```

Expected:
- diff matches the approved Explorer-chrome scope
- no accidental pane-tab move
- no new search feature
- no unrelated file-row changes

- [ ] **Step 6: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml native/FileScout.Native/MainWindow.xaml.cpp native/FileScout.Native/MainWindow.xaml.h docs/ai/status.md docs/ai/plan.md
git commit -m "feat(ui): implement explorer-inspired shell chrome"
```

---

## Done Criteria

- [ ] Custom title-bar integration is in place and interactive controls still work
- [ ] Top chrome no longer reads as a detached bordered card under the frame
- [ ] Caption buttons coexist cleanly with the integrated top shell
- [ ] Pane tabs remain where they are today
- [ ] No new search UI was introduced
- [ ] Left sidebar feels materially closer to Explorer
- [ ] Lower selection/details block remains structurally unchanged
- [ ] Dark mode and light mode both validate cleanly
- [ ] Back, Forward, Address, and Refresh still behave correctly after title-bar integration
- [ ] Build passes
- [ ] Tests pass
- [ ] Manual runtime validation is recorded in `docs/ai/status.md`
- [ ] `docs/ai/plan.md` reflects whether the team is ready to switch focus to performance work
