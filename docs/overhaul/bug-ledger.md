# Bug ledger — "Do not port as is" items of the three specs

Source: `docs/ai/rust-migration/spec-domain.md` §5, `spec-shell.md` §5.2, `spec-ui.md` Part 1 §5 and
Part 2 "Να μη μεταφερθούν ως έχουν" (local, git-ignored; line refs are at commit `e12f7cb`).
Those sections were written for a Rust port, so many items are design notes ("use an enum in Rust"),
not defects. Each item gets one verdict:

- **bug** — wrong observable behaviour, crash or undefined behaviour. Fixed with a test that failed
  first; the PR and test are named in the row.
- **not a bug** — the reason is written in the row. Design debt that is still worth doing is marked
  *debt* and may be cleaned up by a later PR, but nothing user-visible depends on it.

Status: the PR that fixes the item (all squash-merged into `master`), or `—` when no change was made.

## spec-domain §5

| ID | Item | Verdict | Fix / reason | Status |
|---|---|---|---|---|
| D-01 | `ClipboardService` interface with one implementation, `PasteInto` outside it | not a bug | Design debt only: the concrete type is used directly and works. No behaviour depends on the interface. | — |
| D-02 | `PaneSelectionState`: unread `m_anchor`, `ReplaceWith(empty)` leaves an unselected anchor, focus fallback = first element of an `unordered_set` | bug | Nondeterministic focus after deselect; empty id must clear. Fix: insertion-ordered selection, deterministic fallback, empty id clears. | #25 |
| D-03 | `TabState::selected_item_index` duplicates `selection` | not a bug | Debt: kept in sync at every write site; no divergence reproduced. Removed only if a later PR touches all sites. | — |
| D-04 | `ShellItemRef` carries the icon bitmap and a preformatted date | perf | Not a correctness bug; the per-item bitmap copies are a memory cost handled by the perf work (perf.md). | #10 |
| D-05 | `BatchRenamePlan` 3-arg overload used only by tests; five `Build*Plan` loops | not a bug | Duplication, all paths tested and correct. | — |
| D-06 | Five case-fold helpers, three trim definitions | see D-22 | The trims differ only in which whitespace they strip from user input; none produces a wrong file name (Windows rejects leading/trailing spaces anyway via `FileNameValidation`). The case-fold copies share the locale bug in D-22, fixed there. | #45 |
| D-07 | `Wave1FeatureGate` unknown-value branches | not a bug | Defensive `default:` branches on a C++ enum; C++ cannot prove exhaustiveness, so they stay. | — |
| D-08 | `std::sort` not stable in `CommandRegistry` and `ShellItemSort` | bug | Rows with equal sort keys (same size, same date) change order on every re-sort/refresh. Fix: stable sort with name tiebreak. | #15 |
| D-09 | Drive root gets icon key `__folder__` → shared cache slot with folders | bug | `C:\` shows the folder icon or every folder shows the drive icon, whichever loaded first. Fix: distinct drive-root key. | #16 |
| D-10 | `ThemeService`: `LOCALAPPDATA` ≥ 260 chars gives a relative path; `create_directories` throws | bug | Fix: size-correct env read, non-throwing directory creation. | #19 |
| D-11 | Search hit subtitle stored in `kind_label`; all hits marked as files | not a bug | `SearchService::WalkDir` never yields a folder: directories are recursed into and skipped, only files are scored. Every hit is a file, so `is_folder = false` is correct; the subtitle in `kind_label` is a presentation choice nothing else reads (#22). | — |
| D-12 | The 200-result limit written twice | not a bug | Same value in both places; folded into one constant when D-11 touches the file. | #22 |
| D-13 | `FindSearchHighlightPositions` unused in production | not a bug | Tested helper with no caller; harmless. | — |
| D-14 | Test `CutOpacity_IsFourWhenCutFlagSet` checks only the default 1.0 | bug (test) | Fix the test to set the cut flag and expect 0.4. | #12 |
| D-15 | `RowIconState` depends on WinRT `ImageSource` | not a bug | Layering note for a Rust port; C++ domain may use the type. | — |
| D-16 | `NavigationInput` takes raw Win32 messages | not a bug | Same as D-15. | — |
| D-17 | `DragDropPlanning` enum always returns `Deferred`; `Immediate` branch dead | not a bug | Dead code, no behaviour. | — |
| D-18 | Fuzzy "ratio penalty" is 0/1; greedy match is not optimal | not a bug | Ranking quality is a product choice; results are correct matches. | — |
| D-19 | Command ids are strings shared by palette, menu and gate | not a bug | Typo risk only; all ids are covered by `CommandRegistryTests`. | — |
| D-20 | Batch-rename start number: `wcstoul` accepts `+5`, wraps `-1` to 4294967295 | bug | Fix: strict digit-only parse with overflow check. | #13 |
| D-21 | Unknown case index → Lowercase in UI, TitleCase in domain | bug | Two defaults for one choice. Fix: one mapping in the domain. | #14 |
| D-22 | `iswalpha`/`towlower` depend on the C locale | bug | The app never calls `setlocale`, so `towlower` folds ASCII only: fuzzy search, type-ahead and name sort are case-sensitive for Greek and other non-ASCII names. Fix: Unicode case folding via `LCMapStringEx`/`CompareStringOrdinal`. | #45 |

## spec-shell §5.2

| ID | Item | Verdict | Fix / reason | Status |
|---|---|---|---|---|
| S-01 | Mixed COM apartments: MTA per call, STA on pool threads without a pump, workers without init | bug | COM on threads that never called `CoInitializeEx` relies on the implicit MTA. Fix: an apartment guard on every worker. | #33 |
| S-02 | Comment claims `IFileOperation` runs `CLSCTX_LOCAL_SERVER` | bug (comment) | Corrected with S-03. | #46 |
| S-03 | `PerformOperations` on the UI thread (paste, drop, create, rename, batch rename) | bug | The window freezes during a long copy. Fix: a dedicated shell STA thread. | #46 |
| S-04 | `FOFX_ADDUNDORECORD` without an Undo command | not a bug | Kept on purpose: the record lets Explorer's Ctrl+Z undo FileScout operations, the same as other shell file managers. | — |
| S-05 | Cancel detected by comparing an error string | bug | Breaks on any message change or localisation. Fix: typed canceled result. | #18 |
| S-06 | `hresult_error` from batch rename escapes without action/path | bug | Fix: convert to the same error type as other operations. | #21 |
| S-07 | Dead `BuildShellItemRef` slow path | not a bug | Dead code; removed in a cleanup PR. | #11 |
| S-08 | Two date formats: fixed en-US in the fast path, locale in the shell path | bug | A Greek-locale user sees US dates in the list. Fix: one formatter using the user locale. | #32 |
| S-09 | `TrackPopupMenu` without `SetForegroundWindow`/`WM_NULL`; ANSI `GCS_VERB` with 256 buffer | bug | Menu may not dismiss on outside click; verbs truncated/mangled. Fix both. | #29 |
| S-10 | Drag-staging and archive temp files never deleted | bug | `%TEMP%` grows forever. Fix: delete staging dirs older than a day at startup. | #28 |
| S-11 | Search `Start` detaches the previous worker | bug | A detached worker can post into a destroyed pane (use-after-free). Fix: cancel and join. | #27 |
| S-12 | ShellExt: placeholder CLSID, wrong fallback layout, `MAX_PATH` buffers | bug | Fix in the default-file-manager work (real CLSID or a static verb). | #34 |
| S-13 | Default-manager tests expect `FileScout.Native.exe`, binary is `FileScoutNative.exe` | bug | Fix: compare against the running executable's path. | #9 |
| S-14 | Closed gates (archive, batch rename) still run code on each navigation | perf, not done | Not on the measured path: in the 100k bench the load stages are enumeration and row building (perf.md); the gate checks are a few comparisons per navigation. | — |

## spec-ui Part 1 §5

| ID | Item | Verdict | Fix / reason | Status |
|---|---|---|---|---|
| UA-01 | Dead `LOCATION` placeholder text in the chrome | bug | Visible dead text. Removed in the new look. | #24 |
| UA-02 | `StatusIconTextBlock` never gets a glyph | bug | Empty reserved space. Removed or used in the new look. | #24 |
| UA-03 | `PaneContext` re-entrancy flags | not a bug | They guard real re-entrant `ListView`/`TabView` events in XAML; they are needed in C++/WinUI. | — |
| UA-04 | Load in two batches (150 + `ReplaceAll`) | perf | Kept: #17 publishes the first rows one frame before the full list and builds rows in parallel; the 150-row first batch is what makes first rows land before the full list (perf.md). | #17 |
| UA-05 | Filter ignores typing during a load, with no indication | bug | Fix: apply the filter to arriving items and again at the end. | #40 |
| UA-06 | Whole chrome rebuilt on theme change | perf, not done | Theme change is a rare, user-initiated action, not on a measured path; the new look (#24) moved colours to theme resources, the rebuild stays. | — |
| UA-07 | `LookupBrush` falls back to light grey in the dark theme | bug | Fix: theme resources only; test that every looked-up key exists in both themes. | #41 |
| UA-08 | Hard-coded drive colours | bug | Colour islands outside the theme. Tokens in the new look. | #24 |
| UA-09 | `UnhandledException` marked handled → app continues in unknown state | bug | Fix: log, show a dialog, terminate. | #42 |
| UA-10 | Windows App SDK bootstrap and its dialog | not a bug | Required by a framework-dependent C++ WinUI app; the Rust note does not apply. | — |
| UA-11 | `%TEMP%` staging for external drags | see S-10 | Staging stays (the WinRT `StorageFile` path needs it); its leak is fixed in S-10. | #28 |
| UA-12 | Two near-identical context-menu handlers | not a bug | Duplication; same behaviour in both. | — |
| UA-13 | Two near-identical icon backfills | perf, not done | Debt: both backfills go through the same `ShellInteropService` icon cache, and since #10 cached pixels are shared, so nothing is fetched or stored twice; merging the two loops would be cleanup, not a speed-up. | — |
| UA-14 | XAML comment says palette is collapsed outside beta | bug (comment) | Fix the comment. | #24 |
| UA-15 | WinUI animations | not a bug | Rust-port note; WinUI renders them itself. | — |
| UA-16 | `m_lastContextMenuPoint` stored on the window | see UB-15 | Its real defect is the (160,160) fallback, fixed in UB-15. | #48 |

## spec-ui Part 2 "Να μη μεταφερθούν ως έχουν"

| ID | Item | Verdict | Fix / reason | Status |
|---|---|---|---|---|
| UB-01 | Delete comment lies about `CLSCTX_LOCAL_SERVER` | = S-02 | | #46 |
| UB-02 | Modal shell calls on the UI thread | = S-03 | Open With / Properties / overflow menu are modal by design (they need the owner window); file operations move off the UI thread. | #46 |
| UB-03 | `CoInitializeEx(MTA)` per archive task | = S-01 | | #33 |
| UB-04 | F2 renames a file while typing in the search box | bug | Fix + test of the shortcut-ownership rule. | #37 |
| UB-05 | Ctrl+A handled twice | bug | Fix: one binding. | #37 |
| UB-06 | `tab_switching` / `m_initComplete` flags | not a bug | = UA-03. | — |
| UB-07 | `InitPane` temporarily changes `m_activePaneIndex` | bug | Async continuations started inside read the wrong pane if they run before restore. Fix: pass the pane index. | #43 |
| UB-08 | Tab state copied in three places | not a bug | Debt; the copies are consistent today. | — |
| UB-09 | Rows rebuilt on every change | perf | Reduced: #17 builds row view models in parallel and #10 shares icon pixels; a full rebuild per change remains and is within the perf targets (perf.md). | #17 |
| UB-10 | Sidebar rebuilt with sync icons and disk capacity on every navigation | perf/bug | Navigation stalls on slow/network drives. Fixed: the drive list (with free space) is read once and reloads on a volume change or Refresh. Open: that read still runs on the UI thread at startup and Refresh. | #47 |
| UB-11 | Recents always read the left pane | bug | Fix: read the active pane; test. | #35 |
| UB-12 | Recycle Bin click pushes the new path onto the back stack | bug | Back from the Recycle Bin does not return to the previous folder. Fix + test. | #31 |
| UB-13 | Hard-coded `#0078D4` / `#C42B1C` | bug | = UA-08, theme tokens. | #24 |
| UB-14 | "Rename works one item at a time in the closed beta" message | not a bug | Shown only while the batch-rename gate is off, which is the intended message for that state. | — |
| UB-15 | Overflow menu opens at (160,160) without a click point | bug | Fix: anchor at the focused row. | #48 |
| UB-16 | Address box re-focused on the next tick | not a bug | Works around a real WinUI focus steal; removing it reintroduces the bug. | — |
| UB-17 | Case-only rename is skipped | bug | Explorer allows `foo.txt` → `Foo.txt`. Fix + test. | #26 |
| UB-18 | Type-ahead: one letter, A–Z only, over `pane.items` not displayed rows | bug | Fix: buffered type-ahead over displayed rows, any character. | #38 |
| UB-19 | `ShowDeleteDialogAsync(item)` wrapper | not a bug | A one-line overload. | — |
| UB-20 | Batch rename dialog built imperatively in 400 lines | not a bug | Debt; behaviour is correct. | — |
