# Beta Diagnostics And Feedback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give closed-beta testers a simple way to identify the build they are using, export diagnostics, and report issues with useful evidence.

**Architecture:** Build a small diagnostics layer around the existing startup logs and trace logs instead of inventing a telemetry system. Surface version/build information and a visible export/report path in the shell, while keeping the implementation local to new support-oriented helpers.

**Tech Stack:** C++20, WinUI 3, filesystem APIs, GoogleTest, Markdown docs

---

## Branch Rule

Before creating `codex/beta-diagnostics-feedback`, start from freshly updated `master`:

1. `git checkout master`
2. `git pull --ff-only`
3. `git checkout -b codex/beta-diagnostics-feedback`

If another readiness branch merges while this one is in progress, re-sync from latest `master` before opening or updating the PR.

### Task 1: Add A Testable Diagnostics Service

**Files:**
- Create: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\Domain\DiagnosticsBundle.h`
- Create: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\Domain\DiagnosticsService.h`
- Create: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\Domain\DiagnosticsService.cpp`
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\FileScout.Native.vcxproj`
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Tests\FileScout.Tests.vcxproj`
- Create: `C:\Users\machi\source\repos\filescout\native\FileScout.Tests\tests\DiagnosticsServiceTests.cpp`

- [ ] **Step 1: Write failing diagnostics tests**

Cover:
- current trace/startup log locations
- building a support bundle path
- tolerating missing files gracefully

Run: `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=DiagnosticsService*`
Expected: FAIL

- [ ] **Step 2: Implement a minimal diagnostics service**

Keep the first version focused:

```cpp
struct DiagnosticsBundle
{
    std::filesystem::path export_path;
    std::vector<std::filesystem::path> included_files;
};

class DiagnosticsService
{
public:
    [[nodiscard]] static std::filesystem::path TraceLogPath();
    [[nodiscard]] static std::filesystem::path StartupLogPath();
    [[nodiscard]] static DiagnosticsBundle ExportBundle(std::filesystem::path const& destinationRoot);
};
```

- [ ] **Step 3: Re-run diagnostics tests**

Run: `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=DiagnosticsService*`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add native/FileScout.Native/Domain/DiagnosticsBundle.h native/FileScout.Native/Domain/DiagnosticsService.h native/FileScout.Native/Domain/DiagnosticsService.cpp native/FileScout.Native/FileScout.Native.vcxproj native/FileScout.Tests/FileScout.Tests.vcxproj native/FileScout.Tests/tests/DiagnosticsServiceTests.cpp
git commit -m "feat(beta): add diagnostics export service"
```

### Task 2: Surface Build Info And Export Diagnostics In The UI

**Files:**
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\MainWindow.xaml`
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\MainWindow.xaml.h`
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\MainWindow.xaml.cpp`
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\App.xaml.cpp`
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\main.cpp`

- [ ] **Step 1: Add a visible support entry point**

Expose:
- app version/build info
- “Export diagnostics”
- “How to report an issue”

- [ ] **Step 2: Wire diagnostics export**

Use the diagnostics service to export a support bundle to a predictable user-visible location. Keep failures user-readable.

- [ ] **Step 3: Surface build identity**

Add a helper that displays:
- app version
- commit or build stamp if available
- beta label

- [ ] **Step 4: Manual smoke**

Verify:
- support entry point is easy to find
- diagnostics export produces files
- version/build info is visible

- [ ] **Step 5: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml native/FileScout.Native/MainWindow.xaml.h native/FileScout.Native/MainWindow.xaml.cpp native/FileScout.Native/App.xaml.cpp native/FileScout.Native/main.cpp
git commit -m "feat(beta): surface support info and diagnostics export"
```

### Task 3: Add Tester Feedback Documentation

**Files:**
- Create: `C:\Users\machi\source\repos\filescout\docs\beta\reporting-issues.md`
- Modify: `C:\Users\machi\source\repos\filescout\README.md`
- Modify: `C:\Users\machi\source\repos\filescout\docs\ai\status.md`

- [ ] **Step 1: Write a tester-facing issue-report guide**

Include:
- where exported diagnostics go
- what screenshots/logs to send
- what environment details to include
- how to reproduce the issue

- [ ] **Step 2: Link the guide from repo-visible docs**

Add a short README note for beta support and a status note that diagnostics export exists.

- [ ] **Step 3: Commit**

```bash
git add docs/beta/reporting-issues.md README.md docs/ai/status.md
git commit -m "docs(beta): add tester diagnostics and reporting guide"
```

### Task 4: Full Verification And Merge Prep

**Files:**
- Verify only

- [ ] **Step 1: Run focused and full tests**

Run:
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=DiagnosticsService*`
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no`

Expected: PASS

- [ ] **Step 2: Run manual support-flow verification**

Verify:
- support entry point visible
- version/build info visible
- export diagnostics works without developer context
- issue-report doc matches the UI flow

- [ ] **Step 3: Refresh from latest `master` before merge**

1. `git checkout master`
2. `git pull --ff-only`
3. return to `codex/beta-diagnostics-feedback`
4. merge or rebase latest `master`
5. rerun verification

- [ ] **Step 4: Commit any merge-fixups**

```bash
git add -A
git commit -m "chore: refresh beta diagnostics branch from master"
```
