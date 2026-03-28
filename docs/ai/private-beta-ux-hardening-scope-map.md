# Scope Map: private-beta-ux-hardening

## Problem Statement

FileScout now has an audited `ui-polish` baseline, but the app is still too uneven for a
private beta whose success depends on first impression. The next initiative must turn the
core explorer loop into something that feels clearly better than Explorer without becoming
unfamiliar or noisy. The wave-1 beta promise is the **core explorer loop + UI/UX first
impression**, not broad power-feature coverage.

## In-Scope

| File / Area | Changes |
|---|---|
| `native/FileScout.Native/MainWindow.xaml` | Primary row/pane/chrome visual system for wave-1 explorer surfaces |
| `native/FileScout.Native/MainWindow.xaml.cpp` | Feature gating, first-launch defaults, and interaction ownership adjustments that support the redesign |
| `native/FileScout.Native/Domain/FileRowViewModel.*` and small pure helpers | Add or refine state only if required to support distinct mode semantics or stable visual behavior |
| `docs/ai/next-phase-design-inputs.md` | Seed material and constraints source |
| `README.md` if needed late in the initiative | Only for visible beta-surface messaging such as single-pane default / dual-pane capability wording |

## Adjacent (Understand, Don't Touch Unless Required)

| Area | Contract |
|---|---|
| Theme infrastructure (`ThemeDefinition`, `ThemeService`, `App::ApplyTheme`) | Stable baseline; do not reopen unless the redesign exposes a concrete blocker |
| Search, archive mode, batch rename, command palette | Outside the wave-1 beta surface unless individually promoted later |
| Packaging / installer / signing | Initiative 3 scope, not Initiative 2 |

## Out of Scope

- Broad new feature development
- Installer or release-bundle implementation
- Public-beta or post-beta decisions
- Telemetry / feedback systems

## Definition of Done

1. The core explorer loop feels **not Explorer, but safely familiar** on first launch.
2. Wave-1 first impression defaults to **single-pane**, with dual-pane treated as a secondary capability.
3. Core explorer surfaces remain visually coherent end-to-end: title bar, sidebar, pane bodies, row states, view modes, and context surfaces.
4. Sub-beta differentiated features are actually gated or de-emphasized, not merely left visible and rough.
5. The initiative ends with a fixed manual acceptance checklist that covers mode clarity and interaction invariants.

## Verification Commands

| Phase | Command |
|---|---|
| Build | `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` |
| Test (single) | `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=FileRowViewModel.*:RowIconState.*:ShellItemSort.*:ThemeService.*` |
| Test (full) | `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` |
| Type check | `N/A` |
| Lint | `N/A` |
