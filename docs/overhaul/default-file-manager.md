# FileScout as the default file manager

Status: done and shown on video on win1 (2026-11-15).

## How to switch

- **In the app:** open the Theme menu (title bar) and choose **Make FileScout the default file manager**.
  The same menu item then reads **Restore Explorer as default file manager**. The command palette
  has both too (`Make FileScout the Default File Manager`, `Restore Explorer as Default File Manager`).
- **Headless:** `FileScoutNative.exe --set-default-file-manager` / `--restore-explorer` (exit code 0 on
  success), for installers and uninstallers.

## What it writes (HKCU only, no admin, no signature)

All under `HKCU\Software\Classes`. Implementation: `native/FileScout.Native/Shell/DefaultFileManagerService.cpp`.

| Key | Value | Who uses it (verified on win1) |
|---|---|---|
| `Directory\shell` | `(default)` = `FileScout` | default verb: desktop double-click, `ShellExecute(NULL, folder)`, Run dialog |
| `Directory\shell\FileScout\command` | `"<exe>" "%1"` | the verb above |
| `Directory\shell\open\command`, `...\explore\command` | `"<exe>" "%1"`, `DelegateExecute` = `""` | apps that pass an explicit `open`/`explore` verb |
| `Drive\...` | the same four | drive roots (`C:\`) |
| `Folder\shell\explore\command` | `"<exe>" "%1"`, `DelegateExecute` = `""` | Edge's "Open downloads folder" (forces the Folder class + `explore`) |
| `CLSID\{52205fd8-5dfb-447d-801a-d0b52f2e83e1}\shell\opennewwindow\command` | `"<exe>"`, `DelegateExecute` = `""` | Win+E |

`SHOpenFolderAndSelectItems` (Chromium's "Show in folder") resolves the `Directory` default verb.
FileScout opens the folder; the file is not selected (the verb only carries the folder).

How each path was found: a stub command that logs its own command line was registered, then each
launch path was exercised in session 1 (`tools/win1/demo/`). Edge's button produced
`Folder-explore C:\Users\runetest\Downloads` and nothing for the `Directory` verbs, which is why the
`Folder\shell\explore` entry exists.

## Restore without traces

Before the first write the service stores a manifest in `HKCU\Software\FileScout\DefaultFileManager`:
for each value it writes, the previous data or "absent", and every key it had to create. Restore puts
the old data back, deletes created keys that are still empty (a key another program wrote into
since is kept), then deletes the manifest and `HKCU\Software\FileScout` if nothing else lives there.
A second "Make default" keeps the first manifest, so the original Explorer state is never lost.

Tests: `DefaultFileManagerServiceTests.cpp` runs every round trip in an isolated hive
(`HKCU\Software\FileScoutTests\<guid>`) and compares a recursive dump before and after.

## Virtual folders

`Folder` verbs also apply to virtual folders. A `::{CLSID}` or `shell:` argument is handed to
`explorer.exe` unchanged before WinUI starts (`ShouldDelegateToExplorer` in `main.cpp`), so This PC,
Control Panel and similar still open in Explorer.

## Video and evidence

- `media/default-fm-demo.mp4`: 70 s, real time, session 1 of `runetest` on win1, real input.
  Steps: Win+E opens Explorer → the menu action in FileScout → Win+E, a desktop double-click, the Run
  dialog and Edge's "Open downloads folder" all open FileScout → restore from the menu → Win+E and a
  folder open Explorer again → the registry keys FileScout touches are identical to before.
- `media/default-fm-demo.json`: which window each step opened, detected by UI Automation (class
  `CabinetWClass` for Explorer, process `FileScoutNative` for FileScout).
- `media/registry-before.txt`, `media/registry-after.txt`: exports of every key FileScout touches,
  before the demo and after the restore. They are byte-identical (SHA-256 shown in the video).

Re-run: `tools/win1/win1.sh gui 'powershell -NoProfile -ExecutionPolicy Bypass -File C:\fsdev\tools\default-fm-demo.ps1' 600`
(after copying `tools/win1/demo/*.ps1|psm1` to `C:\fsdev\tools\` and a build to `C:\FileScout\`).

## Limits

- Open/Save dialogs, the taskbar and the desktop remain Explorer: no third-party file manager can
  replace these.
- Apps that run `explorer.exe <path>` directly bypass all verbs. None of the tested paths did.
- Every launch starts a new FileScout process (no single instance yet), so the cold start time in
  [perf.md](perf.md) is what a user waits for after Win+E.
