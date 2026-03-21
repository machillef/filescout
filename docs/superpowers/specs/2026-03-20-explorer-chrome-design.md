# FileScout Explorer Chrome — Design Spec

**Date:** 2026-03-20
**Scope:** Explorer-inspired shell chrome pass focused on top window chrome and left sidebar. Theme-aware visual work with narrowly bounded wiring changes required for custom title-bar integration; no feature expansion.
**Baseline:** Existing WinUI 3 / C++/WinRT app in `native/FileScout.Native/`
**Target OS:** Windows 11 x64 (minimum, unpackaged)
**Visual references:**
- User-approved layout source: `C:\Users\machi\source\repos\filescout\.superpowers\brainstorm\2171-1773962558\full-design-v2.html`
- User screenshot reference: `C:\Users\machi\Pictures\windows-explorer-icons.png`
- User screenshot reference: session image comparing Explorer top chrome vs FileScout top chrome on 2026-03-20

---

## Summary of Decisions

| Area | Decision |
|---|---|
| Primary goal | Make FileScout feel materially closer to Windows Explorer at a glance, especially in the top chrome |
| Top chrome | Replace the detached toolbar card with integrated custom title-bar chrome while keeping existing pane tabs in place |
| Layout direction | Explorer-inspired, not Explorer-identical |
| Sidebar | Restyle left sidebar closer to Explorer with flatter rows and more native-feeling icons |
| Theme behavior | Theme-aware in both dark and light modes |
| File list icons | Keep existing native shell-extracted icons |
| Toolbar behavior | Preserve current behaviors where possible; this slice is mainly structural and visual |
| Performance work | Out of scope for this slice |

---

## 1. Scope

This slice is a focused Explorer-chrome pass, not a full application redesign.

### Included
- Integrate the app’s top content into a real custom title bar
- Redesign the full top area so the title-bar region and existing address/command region read as one continuous shell
- Remove the detached bordered-toolbar feel that currently makes the app look “embedded inside a window”
- Restyle the left sidebar closer to Explorer with flatter navigation treatment and more native-feeling icons
- Keep the entire result theme-aware

### Explicitly not included
- No full rewrite of the file row layout
- No feature-parity work against Windows Explorer
- No global icon replacement across the entire app
- No performance optimization work
- No context-menu, drag/drop, or file-operation redesign
- No movement of the existing pane `TabView` controls into the window chrome
- No new search box or top-search feature

### Success bar
- The visible white top-frame separation is gone or materially reduced
- The app no longer reads as a custom rounded card sitting under a standard window frame
- The sidebar and top chrome feel recognizably closer to Explorer at a glance

---

## 2. Layout Direction

### Top chrome
- The window should move from “normal title bar + toolbar card underneath” to a custom integrated title bar.
- The topmost region should feel like one shell surface, not a bordered panel inside the window.
- Branding remains present, but it becomes much quieter and less dominant than in the current layout.
- The top bar should use a flatter, tighter composition with less exaggerated rounding across the existing branding, navigation controls, address surface, and top commands.

### Explorer-inspired structure
- The top should feel layered like Explorer: a title-bar region and a calmer navigation/command region, but the two should visually blend into one shell.
- Separation should come mostly from spacing, subtle dividers, and tonal contrast rather than outlined card borders.
- The current rounded card framing around the top row should be removed.
- The existing left/right pane `TabView` controls remain where they are in the content area for this slice.
- The existing address field remains an address field. This slice does not add a separate search surface.

### Left sidebar
- The left sidebar should move away from an app-styled menu and toward a flatter Explorer-style navigation column.
- Icons should feel more like native shell navigation than app glyph chrome.
- Selection fill should be restrained and cleaner than the current treatment.
- Drive/location sections should read as navigation, not mini dashboards.
- The existing selection/details block at the bottom of the left sidebar stays structurally unchanged in this slice.
- That lower block may be visually harmonized so it does not clash with the Explorer-inspired navigation above it, but it is not being redesigned.

### Theme-aware behavior
- Dark mode should borrow Explorer’s darker shell mood.
- Light mode should preserve the same structure and hierarchy without forcing a dark-only treatment.
- The design should rely on theme-aware brushes or controlled per-theme overrides, not a fixed one-theme palette.

---

## 3. Implementation Shape

This should be executed as one focused UI slice with three work areas.

### A. Window and title-bar integration
- Add real custom title-bar integration in `MainWindow.xaml.cpp`.
- Mark a dedicated top region in `MainWindow.xaml` as the draggable title-bar surface.
- Use the existing Mica-backed window as the backdrop foundation; the missing piece is integrated chrome, not a new backdrop system.
- Remove the current detached bordered top container and replace it with integrated shell sections.
- Allowed non-visual plumbing is limited to the title-bar integration wiring needed for dragging, caption-button coexistence, and hit-test-safe interactive controls.
- This slice does not include unrelated input, navigation, or feature behavior changes.

### B. Top-shell restyle
- Rework the top region in `MainWindow.xaml` so it behaves more like Explorer chrome:
  - quieter branding
  - flatter nav buttons
  - existing address surface that blends into the shell
  - less rounded, less outlined containers
- Keep the current application behaviors where practical so this remains mostly a structural and visual rewrite rather than a feature rewrite.
- Keep the current pane tabs in their existing content-area location rather than moving them into the title bar.
- Do not add a new top search box in this slice.

### C. Sidebar Explorer pass
- Replace the current special Fluent-first sidebar icon treatment with one fixed Explorer-inspired rule:
  - favorites and known places use shell-backed or native-style icons when available
  - drives use shell-style drive icons
  - the selection/details block keeps its current icon behavior
- Flatten row styling and restyle drive/location sections so they read as shell navigation.
- Keep file-row icons unchanged and continue to use the existing shell extraction path there.
- Keep the selection/details block structure intact and only harmonize its colors and border treatment if needed.

### Likely touched files
- `native/FileScout.Native/MainWindow.xaml`
- `native/FileScout.Native/MainWindow.xaml.cpp`
- `native/FileScout.Native/MainWindow.xaml.h` only if a small title-bar helper or field is needed

### Verification target
- Solution build passes
- Existing tests pass
- Manual runtime validation confirms the top white-frame separation is gone or clearly reduced
- Manual runtime validation confirms the sidebar/top chrome feel materially closer to Explorer

---

## 4. Structural Assessment of the Current UI

### What currently causes the “external app” feel
- The window already uses `MicaBackdrop`, so the backdrop itself is not the main problem.
- The top area is still built as a rounded bordered container under a standard system title bar.
- That structure creates the exact visual effect shown in the user comparison screenshot: FileScout looks like a custom app placed inside a normal window frame, while Explorer reads as one continuous native surface.
- The issue is therefore primarily about title-bar integration and shell composition, not missing backdrop effects.

### Design implication
- A simple recolor is insufficient.
- Removing only the border color would help, but the app would still feel bolted on.
- The biggest gain comes from integrating the top content into the title bar and flattening the shell styling at the same time.

---

## 5. Icon Direction

The user’s final direction for this slice is not “change all icons everywhere.” It is:
- make the left sidebar feel more like Explorer now
- keep broader icon work out of scope

### Keep as-is
- File list row icons remain native shell icons
- Existing file-type/runtime icon pipeline stays in place
- Toolbar does not need a separate global icon replacement effort in this slice
- The selection/details block keeps its current icon behavior

### Change in this slice
- Left sidebar favorites and known places should use shell-backed or native-style icons when available
- Left sidebar drives should use shell-style drive icons
- Avoid decorative or overly app-like glyph treatment in the sidebar
- Prefer a consistent Explorer-inspired approach over a mixed “app chrome” icon language

This is intentionally a narrower icon decision than a full app-wide icon overhaul.

---

## 6. Non-goals and Guardrails

- Do not add new Explorer commands just because the top chrome is becoming more Explorer-like
- Do not restructure the main file list rows
- Do not replace shell icons in the file pane with custom art or font-only stand-ins
- Do not start performance optimization inside this slice
- Do not lose theme awareness in pursuit of dark-mode fidelity
- Do not move the pane tabs into the title bar in this slice
- Do not add a new search box in this slice
- Do not redesign the lower selection/details block beyond light visual harmonization

This slice should improve first impression and perceived nativeness without broadening into a larger product redesign.

---

## 7. Success Criteria

### Top chrome
- [ ] The detached toolbar-card feeling is removed
- [ ] The visible white top separation shown in the comparison screenshot is gone or materially reduced
- [ ] The top reads as integrated window chrome rather than content below a frame
- [ ] Branding is quieter and no longer dominates the top-left area
- [ ] The existing pane tabs remain in their current content-area position
- [ ] No separate new search UI is introduced

### Sidebar
- [ ] The left sidebar reads closer to Explorer at a glance
- [ ] Sidebar rows feel flatter and more native
- [ ] Sidebar icons feel closer to shell navigation than app glyph chrome
- [ ] Drive/location sections remain readable without looking like mini dashboard cards
- [ ] The lower selection/details block remains structurally unchanged

### Theme behavior
- [ ] Dark mode feels close to Explorer’s darker shell mood
- [ ] Light mode remains coherent and native-looking
- [ ] No fixed dark-only palette is introduced

### Safety
- [ ] Build passes with no new warnings beyond the known exempt WinAppSDK warnings
- [ ] Existing tests continue to pass
- [ ] File-row shell icons remain intact
- [ ] Existing navigation/address/refresh behavior is preserved unless an intentional visual-shell change requires a small wiring adjustment
