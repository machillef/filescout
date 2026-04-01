# FileScout File List Shell Fixes — Design Spec

**Date:** 2026-04-01
**Scope:** Focused shell-behavior and list-row polish slice covering Properties launch, executable icons, and list-view metadata readability.
**Baseline:** Existing WinUI 3 / C++/WinRT app in `native/FileScout.Native/`
**Target OS:** Windows 11 x64 (minimum, unpackaged)

---

## Summary of Decisions

| Area | Decision |
|---|---|
| Properties command | Keep single-select behavior and replace the fragile shell-verb path with a more reliable shell-native Properties-sheet invocation |
| Executable icons | Preserve fast enumeration, but allow executable files to resolve and cache their real per-file icon instead of collapsing to a generic extension icon |
| Metadata styling | Apply the approved "A" direction only in list view: subtle Explorer-like emphasis for `Type` and `Size` without changing column layout |
| Scope control | No card/group visual redesign, no global shell metadata rewrite, no broad enumeration rework |

---

## 1. Scope

This slice fixes two concrete shell bugs and one targeted readability issue in the file list.

### Included
- Make right-click `Properties` reliably open the Windows Properties sheet for a single selected item
- Restore native-looking executable icons so `.exe` items do not show the generic file icon
- Improve readability of `Type` and `Size` in list view so they are easier to scan while staying calmer than `Name`
- Keep the existing sort columns, selection model, and status-bar messaging pattern

### Explicitly not included
- No multi-select Properties sheet work
- No redesign of card view or grouped row presentation
- No new row-model data shape unless a tiny helper is required
- No full shell-metadata overhaul for all file types
- No large performance refactor of directory enumeration
- No context-menu redesign beyond making the existing command work

### Success bar
- `Properties` opens a real Properties sheet instead of failing silently
- `.exe` rows show a real app icon in normal list loading paths
- `Type` and `Size` are more legible in list view, but the row still feels Explorer-like rather than heavily decorated

---

## 2. Architecture

This remains a narrow slice built on the current shell/service boundaries.

- The command entry point stays in `MainWindow.xaml.cpp`, where `properties` already routes through `ShellFileOperationService`.
- The implementation change happens in `ShellFileOperationService`: success should mean the Properties sheet is actually shown for the selected shell item, not merely that `ShellExecuteW(..., "properties", ...)` returned a non-error code.
- Icon extraction remains centered in `ShellInteropService`, preserving the current fast-enumeration path and cache structure.
- List-row visibility changes stay in the `ListRowTemplate` in `MainWindow.xaml`; they should not require card/group template churn.

This preserves the existing division of responsibilities:
- `MainWindow.xaml.cpp` handles selection rules and status text
- `ShellFileOperationService` performs shell-backed file actions
- `ShellInteropService` controls icon lookup and caching
- `MainWindow.xaml` controls list-row visual hierarchy

---

## 3. Component Behavior

### A. Properties command

- Keep the current single-selection guard.
- When the user invokes `Properties`, the command handler should still reject zero-selection and multi-selection with a status-bar message.
- For a valid single item, the shell service should use a Properties-specific shell API path that is more reliable than a generic verb launch.
- If Windows rejects the request or the item path is invalid, the failure should surface as the existing status-bar failure message rather than a silent no-op.

### B. Executable icon resolution

- Preserve the current fast first paint for directory enumeration.
- Keep extension-level caching for ordinary file types where it is already appropriate.
- Treat executable files as a special case so their icon can be resolved from the actual file path rather than the shared `"file.exe"` fallback pattern.
- Cache executable icon results with a per-item identity that avoids every executable sharing one generic icon result.
- Avoid widening this into a per-file icon policy for every extension; the scope is to make executable-like files look correct while leaving the general optimization strategy intact.

### C. List-view metadata visibility

- Apply the approved "A" direction only to list view.
- Keep the current four data columns: `Name`, `Type`, `Size`, `Date Modified`.
- Make `Type` slightly more visible with restrained Explorer-like emphasis rather than a loud tag treatment.
- Make `Size` a touch stronger in weight/contrast so it scans more like a useful datum and less like faint filler text.
- Keep `Date Modified` and `Name` as the main visual anchors.
- Do not change the underlying strings for `TypeLabel` and `SizeText` unless a tiny formatting helper materially improves consistency.

---

## 4. Error Handling

### Properties
- If no valid single item is selected, keep the existing command-level guard message.
- If shell invocation fails, propagate that failure out of the shell service so the command handler can show a failure status instead of a false success message.
- Do not crash or mutate selection state on failure.

### Icons
- If per-file executable icon extraction fails, fall back safely to the existing generic icon behavior rather than leaving the row blank.
- Do not introduce synchronous per-item shell work that blocks the UI for all file types.

### Styling
- Visual tweaks must preserve truncation, selection contrast, and row alignment with the sort header.

---

## 5. Testing and Verification

### Automated coverage
- Add or update tests around the Properties-launch service seam if it can be exercised deterministically.
- Add or update shell interop tests to protect the intent that executable files do not collapse into the shared generic-extension icon strategy.
- Keep existing row formatting tests intact unless the presentation change introduces a new reusable formatting helper.

### Manual verification
- Right-click a file and choose `Properties`; verify the Windows Properties sheet opens.
- Confirm the single-select guard still blocks multi-select Properties with the existing status pattern.
- Open a directory containing multiple `.exe` files with distinct embedded icons; verify they no longer all appear as the same generic file icon.
- Verify list view readability in both themes: `Type` and `Size` should be easier to scan, remain aligned with headers, and preserve selection readability.

---

## 6. Guardrails

- Do not broaden this into multi-select Properties behavior
- Do not replace the fast directory enumeration path with per-item shell metadata lookups
- Do not redesign cards or grouped rows in this slice
- Do not turn `Type` or `Size` into dominant badge-like UI
- Do not change unrelated context-menu behavior while fixing `Properties`

---

## 7. Success Criteria

- [ ] Single-select `Properties` opens the Windows Properties sheet reliably
- [ ] Failure cases report through the status bar instead of appearing to do nothing
- [ ] `.exe` rows show native app icons in standard list loading flows
- [ ] Existing enumeration behavior for ordinary files stays performant
- [ ] List-view `Type` and `Size` are more readable while staying visually restrained
- [ ] Card and group presentations remain unchanged by this slice
