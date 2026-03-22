# Manual Smoke Checklist

Use a disposable sandbox folder created by `scripts/new-ui-sandbox.ps1`.

## Launch

- App starts without a bootstrap or startup error dialog.
- Main window renders both panes.

## Navigation and pane routing

- Open the sandbox root in one pane and a child folder in the other.
- Use `Ctrl+Tab` to switch active panes.
- Confirm navigation actions affect only the active pane.

## Tabs

- `Ctrl+T` opens a new tab in the active pane.
- `Ctrl+W` closes the active tab without affecting the other pane.

## Refresh and selection

- `F5` refreshes the current folder.
- Selecting files or folders updates the active pane state without disturbing the other pane.

## Context menu

- Right-click an item and confirm the Windows 11 style context menu appears.
- Confirm the shell overflow entry is available when expected.

## Safe file operations inside the sandbox

- Create a new folder.
- Rename a file or folder.
- Copy and paste between sandbox folders.
- Cut and paste between sandbox folders.
- Delete an item and confirm it goes to the recycle bin behavior you expect for shell-backed deletes.

## Drag and drop

- Drag a file between folders or panes inside the sandbox.
- Confirm the file operation lands in the intended target only once.

## Reporting

- Record the active pane, selected item, exact step, expected result, actual result, and whether the issue is deterministic.
