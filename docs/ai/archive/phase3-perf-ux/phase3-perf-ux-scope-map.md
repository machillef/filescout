# Scope Map: phase3-perf-ux

## Problem Statement

After UX+Perf Sprint Round 2 (S1–S7), manual testing confirmed:
1. System32 (~5120 items) loads in ~2s vs Windows Explorer's near-instant. Root cause: XAML object
   creation without ListView virtualization (8–13 objects × 5120 items = ~50k XAML objects on UI thread).
2. Breadcrumbs (FontSize=12, 38px toolbar) are too small — feel cramped.
3. 24 known UX issues persist; 8 are critical functionality regressions.
4. Sidebar is visually dated; lacks collapsible Quick Access / This PC / Bookmarks sections.

## In-Scope

| Track | Description |
|---|---|
| P (Performance) | Replace `CreateCardRow`/`CreateListRow` with XAML DataTemplates + `FileRowViewModel` (INPC) |
| U (UX) | Breadcrumb size; 8 critical UX bug fixes |
| S (Sidebar) | Three-section collapsible sidebar: Quick Access, This PC, Bookmarks |

## Adjacent (Understand, Don't Touch)

| Area | Contract |
|---|---|
| `ShellInteropService::GetItemIcon` | Thread-safe icon lookup; used by `BackfillIconsAsync` — interface unchanged |
| `SearchService` / search results | `CreateSearchRowView` returns `IInspectable`; must remain compatible with search path |
| `FavoritesService` | Used by S4 (Bookmarks); only `BuildSidebarFavorites` wiring changes |
| `TabState`, `PaneContext` | Data model unchanged in P-track; `tab_rows` vector type changes |
| `PaneLoadTiming` | Timing struct — unchanged, used for perf measurement |

## Out of Scope

- Archive/zip browsing
- Network paths
- Thumbnail image previews in the preview panel
- Non-critical UX issues not listed in slices (Round 1 #2, 4, 5, 6, 7, 8, 9, 10, 11, 16; Round 2 #2, 3, 5b)
- macOS/Linux

## Dependents on Target Area

| Dependent | Risk |
|---|---|
| `BackfillIconsAsync` | Rewritten in P4 — uses ViewModel.IconSource instead of SetAt |
| `RebuildAllTabRowsForPane` | Updated in P3 — regenerates ViewModels not XAML objects |
| Search result rows (`CreateSearchRowView`) | Must continue working after P3 row-creation overhaul |
| Group headers in grouped view | Custom separator rows — must coexist with FileRowViewModel items |

## Definition of Done

1. System32 (~5120 items) first-batch rows visible in < 800ms on a warm navigate
2. Breadcrumbs are comfortably readable; not cramped in the toolbar
3. Double-click opens folder, Delete triggers delete dialog, Ctrl+T opens new tab
4. Three-section sidebar (Quick Access, This PC, Bookmarks) visible on launch

## Verification Commands

```
# Build
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal"

# Tests
native\bin\x64\Debug\Tests\FileScout.Tests.exe

# Manual host launch
native\x64\Debug\FileScout.Native\FileScoutNative.exe
```
