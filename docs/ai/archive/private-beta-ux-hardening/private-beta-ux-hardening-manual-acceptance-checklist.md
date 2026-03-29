# Manual Acceptance Checklist: private-beta-ux-hardening

Last updated: 2026-03-28

## Fixed checklist

- Launch the Debug x64 build and confirm the app starts without a bootstrap or startup error dialog.
- From `C:\Users\machi`, search for `U4Sandbox-20260328-202222`, then double-click the visible filtered row and confirm navigation opens `C:\Users\machi\U4Sandbox-20260328-202222` rather than a hidden underlying row target.
- In a sparse folder, right-click below the last visible row and confirm the background-capable context menu opens with `New folder` and `Refresh`.
- In a sparse folder, right-click between two visible rows and confirm the pane still owns the gap hit target well enough to open the background-capable context menu.
- Open `C:\Users\machi\U4Sandbox-20260328-202222\PaneA\Empty`, right-click the empty folder surface, and confirm the background menu appears with `New folder` and `Refresh`.
- Right-click a concrete item and confirm the menu still exposes item-aware commands such as `Open`, `Rename`, `Delete`, `Copy`, `Cut`, or `Properties`.
- On a fresh launch where `Back` and `Forward` are disabled, click or double-click those disabled buttons and confirm the window does not maximize/minimize and the breadcrumb path does not change.
- Check the pane and sidebar at a reduced-height window size and confirm the scroll surfaces remain discoverable without needing a hover-first affordance.

## Latest run: 2026-03-28

- Launch/startup: PASS
- Filtered-search visible-row navigation: PASS
  Evidence: `C:\Users\machi\AppData\Local\Temp\filescout-u4-final\search-filter.png`, `C:\Users\machi\AppData\Local\Temp\filescout-u4-final\search-filter-opened-correct-folder.png`
- Sparse-folder blank-space menu below last row: PASS
  Evidence: `C:\Users\machi\AppData\Local\Temp\filescout-u4-final\blank-space-context-menu.png`
- Sparse-folder gap hit target between rows: PASS
  Evidence: runtime UIA probe after right-clicking the row gap returned menu commands including `New folder` and `Refresh`
- Empty-folder background behavior: PASS
  Evidence: `C:\Users\machi\AppData\Local\Temp\filescout-u4-final\empty-folder.png`, `C:\Users\machi\AppData\Local\Temp\filescout-u4-final\empty-folder-context-menu.png`
- Item-aware context menu ownership: PASS
  Evidence: `C:\Users\machi\AppData\Local\Temp\filescout-u4-final\item-context-menu.png`
- Disabled custom-chrome navigation buttons: PASS
  Evidence: `C:\Users\machi\AppData\Local\Temp\filescout-u4-final\disabled-nav-buttons.png`; runtime check confirmed `BackEnabled=false`, `ForwardEnabled=false`, unchanged breadcrumb, `IsZoomed=false`, `IsIconic=false`
- Scrollbar discoverability: PASS with note
  Evidence: `C:\Users\machi\AppData\Local\Temp\filescout-u4-final\small-window-scrollbars.png`, `C:\Users\machi\AppData\Local\Temp\filescout-u4-final\small-window-scrollbars-340h.png`
  Note: WinUI UI Automation did not expose the rendered scrollbars as visible child elements during the reduced-height pass, but the live app still retained the intended always-discoverable configuration for the sidebar `ScrollViewer` in `MainWindow.xaml` and the pane list `ScrollViewer` in `MainWindow.xaml.cpp`.
