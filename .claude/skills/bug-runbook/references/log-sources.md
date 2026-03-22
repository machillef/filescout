# Log Sources

## Temp logs

- `%TEMP%\FileScout.Native.startup.log`
  - written from `native\FileScout.Native\main.cpp`
  - useful for bootstrap and startup failures before the UI is usable
- `%TEMP%\FileScout.Native.trace.log`
  - written from `native\FileScout.Native\TraceHelper.h`
  - append-only trace log used during startup and other instrumented paths

## Hot files by symptom

- Bootstrap or startup:
  - `native\FileScout.Native\main.cpp`
  - `native\FileScout.Native\App.xaml.cpp`
  - `native\FileScout.Native\TraceHelper.h`
- Window and pane behavior:
  - `native\FileScout.Native\MainWindow.xaml.cpp`
  - `native\FileScout.Native\MainWindow.xaml`
- Shell-backed operations:
  - `native\FileScout.Native\Shell\ShellFileOperationService.cpp`
  - `native\FileScout.Native\Shell\ShellClipboardService.cpp`
  - `native\FileScout.Native\Shell\ShellOverflowMenuService.cpp`

## Useful report shape

- Symptom
- Exact repro steps
- Launch state: bootstrap failure, startup failure, or runtime bug
- Relevant logs or thrown errors
- Likely subsystem
- Next verification step
