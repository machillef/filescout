# FileScout Native Rewrite Slices

## R1: Native Workspace And Shell Scaffold
**Goal:** Create a same-repo WinUI 3 desktop app scaffold with real filesystem
enumeration and initial domain contracts.
**Status:** Complete
**Acceptance:** The native app hosts an unpackaged WinUI 3 shell, loads the
current directory, and renders a placeholder Explorer-style surface.

## R2: Shell-Backed File Pane With Native Icon Extraction And Basic Selection
**Goal:** Turn the placeholder pane into a usable shell-backed file list with
selection, metadata, and native icons.
**Status:** Complete
**Completed sub-slices:** `R2a` selectable shell pane foundation; `R2b` native
icon extraction and richer row visuals
**Follow-up note:** manual validation of the native selection flow is still
recommended before widening behavior-heavy slices
**Depends on:** `R1`

## R3: Windows 11 Style Context Menu Model With Native Overflow
**Goal:** FileScout-owned Windows 11 menu with shell-backed overflow for
unsupported extension cases.
**Status:** Complete
**Completed sub-slices:** `R3a` FileScout-owned context menu foundation; `R3b`
shell-backed overflow / `More Windows actions...` invocation
**Follow-up note:** manual validation of the right-click and overflow flow is
still recommended before widening into shell file operations
**Depends on:** `R2`

## R4: Shell-Backed Open/Open With/Properties/Delete/Rename/Copy/Cut/Paste
**Goal:** File operations use Windows shell semantics first.
**Status:** Complete
**Completed sub-slices:** `R4a` shell-backed open, open with, properties, and
file activation; `R4b` native new-folder, rename, and delete mutations; `R4c`
clipboard-backed `Copy`, `Cut`, and `Paste`
**Follow-up note:** manual validation of file activation, destructive actions,
and clipboard-backed paste flows is still recommended before widening into drag
and drop; the native debug host is currently blocked by a startup abort that
must be resolved before further feature slices
**Depends on:** `R2`, `R3`

## R4d: Native Runtime Stability Fix
**Goal:** Restore a reliably launching unpackaged WinUI 3 host before more
explorer features are added.
**Status:** Complete
**Root cause fixed:** `WindowsAppSDKSelfContained=true` caused a mixed-mode
initialization where `MddBootstrapInitialize2` was a no-op and the MRT resource
namespace was never established; switching to framework-package mode
(`WindowsAppSDKSelfContained=false`) fixed both the XAML resource abort and a
clean-build PCH memory failure (`C3859`/`C1076`)
**Depends on:** `R4c`

## R5: Native Drag/Drop And Clipboard Interop
**Goal:** Native drag/drop and clipboard parity for filesystem items.
**Status:** Complete
**Completed sub-slices:** `R5a` keyboard shortcuts (Ctrl+C/X/V, Del, F2, F5);
`R5b` drop target (DragOver + Drop with GetDeferral + CopyItemsFromPaths);
`R5c` drag source (DragItemsStarting with StorageFile/Folder packaging)
**Follow-up note:** manual validation of in-app drag/drop and cross-app
Explorer interop is recommended before widening into R6
**Depends on:** `R4d`

## R6: Tabs
**Goal:** Multiple tabs per pane in the native shell.
**Status:** Complete
**Completed sub-slices:** `R6` TabView with per-tab ListViews, state persistence,
Ctrl+T / Ctrl+W shortcuts, and last-tab close protection
**Follow-up note:** manual validation of tab open/close/switch and keyboard
shortcuts is recommended before widening into R7
**Depends on:** `R5`

## R7: Dual-Pane Layout
**Goal:** Two independent panes with active-pane routing.
**Status:** Not Started
**Depends on:** `R6`

## R8: Favorites, Sidebar Tree, And Pinned Locations
**Goal:** Real navigation surfaces for places, favorites, and shell locations.
**Status:** Not Started
**Depends on:** `R2`, `R7`

## R9: Sort/Group/View Mode Parity
**Goal:** Explorer-class list behavior for sort, grouping, and presentation.
**Status:** Not Started
**Depends on:** `R2`

## R10: Preview And Inspect Surface
**Goal:** Native preview and metadata inspection.
**Status:** Not Started
**Depends on:** `R2`

## R11: Archive Mode
**Goal:** Browse archives as FileScout-owned virtual views in the native app.
**Status:** Not Started
**Depends on:** `R7`

## R12: Batch Rename
**Goal:** Bring FileScout power-user rename flows into the native shell.
**Status:** Not Started
**Depends on:** `R4`, `R7`

## R13: Command Palette
**Goal:** Global fuzzy command surface in the native host.
**Status:** Not Started
**Depends on:** `R6`

## R14: Git-Aware And Power-User Workflows
**Goal:** Add differentiated workflows after Explorer-parity basics stabilize.
**Status:** Not Started
**Depends on:** `R4`, `R7`, `R13`

## R15: AI Features
**Goal:** Add AI only after the native parity baseline is stable.
**Status:** Not Started
**Depends on:** `R12`, `R13`
