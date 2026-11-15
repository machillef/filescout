# Bug ledger — "Do not port as is" items of the three specs

Source: `docs/ai/rust-migration/spec-domain.md` §5, `spec-shell.md` §5.2, `spec-ui.md` Part 1 §5 and
Part 2 "Να μη μεταφερθούν ως έχουν" (local, git-ignored; line refs are at commit `e12f7cb`).
Those sections were written for a Rust port, so many items are design notes ("use an enum in Rust"),
not defects. Each item gets one verdict:

- **bug** — wrong observable behaviour, crash or undefined behaviour. Fixed with a test that failed
  first; the PR and test are named in the row.
- **not a bug** — the reason is written in the row. Design debt that is still worth doing is marked
  *debt* and may be cleaned up by a later PR, but nothing user-visible depends on it.

Status: `open` until a PR is merged, then the PR number.

## spec-domain §5

| ID | Item | Verdict | Fix / reason | Status |
|---|---|---|---|---|
| D-01 | `ClipboardService` interface with one implementation, `PasteInto` outside it | not a bug | Design debt only: the concrete type is used directly and works. No behaviour depends on the interface. | — |
| D-02 | `PaneSelectionState`: unread `m_anchor`, `ReplaceWith(empty)` leaves an unselected anchor, focus fallback = first element of an `unordered_set` | bug | Nondeterministic focus after deselect; empty id must clear. Fix: insertion-ordered selection, deterministic fallback, empty id clears. | open |
| D-03 | `TabState::selected_item_index` duplicates `selection` | not a bug | Debt: kept in sync at every write site; no divergence reproduced. Removed only if a later PR touches all sites. | — |
| D-04 | `ShellItemRef` carries the icon bitmap and a preformatted date | perf | Not a correctness bug; the per-item bitmap copies are a memory cost handled by the perf work (perf.md). | open |
| D-05 | `BatchRenamePlan` 3-arg overload used only by tests; five `Build*Plan` loops | not a bug | Duplication, all paths tested and correct. | — |
| D-06 | Five case-fold helpers, three trim definitions | see D-24 | The trims differ only in which whitespace they strip from user input; none produces a wrong file name (Windows rejects leading/trailing spaces anyway via `FileNameValidation`). The case-fold copies share the locale bug in D-24, fixed there. | open |
| D-07 | `Wave1FeatureGate` unknown-value branches | not a bug | Defensive `default:` branches on a C++ enum; C++ cannot prove exhaustiveness, so they stay. | — |
| D-08 | `std::sort` not stable in `CommandRegistry` and `ShellItemSort` | bug | Rows with equal sort keys (same size, same date) change order on every re-sort/refresh. Fix: stable sort with name tiebreak. | open |
| D-09 | Drive root gets icon key `__folder__` → shared cache slot with folders | bug | `C:\` shows the folder icon or every folder shows the drive icon, whichever loaded first. Fix: distinct drive-root key. | open |
| D-10 | `ThemeService`: `LOCALAPPDATA` ≥ 260 chars gives a relative path; `create_directories` throws | bug | Fix: size-correct env read, non-throwing directory creation. | open |
| D-11 | Search hit subtitle stored in `kind_label`; all hits marked as files | bug | Folders in recursive search results get file icon/behaviour. Fix: carry `is_folder` from the search. | open |
| D-12 | The 200-result limit written twice | not a bug | Same value in both places; folded into one constant when D-11 touches the file. | — |
| D-13 | `FindSearchHighlightPositions` unused in production | not a bug | Tested helper with no caller; harmless. | — |
| D-14 | Test `CutOpacity_IsFourWhenCutFlagSet` checks only the default 1.0 | bug (test) | Fix the test to set the cut flag and expect 0.4. | open |
| D-15 | `RowIconState` depends on WinRT `ImageSource` | not a bug | Layering note for a Rust port; C++ domain may use the type. | — |
| D-16 | `NavigationInput` takes raw Win32 messages | not a bug | Same as D-15. | — |
| D-17 | `DragDropPlanning` enum always returns `Deferred`; `Immediate` branch dead | not a bug | Dead code, no behaviour. | — |
| D-18 | Fuzzy "ratio penalty" is 0/1; greedy match is not optimal | not a bug | Ranking quality is a product choice; results are correct matches. | — |
| D-19 | Command ids are strings shared by palette, menu and gate | not a bug | Typo risk only; all ids are covered by `CommandRegistryTests`. | — |
| D-20 | Batch-rename start number: `wcstoul` accepts `+5`, wraps `-1` to 4294967295 | bug | Fix: strict digit-only parse with overflow check. | open |
| D-21 | Unknown case index → Lowercase in UI, TitleCase in domain | bug | Two defaults for one choice. Fix: one mapping in the domain. | open |
| D-22 | `iswalpha`/`towlower` depend on the C locale | bug | The app never calls `setlocale`, so `towlower` folds ASCII only: fuzzy search, type-ahead and name sort are case-sensitive for Greek and other non-ASCII names. Fix: Unicode case folding via `LCMapStringEx`/`CompareStringOrdinal`. | open |

## spec-shell §5.2

| ID | Item | Verdict | Fix / reason | Status |
|---|---|---|---|---|
| S-01 | Mixed COM apartments: MTA per call, STA on pool threads without a pump, workers without init | bug | COM on threads that never called `CoInitializeEx` relies on the implicit MTA. Fix: an apartment guard on every worker. | open |
| S-02 | Comment claims `IFileOperation` runs `CLSCTX_LOCAL_SERVER` | bug (comment) | Corrected with S-03. | open |
| S-03 | `PerformOperations` on the UI thread (paste, drop, create, rename, batch rename) | bug | The window freezes during a long copy. Fix: a dedicated shell STA thread. | open |
| S-04 | `FOFX_ADDUNDORECORD` without an Undo command | not a bug | Kept on purpose: the record lets Explorer's Ctrl+Z undo FileScout operations, the same as other shell file managers. | — |
| S-05 | Cancel detected by comparing an error string | bug | Breaks on any message change or localisation. Fix: typed canceled result. | open |
| S-06 | `hresult_error` from batch rename escapes without action/path | bug | Fix: convert to the same error type as other operations. | open |
| S-07 | Dead `BuildShellItemRef` slow path | not a bug | Dead code; removed in a cleanup PR. | open |
| S-08 | Two date formats: fixed en-US in the fast path, locale in the shell path | bug | A Greek-locale user sees US dates in the list. Fix: one formatter using the user locale. | open |
| S-09 | `TrackPopupMenu` without `SetForegroundWindow`/`WM_NULL`; ANSI `GCS_VERB` with 256 buffer | bug | Menu may not dismiss on outside click; verbs truncated/mangled. Fix both. | open |
| S-10 | Drag-staging and archive temp files never deleted | bug | `%TEMP%` grows forever. Fix: delete staging dirs older than a day at startup. | open |
| S-11 | Search `Start` detaches the previous worker | bug | A detached worker can post into a destroyed pane (use-after-free). Fix: cancel and join. | open |
| S-12 | ShellExt: placeholder CLSID, wrong fallback layout, `MAX_PATH` buffers | bug | Fix in the default-file-manager work (real CLSID or a static verb). | open |
| S-13 | Default-manager tests expect `FileScout.Native.exe`, binary is `FileScoutNative.exe` | bug | Fix: compare against the running executable's path. | open |
| S-14 | Closed gates (archive, batch rename) still run code on each navigation | perf | Measured with the perf work; removed if it shows up. | open |

## spec-ui Part 1 §5

| ID | Item | Verdict | Fix / reason | Status |
|---|---|---|---|---|
| UA-01 | Dead `LOCATION` placeholder text in the chrome | bug | Visible dead text. Removed in the new look. | open |
| UA-02 | `StatusIconTextBlock` never gets a glyph | bug | Empty reserved space. Removed or used in the new look. | open |
| UA-03 | `PaneContext` re-entrancy flags | not a bug | They guard real re-entrant `ListView`/`TabView` events in XAML; they are needed in C++/WinUI. | — |
| UA-04 | Load in two batches (150 + `ReplaceAll`) | perf | Replaced by the virtualised row source (perf.md). | open |
| UA-05 | Filter ignores typing during a load, with no indication | bug | Fix: apply the filter to arriving items and again at the end. | open |
| UA-06 | Whole chrome rebuilt on theme change | perf | Handled by the new look's theme resources. | open |
| UA-07 | `LookupBrush` falls back to light grey in the dark theme | bug | Fix: theme resources only; test that every looked-up key exists in both themes. | open |
| UA-08 | Hard-coded drive colours | bug | Colour islands outside the theme. Tokens in the new look. | open |
| UA-09 | `UnhandledException` marked handled → app continues in unknown state | bug | Fix: log, show a dialog, terminate. | open |
| UA-10 | Windows App SDK bootstrap and its dialog | not a bug | Required by a framework-dependent C++ WinUI app; the Rust note does not apply. | — |
| UA-11 | `%TEMP%` staging for external drags | see S-10 | Staging stays (the WinRT `StorageFile` path needs it); its leak is fixed in S-10. | open |
| UA-12 | Two near-identical context-menu handlers | not a bug | Duplication; same behaviour in both. | — |
| UA-13 | Two near-identical icon backfills | perf | Unified by the perf work. | open |
| UA-14 | XAML comment says palette is collapsed outside beta | bug (comment) | Fix the comment. | open |
| UA-15 | WinUI animations | not a bug | Rust-port note; WinUI renders them itself. | — |
| UA-16 | `m_lastContextMenuPoint` stored on the window | see UB-15 | Its real defect is the (160,160) fallback, fixed in UB-15. | open |

## spec-ui Part 2 "Να μη μεταφερθούν ως έχουν"

| ID | Item | Verdict | Fix / reason | Status |
|---|---|---|---|---|
| UB-01 | Delete comment lies about `CLSCTX_LOCAL_SERVER` | = S-02 | | open |
| UB-02 | Modal shell calls on the UI thread | = S-03 | Open With / Properties / overflow menu are modal by design (they need the owner window); file operations move off the UI thread. | open |
| UB-03 | `CoInitializeEx(MTA)` per archive task | = S-01 | | open |
| UB-04 | F2 renames a file while typing in the search box | bug | Fix + test of the shortcut-ownership rule. | open |
| UB-05 | Ctrl+A handled twice | bug | Fix: one binding. | open |
| UB-06 | `tab_switching` / `m_initComplete` flags | not a bug | = UA-03. | — |
| UB-07 | `InitPane` temporarily changes `m_activePaneIndex` | bug | Async continuations started inside read the wrong pane if they run before restore. Fix: pass the pane index. | open |
| UB-08 | Tab state copied in three places | not a bug | Debt; the copies are consistent today. | — |
| UB-09 | Rows rebuilt on every change | perf | Perf work. | open |
| UB-10 | Sidebar rebuilt with sync icons and disk capacity on every navigation | perf/bug | Navigation stalls on slow/network drives. Fix: rebuild only when favourites/drives change. | open |
| UB-11 | Recents always read the left pane | bug | Fix: read the active pane; test. | open |
| UB-12 | Recycle Bin click pushes the new path onto the back stack | bug | Back from the Recycle Bin does not return to the previous folder. Fix + test. | open |
| UB-13 | Hard-coded `#0078D4` / `#C42B1C` | bug | = UA-08, theme tokens. | open |
| UB-14 | "Rename works one item at a time in the closed beta" message | not a bug | Shown only while the batch-rename gate is off, which is the intended message for that state. | — |
| UB-15 | Overflow menu opens at (160,160) without a click point | bug | Fix: anchor at the focused row. | open |
| UB-16 | Address box re-focused on the next tick | not a bug | Works around a real WinUI focus steal; removing it reintroduces the bug. | — |
| UB-17 | Case-only rename is skipped | bug | Explorer allows `foo.txt` → `Foo.txt`. Fix + test. | open |
| UB-18 | Type-ahead: one letter, A–Z only, over `pane.items` not displayed rows | bug | Fix: buffered type-ahead over displayed rows, any character. | open |
| UB-19 | `ShowDeleteDialogAsync(item)` wrapper | not a bug | A one-line overload. | — |
| UB-20 | Batch rename dialog built imperatively in 400 lines | not a bug | Debt; behaviour is correct. | — |
