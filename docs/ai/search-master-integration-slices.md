# Slices: search-master-integration

## SMI1: Add pure search helpers on `master`

User stories: 2, 5, 8, 9

Goal:
Add `SearchFilter.h` and `SearchPresentation.h` to `master` with focused tests, proving that the
portable search logic from `ea49541` can live cleanly in the current branch.

Touched area:
- `native/FileScout.Native/Search/SearchFilter.h`
- `native/FileScout.Native/Search/SearchPresentation.h`
- `native/FileScout.Tests/tests/SearchFilterTests.cpp`
- `native/FileScout.Tests/tests/SearchPresentationTests.cpp`
- project files only if new sources must be included

TDD required: Yes

Done criteria:
- helper headers compile on `master`
- search helper tests cover ranking, status strings, subtitles, highlight positions, and open targets
- no `MainWindow` behavior change yet beyond minimum compile glue

Validation:
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=SearchFilter*:*SearchPresentation*`

---

## SMI2: Harden `SearchService` on `master`

User stories: 3, 4, 5, 6, 9

Goal:
Forward-port the safer recursive search worker behavior from `ea49541` without yet rewriting the
full `MainWindow` integration.

Touched area:
- `native/FileScout.Native/Search/SearchService.h`
- `native/FileScout.Native/Search/SearchService.cpp`
- `native/FileScout.Tests/tests/SearchServiceTests.cpp`

TDD required: Yes

Done criteria:
- complete callback can distinguish normal completion from worker failure if the final design still requires it
- local-root checks, generation ownership, and cancellation semantics are covered by tests
- search-service tests avoid hanging the suite and remain deterministic

Validation:
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=SearchService*`

---

## SMI3: Replace inline filter logic in `MainWindow`

User stories: 1, 2, 6, 8

Goal:
Move local filter mode on `master` from ad hoc inline ranking to the new helper contract while
preserving the existing dedicated `SearchTextBox` UX.

Touched area:
- `native/FileScout.Native/MainWindow.xaml.cpp`
- `native/FileScout.Native/MainWindow.xaml.h` only if helper declarations are needed

TDD required: Yes for helper-facing behavior; regression-first for UI-adjacent code

Done criteria:
- `SearchTextBox_TextChanged` uses `BuildFilterResults`
- snapshot restore and filtered row publication still go through the current row pipeline
- query `< 2` behavior and score-tie ordering match helper tests

Validation:
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=SearchFilter*:*SearchPresentation*:*SearchService*`

---

## SMI4: Align recursive search UI with helper contracts

User stories: 3, 4, 5, 6, 7, 8

Goal:
Integrate the hardened worker and presentation helpers into the recursive search path on `master`.

Touched area:
- `native/FileScout.Native/MainWindow.xaml.cpp`
- `native/FileScout.Native/MainWindow.xaml`
- `native/FileScout.Native/MainWindow.xaml.h`

TDD required: Regression coverage plus manual validation

Done criteria:
- recursive progress and completion text come from `SearchPresentation`
- recursive rows use helper-shaped subtitles and remain on `FileRowViewModel`
- double-click navigate-on-select uses `TryBuildSearchOpenTarget`
- progress bar and read-only search box state reset correctly on cancellation paths

Validation:
- full search-focused tests
- canonical build

---

## SMI5: Manual acceptance and initiative closeout

User stories: 1, 3, 4, 5, 6, 7

Goal:
Verify the integrated search behavior in the real WinUI host and finalize the initiative docs.

Touched area:
- `docs/ai/search-master-integration-status.md`
- any manual acceptance notes created during verification

TDD required: No new code; manual acceptance required

Done criteria:
- live app confirms filter, recursive search, cancellation, and navigate-on-select behavior
- initiative status is updated with final validation evidence

Validation:
- canonical build
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no`
- live host run
