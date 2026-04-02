# Closed Beta Readiness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Validate the fully merged product and prepare the final closed-beta candidate after all five readiness branches land on `master`.

**Architecture:** This is a short-lived integration and validation branch, not a shadow feature branch. It should absorb the already merged readiness work, run full verification, produce tester-facing release materials, and close with a go/no-go decision.

**Tech Stack:** Git, MSBuild, PowerShell, WinUI 3 runtime smoke testing, Markdown docs

---

## Branch Rule

Create this branch only after all five readiness branches are merged into `master`.

Required workflow:

1. `git checkout master`
2. `git pull --ff-only`
3. confirm all five readiness PRs are merged
4. `git checkout -b codex/closed-beta-readiness`

Do not start this branch early and let it drift.

### Task 1: Capture The Integrated Baseline

**Files:**
- Modify: `C:\Users\machi\source\repos\filescout\docs\ai\status.md`
- Create: `C:\Users\machi\source\repos\filescout\docs\beta\closed-beta-checklist.md`

- [ ] **Step 1: Record the merged readiness state**

Update `docs/ai/status.md` with:
- merged branch SHAs
- beta-surface summary
- diagnostics/export path summary
- persistence summary
- distribution summary

- [ ] **Step 2: Write the final manual checklist**

`closed-beta-checklist.md` must cover:
- install
- launch
- navigation
- file operations
- diagnostics export
- session restore
- uninstall or upgrade

- [ ] **Step 3: Commit**

```bash
git add docs/ai/status.md docs/beta/closed-beta-checklist.md
git commit -m "docs(beta): capture integrated closed beta baseline"
```

### Task 2: Run Full Automated Verification On The Integrated Build

**Files:**
- Verify only

- [ ] **Step 1: Restore and build**

Run:

```powershell
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal"
```

Expected: build succeeds with no new beta-blocking warnings.

- [ ] **Step 2: Run full tests**

Run: `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no`
Expected: full pass

- [ ] **Step 3: Commit only if fixes were required**

```bash
git add -A
git commit -m "fix(beta): resolve integrated readiness regressions"
```

### Task 3: Validate The Installable Beta Artifact End To End

**Files:**
- Verify only

- [ ] **Step 1: Produce the beta artifact**

Run: `powershell -ExecutionPolicy Bypass -File .\scripts\build-beta.ps1`
Expected: final beta artifact created.

- [ ] **Step 2: Perform installer smoke**

Run:
- `powershell -ExecutionPolicy Bypass -File .\scripts\install-beta.ps1`
- launch app
- use `docs/beta/closed-beta-checklist.md`
- `powershell -ExecutionPolicy Bypass -File .\scripts\uninstall-beta.ps1`

Expected: full install, launch, use, and uninstall pass.

- [ ] **Step 3: Commit only if install-flow fixes were required**

```bash
git add -A
git commit -m "fix(beta): resolve final install-flow issues"
```

### Task 4: Publish Tester-Facing Beta Materials

**Files:**
- Create: `C:\Users\machi\source\repos\filescout\docs\beta\release-notes.md`
- Create: `C:\Users\machi\source\repos\filescout\docs\beta\tester-guide.md`
- Modify: `C:\Users\machi\source\repos\filescout\README.md`

- [ ] **Step 1: Write release notes**

Summarize:
- key beta capabilities
- known non-goals
- known limitations
- how to report issues

- [ ] **Step 2: Write the tester guide**

Include:
- install link or artifact path
- what to focus on
- what not to expect yet
- how to send diagnostics

- [ ] **Step 3: Link the release material**

Add a short repo-visible pointer from README if appropriate for the beta handoff.

- [ ] **Step 4: Commit**

```bash
git add docs/beta/release-notes.md docs/beta/tester-guide.md README.md
git commit -m "docs(beta): add closed beta release materials"
```

### Task 5: Final Go/No-Go Review

**Files:**
- Verify only

- [ ] **Step 1: Re-run the critical verification commands after the final docs commit**

Run:
- full build
- full test suite
- beta artifact build

Expected: all pass

- [ ] **Step 2: Complete the final checklist**

Mark each item in `docs/beta/closed-beta-checklist.md` as pass/fail with notes.

- [ ] **Step 3: Prepare merge**

If go:
- merge `codex/closed-beta-readiness` to `master`

If no-go:
- capture blockers in `docs/ai/status.md`
- do not merge until resolved

- [ ] **Step 4: Commit final readiness notes**

```bash
git add docs/beta/closed-beta-checklist.md docs/ai/status.md
git commit -m "docs(beta): record closed beta go-no-go result"
```
