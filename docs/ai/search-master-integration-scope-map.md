# Scope Map: search-master-integration

## Problem Statement

The completed fuzzy-search work on `feature/r17-fuzzy-search` cannot be merged directly into
current `master` because the UI architecture has moved on. `master` already has a dedicated
`SearchTextBox`, breadcrumb-driven navigation chrome, `FileRowViewModel` row rendering, and
per-pane search state. The integration work must forward-port the missing fuzzy-search behavior
from commit `ea49541` into the current UX shell without regressing the latest `master` experience.

## Objective

Preserve the current `master` search UX and integrate only the missing fuzzy-search logic,
rendering polish, and search-service hardening needed for the current branch to match the intended
behavior of the completed feature branch.

## Stack And Runtime

- Language: C++20
- UI: WinUI 3 with C++/WinRT
- Build: MSBuild v143 via Visual Studio 2022 Build Tools
- Tests: GoogleTest native test binary
- Platform: Windows 11 x64 desktop app

## Existing Evidence

- `master` already contains `SearchMode`, per-pane `search_results`, and a dedicated
  `SearchTextBox` in `native/FileScout.Native/MainWindow.xaml*`.
- `master` already contains `Search/FuzzyMatcher.h` and a recursive `SearchService`, but it does
  not contain `SearchFilter.h` or `SearchPresentation.h`.
- `feature/r17-fuzzy-search` commit `ea49541` contains search-filter helpers, search-presentation
  helpers, stricter recursive-search constraints, and updated search tests that do not cleanly
  merge because `master` has newer UI patterns.

## In-Scope

| File / Area | Changes |
|---|---|
| `native/FileScout.Native/MainWindow.xaml` | Integrate any search-only visual affordances that are missing on `master` while preserving current chrome and layout direction |
| `native/FileScout.Native/MainWindow.xaml.h` | Reconcile per-pane search state and helper declarations needed for the integrated behavior |
| `native/FileScout.Native/MainWindow.xaml.cpp` | Forward-port fuzzy filter, recursive escalation, cancellation, result navigation, and search-row presentation into current `master` event flow |
| `native/FileScout.Native/Search/FuzzyMatcher.h` | Reuse as-is or minimally adjust only if required by the integrated behavior |
| `native/FileScout.Native/Search/SearchFilter.h` | Add the local fuzzy-filter helper if still absent on `master` |
| `native/FileScout.Native/Search/SearchPresentation.h` | Add search-row presentation helpers if still absent on `master` |
| `native/FileScout.Native/Search/SearchService.*` | Forward-port only the hardening and contract changes required by the current UI |
| `native/FileScout.Tests/tests/Search*` | Reconcile and expand search-focused tests to cover the integrated behavior on `master` |
| `docs/ai/search-master-integration-*` | Track the forward-port separately from the historical `R17` docs |

## Adjacent (Understand, Do Not Rewrite)

| Area | Contract |
|---|---|
| `native/FileScout.Native/Domain/FileRowViewModel.*` | Keep the current row-model approach; adapt search rendering to it instead of reverting to legacy row construction |
| Breadcrumb and address chrome in `MainWindow.xaml*` | Preserve current `master` UX direction and event flow |
| `Shell/CommandRegistry.*` and command-palette flow | Search integration may coexist with commands, but this initiative does not redesign command routing |
| Existing top-level `docs/ai/plan.md`, `docs/ai/slices.md`, `docs/ai/status.md` | Treat as historical current-state docs; do not overwrite them during bootstrap of this new integration initiative |
| `.brainstorm/` | Local scratch output; not part of this initiative |

## Out of Scope

- Reverting `master` to the older shared-address-bar search model
- Replacing the current breadcrumb or command-palette UX
- Packaging, installer, or dependency bundling changes
- New non-search UX work unrelated to fuzzy-search integration
- Broad cleanup of historical docs outside the new initiative file set

## Definition of Done

1. Current `master` preserves its dedicated `SearchTextBox` and breadcrumb UX while gaining the
   intended fuzzy filter and recursive search behavior from `ea49541`.
2. Search result rendering and navigate-on-select behavior work through the current
   `FileRowViewModel`-based row pipeline instead of reintroducing obsolete UI patterns.
3. Recursive search honors the hardened constraints from the feature branch where still applicable:
   cancellation, local-drive-only roots, bounded depth, and deterministic completion behavior.
4. Search-specific tests on `master` cover the integrated filter, presentation, and recursive
   search behavior, and the canonical build plus full native test suite remain green.

## Verification Commands

| Phase | Command |
|---|---|
| Build | `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64"` |
| Test (single) | `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=SearchFilter*:*SearchPresentation*:*SearchService*` |
| Test (full) | `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no` |
| Type check | `N/A` |
| Lint | `N/A` |

## CI / Eval Entry Points

- Eval grader: `bash .claude/evals/scripts/grade.sh`
- Tests only: `bash .claude/evals/scripts/grade.sh --tests-only`

## Candidate Validation Strategy

- Bootstrap and design work: no build required unless docs expose a factual gap
- Early implementation slices: run the search-focused test filter first
- Integration completion: run the full build and full native test suite
