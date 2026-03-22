---
name: build-verify-windows
description: Use when working in the FileScout repo and you need to restore NuGet packages, build native/FileScout.Native.sln for Debug x64, verify the unpackaged WinUI output exists, launch the built desktop app, or diagnose MSBuild and Windows App Runtime startup failures. Trigger after changes under native/, before review or handoff, or when the user asks to build, compile, run, or verify the app.
---

# Build Verify Windows

Use this skill for repo-local Windows builds only. Prefer PowerShell and the checked-in helper scripts over retyping long MSBuild commands.

## Quick start

1. Read [references/commands.md](references/commands.md) for the exact commands and artifact paths.
2. Run `scripts/restore-build-debug.ps1`.
3. Verify `native\x64\Debug\FileScout.Native\FileScoutNative.exe` exists.
4. If the user asked to run the app, use `scripts/launch-debug-build.ps1`.
5. If the build fails, report the first actionable compiler, linker, or bootstrap error instead of pasting the entire log.

## Gotchas

- The repo docs currently mention `FileScout.Native.exe`, but the built binary is `FileScoutNative.exe`.
- Startup failures can happen before the window renders. Check `%TEMP%\FileScout.Native.startup.log` and `%TEMP%\FileScout.Native.trace.log`.
- Missing Windows App Runtime installs fail during bootstrap, not during normal app logic.
- Do not delete `native\x64\Debug\FileScout.Native\` or other build output unless the user explicitly asks for a clean rebuild.

## When the build is red

- Separate compile errors, link errors, bootstrap failures, and runtime failures.
- For bootstrap or startup failures, collect the temp log files before trying broader fixes.
- If native code changed and the build succeeds, say whether you only compiled or also launched the app.
