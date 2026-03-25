# Evaluation Criteria: ui-polish

## Scoring
- Each criterion scored 1-5
- Threshold: minimum score to pass (per criterion)
- Weight: relative importance (higher = more influence on overall verdict)
- Overall FAIL if ANY criterion falls below its threshold

## Criteria

### 1. Icon Distinctiveness
**Weight:** high
**Threshold:** 4
**What good looks like:** Every known folder in Quick Access is instantly identifiable by its icon alone — a user can find "Downloads" without reading the label. Drive types (HDD vs USB) are visually obvious. Icons render crisply at 16px.
**What fails:** Two or more sidebar items share the same icon. Glyphs are too similar to distinguish at a glance (e.g., Documents and Downloads look the same). Icons appear blurry or misaligned in their tiles.

### 2. Theme Color Harmony
**Weight:** high
**Threshold:** 3
**What good looks like:** Each theme has a cohesive feel — backgrounds, borders, text, and accents look intentionally coordinated. Tokyo Night feels distinctly blue/purple. Catppuccin Mocha feels warm and lavender-tinted. Neither looks like "default dark with one color changed." Sufficient contrast between text and backgrounds (WCAG AA minimum 4.5:1 for body text).
**What fails:** Colors clash or feel randomly chosen. Accent color is hard to see against the background. Text is hard to read. Theme looks like a tinted version of another theme rather than its own identity.

### 3. Theme Switching UX
**Weight:** medium
**Threshold:** 3
**What good looks like:** Dropdown is easy to find in the title bar without instructions. Clicking a theme applies it instantly with no flicker, flash, or partial state. Current theme is clearly indicated. The control doesn't crowd or misalign existing title bar elements.
**What fails:** Theme change causes visible flicker or requires restart. Dropdown is hidden or hard to discover. Current theme isn't indicated. Title bar layout breaks or elements overlap.

### 4. Visual Consistency Across Themes
**Weight:** medium
**Threshold:** 3
**What good looks like:** All UI elements (sidebar, file list, tabs, breadcrumb, status bar, context menus) respond to theme changes. No element is left with the old theme's colors. Borders, dividers, and hover states all shift cohesively.
**What fails:** Some panels stay in default dark while others change. Progress bars, selection highlights, or hover states retain old colors. Hardcoded `SolidBrush()` calls create color islands that don't match the active theme.

### 5. Default Dark Regression
**Weight:** high
**Threshold:** 4
**What good looks like:** With "Default Dark" selected, the app is pixel-identical to the pre-change version. No colors shift, no layout changes, no new visual artifacts. Users who never touch the theme dropdown see zero difference.
**What fails:** Default Dark looks subtly different from before — slightly different background shade, border color, or text opacity. Theme infrastructure introduces visual side effects even when the default is selected.
