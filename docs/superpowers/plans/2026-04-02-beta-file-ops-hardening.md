# Beta File-Operation Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make copy, cut, paste, rename, delete, and related shell flows predictable and trustworthy for closed-beta users.

**Architecture:** Keep shell-backed operations in `ShellFileOperationService` and `ShellClipboardService`, but stop collapsing operational failures into vague status-bar messages. Add structured failure details where possible, tighten unsafe shell flag choices, and surface clearer outcomes in `MainWindow`.

**Tech Stack:** C++20, COM shell APIs, WinUI 3, GoogleTest

---

## Branch Rule

Before creating `codex/beta-file-ops-hardening`, start from freshly updated `master`:

1. `git checkout master`
2. `git pull --ff-only`
3. `git checkout -b codex/beta-file-ops-hardening`

If another readiness branch merges while this one is in progress, re-sync from latest `master` before opening or updating the PR.

### Task 1: Add Structured File-Operation Error Coverage

**Files:**
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\Shell\ShellFileOperationService.h`
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\Shell\ShellFileOperationService.cpp`
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Tests\tests\ShellFileOperationServiceTests.cpp`

- [ ] **Step 1: Write failing tests for actionable failures**

Add tests that lock down richer behavior for:
- missing-path properties failure
- aborted or conflicting shell operations
- same-folder copy collision behavior remaining safe

Run: `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=ShellFileOperationFixture.*:ShellFileOperationDetail.*`
Expected: FAIL

- [ ] **Step 2: Introduce an explicit operation-error shape**

Keep the error type small and local to shell operations:

```cpp
struct FileOperationError
{
    std::wstring action;
    std::wstring path;
    std::wstring detail;
};
```

Use it to preserve context instead of throwing only generic strings.

- [ ] **Step 3: Re-run focused shell file-operation tests**

Run: `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=ShellFileOperationFixture.*:ShellFileOperationDetail.*`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add native/FileScout.Native/Shell/ShellFileOperationService.h native/FileScout.Native/Shell/ShellFileOperationService.cpp native/FileScout.Tests/tests/ShellFileOperationServiceTests.cpp
git commit -m "fix(fileops): preserve actionable shell operation errors"
```

### Task 2: Harden Clipboard And Paste Failure Handling

**Files:**
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\Shell\ShellClipboardService.h`
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\Shell\ShellClipboardService.cpp`
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\MainWindow.xaml.cpp`
- Create: `C:\Users\machi\source\repos\filescout\native\FileScout.Tests\tests\ShellClipboardServiceTests.cpp`
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Tests\FileScout.Tests.vcxproj`

- [ ] **Step 1: Add clipboard-focused tests**

Cover:
- empty clipboard paste rejection
- move-vs-copy detection
- richer paste failure messaging

Run: `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=ShellClipboardService*`
Expected: FAIL

- [ ] **Step 2: Return richer paste outcomes**

Extend `ClipboardPasteResult` or add a related result type so the UI can show:
- attempted action
- item count
- destination
- failure detail when available

- [ ] **Step 3: Update UI failure and success messaging**

In `MainWindow.xaml.cpp`, replace vague messages like “Paste failed into X” with user-readable detail that still fits the status surface.

- [ ] **Step 4: Re-run clipboard tests**

Run: `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=ShellClipboardService*`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add native/FileScout.Native/Shell/ShellClipboardService.h native/FileScout.Native/Shell/ShellClipboardService.cpp native/FileScout.Native/MainWindow.xaml.cpp native/FileScout.Tests/tests/ShellClipboardServiceTests.cpp native/FileScout.Tests/FileScout.Tests.vcxproj
git commit -m "fix(fileops): harden clipboard and paste reporting"
```

### Task 3: Review Unsafe Silent Behaviors

**Files:**
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\Shell\ShellFileOperationService.cpp`
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\MainWindow.xaml.cpp`
- Modify: `C:\Users\machi\source\repos\filescout\docs\ai\status.md`

- [ ] **Step 1: Audit and reduce risky shell flags**

Review the current use of:
- `FOF_SILENT`
- `FOF_NOERRORUI`
- `FOF_NOCONFIRMATION`

Keep only what is intentional for the closed beta. If a flag remains, document why.

- [ ] **Step 2: Improve destructive-flow messaging**

Update delete, rename, and move/cut status text so users can distinguish:
- success
- cancellation/abort
- conflict/failure

- [ ] **Step 3: Document the operational policy**

Add a short `docs/ai/status.md` note that explains the closed-beta file-operation stance and where errors are surfaced.

- [ ] **Step 4: Commit**

```bash
git add native/FileScout.Native/Shell/ShellFileOperationService.cpp native/FileScout.Native/MainWindow.xaml.cpp docs/ai/status.md
git commit -m "fix(fileops): reduce silent failure paths for beta"
```

### Task 4: Full Verification And Manual Checklist

**Files:**
- Verify only

- [ ] **Step 1: Run the full suite**

Run: `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no`
Expected: full pass

- [ ] **Step 2: Run manual file-op validation**

Verify:
- copy into same folder creates a duplicate safely
- paste failure explains itself clearly
- delete/remove flows still work and failure text is specific
- rename conflicts do not feel silent or ambiguous

- [ ] **Step 3: Refresh from latest `master` before merge**

1. `git checkout master`
2. `git pull --ff-only`
3. return to `codex/beta-file-ops-hardening`
4. merge or rebase latest `master`
5. rerun verification

- [ ] **Step 4: Commit any merge-fixups**

```bash
git add -A
git commit -m "chore: refresh beta file-op branch from master"
```
