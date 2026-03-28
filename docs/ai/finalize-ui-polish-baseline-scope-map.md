# Scope Map: finalize-ui-polish-baseline

## Problem Statement

`ui-polish` was bootstrapped and then implemented as a large dirty local batch on top of `master`.
The code and status doc now claim a clean manual pass, but the batch has not yet been normalized
into a merge-ready baseline. Before any private-beta UX work starts, the repo needs one hostile
audit pass that proves the current `ui-polish` state is trustworthy, archives the completed
initiative docs, and commits the audited baseline to `master`.

## In-Scope

| File / Area | Changes |
|---|---|
| `native/FileScout.Native/*` touched by the current `ui-polish` batch | Audit, baseline fixes only if they are blockers, no new UX scope |
| `native/FileScout.Tests/*` touched by the current `ui-polish` batch | Audit and keep regression coverage aligned with the baseline |
| `docs/ai/ui-polish-*` | Normalize final state and archive after verification |
| `docs/ai/retro-log.md` | Keep the completed `ui-polish` retro and add Initiative 1 closeout if needed |
| `docs/ai/next-phase-design-inputs.md` | Force-track as the checked-in seed for Initiative 2 |

## Adjacent (Understand, Don't Touch)

| Area | Contract |
|---|---|
| `.brainstorm/` scratch HTML output | Local brainstorming artifact; not part of the repo baseline |
| Future beta-prep initiatives | Do not start redesign or release gating work inside Initiative 1 |
| Installer / signing work | Explicitly deferred to later beta-readiness scope |

## Out of Scope

- New UX redesign work
- New private-beta gating rules beyond what is needed to normalize the baseline
- New feature development
- Packaging / installer implementation

## Definition of Done

1. The current `ui-polish` code batch passes the canonical Debug x64 build and full test suite.
2. The baseline commit excludes scratch artifacts and includes the checked-in design seed for Initiative 2.
3. `ui-polish-*` docs are archived only after the audited baseline is verified.
4. `master` can receive this branch as the stable starting point for `private-beta-ux-hardening`.

## Verification Commands

| Phase | Command |
|---|---|
| Build | `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` |
| Test (single) | `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=ThemeService.*:ShellItemSort.*:RowIconState.*:SidebarIconInfo.*` |
| Test (full) | `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` |
| Type check | `N/A` |
| Lint | `N/A` |
