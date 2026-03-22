# Build Commands

## Restore packages

```powershell
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true"
```

## Build Debug x64

```powershell
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /p:Configuration=Debug /p:Platform=x64"
```

## Expected artifact

- Verified in this repo: `native\x64\Debug\FileScout.Native\FileScoutNative.exe`
- Repo docs currently say `FileScout.Native.exe`; treat that as stale.

## Useful startup evidence

- `%TEMP%\FileScout.Native.startup.log`
- `%TEMP%\FileScout.Native.trace.log`

## Common failure split

- Compile or link failure: summarize the first real error from MSBuild.
- Bootstrap failure before UI: check the startup log and Windows App Runtime presence.
- UI launches but behavior is wrong: switch to the UI verifier or bug runbook workflow.

## Capturing MSBuild output (pwsh required)

`cmd /c "... > file.txt 2>&1"` does **not** capture MSBuild stdout in Claude's bash
environment — the file gets only the Windows banner line.

Use `pwsh` instead, which streams MSBuild output directly to the terminal:

```powershell
pwsh -Command "& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -SkipAutomaticLocation; & 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' 'native\FileScout.Native.sln' /p:Configuration=Debug /p:Platform=x64 /v:minimal"
```
