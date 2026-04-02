# Beta Session Persistence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve enough session state that relaunching FileScout feels continuous instead of resetting to a generic home view.

**Architecture:** Introduce a small persistence service dedicated to serializing and restoring closed-beta session state. Keep state capture and restore orchestration in `MainWindow`, but keep file IO and schema details in new focused domain files.

**Tech Stack:** C++20, WinUI 3, filesystem APIs, local app storage, GoogleTest

---

## Branch Rule

Before creating `codex/beta-session-persistence`, start from freshly updated `master`:

1. `git checkout master`
2. `git pull --ff-only`
3. `git checkout -b codex/beta-session-persistence`

If another readiness branch merges while this one is in progress, re-sync from latest `master` before opening or updating the PR.

### Task 1: Define The Session State Model

**Files:**
- Create: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\Domain\SessionState.h`
- Create: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\Domain\SessionStateService.h`
- Create: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\Domain\SessionStateService.cpp`
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\FileScout.Native.vcxproj`
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Tests\FileScout.Tests.vcxproj`
- Create: `C:\Users\machi\source\repos\filescout\native\FileScout.Tests\tests\SessionStateServiceTests.cpp`

- [ ] **Step 1: Write failing persistence tests**

Cover:
- save/load round trip
- missing file -> sane defaults
- missing paths on restore -> fallback behavior
- pane layout persistence

Run: `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=SessionStateService*`
Expected: FAIL

- [ ] **Step 2: Define a minimal state schema**

Keep the model constrained to closed-beta needs:

```cpp
struct PaneSessionState
{
    std::filesystem::path current_path;
    std::vector<std::filesystem::path> tab_paths;
    int32_t active_tab_index{ 0 };
};

struct SessionState
{
    bool dual_pane_visible{ false };
    int32_t active_pane_index{ 0 };
    PaneSessionState left;
    PaneSessionState right;
};
```

- [ ] **Step 3: Implement local storage**

Save under `LOCALAPPDATA\\FileScoutNative\\session.*` alongside other app-owned persisted files. Keep schema parsing resilient and default-safe.

- [ ] **Step 4: Re-run focused persistence tests**

Run: `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=SessionStateService*`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add native/FileScout.Native/Domain/SessionState.h native/FileScout.Native/Domain/SessionStateService.h native/FileScout.Native/Domain/SessionStateService.cpp native/FileScout.Native/FileScout.Native.vcxproj native/FileScout.Tests/FileScout.Tests.vcxproj native/FileScout.Tests/tests/SessionStateServiceTests.cpp
git commit -m "feat(beta): add session persistence service"
```

### Task 2: Restore Session State At Startup

**Files:**
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\MainWindow.xaml.h`
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\MainWindow.xaml.cpp`

- [ ] **Step 1: Restore persisted pane/layout state**

Update startup flow so `MainWindow`:
- loads persisted state before initial pane setup
- restores single vs dual pane
- restores current paths and tabs
- falls back to home when saved paths are missing or invalid

- [ ] **Step 2: Manual smoke**

Verify:
- open a few tabs and paths
- relaunch
- state returns correctly
- missing/deleted folders fall back safely

- [ ] **Step 3: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml.h native/FileScout.Native/MainWindow.xaml.cpp
git commit -m "feat(beta): restore session state on launch"
```

### Task 3: Persist State On User-Relevant Changes

**Files:**
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\MainWindow.xaml.cpp`
- Modify: `C:\Users\machi\source\repos\filescout\docs\ai\status.md`

- [ ] **Step 1: Save on stable transition points**

Persist after:
- pane layout changes
- navigation changes
- tab add/close/select changes
- active pane changes when meaningful

- [ ] **Step 2: Add write-coalescing if needed**

If the first implementation writes too often, add a lightweight coalescing strategy rather than ad hoc guards.

- [ ] **Step 3: Document persistence scope**

Update `docs/ai/status.md` to say exactly what the closed beta now restores between launches.

- [ ] **Step 4: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml.cpp docs/ai/status.md
git commit -m "feat(beta): persist navigation and pane state"
```

### Task 4: Full Verification And Merge Prep

**Files:**
- Verify only

- [ ] **Step 1: Run focused and full tests**

Run:
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=SessionStateService*`
- `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no`

Expected: PASS

- [ ] **Step 2: Run manual persistence checklist**

Verify:
- tab restore
- pane restore
- active pane restore
- fallback when saved paths no longer exist

- [ ] **Step 3: Refresh from latest `master` before merge**

1. `git checkout master`
2. `git pull --ff-only`
3. return to `codex/beta-session-persistence`
4. merge or rebase latest `master`
5. rerun verification

- [ ] **Step 4: Commit any merge-fixups**

```bash
git add -A
git commit -m "chore: refresh beta session persistence branch from master"
```
