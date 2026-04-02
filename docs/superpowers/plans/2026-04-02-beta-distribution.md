# Beta Distribution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a closed-beta distribution path that lets non-developer testers install and launch FileScout reliably.

**Architecture:** Preserve the unpackaged desktop-host model, but package it as a self-contained beta artifact with explicit install, upgrade, and uninstall steps. Keep build/release automation in repo-visible scripts and workflows so the distribution process is reproducible.

**Tech Stack:** MSBuild, Windows App SDK, PowerShell, GitHub Actions, Markdown docs

---

## Branch Rule

Before creating `codex/beta-distribution`, start from freshly updated `master`:

1. `git checkout master`
2. `git pull --ff-only`
3. `git checkout -b codex/beta-distribution`

If another readiness branch merges while this one is in progress, re-sync from latest `master` before opening or updating the PR.

### Task 1: Lock The Closed-Beta Packaging Strategy

**Files:**
- Create: `C:\Users\machi\source\repos\filescout\docs\beta\distribution-strategy.md`
- Modify: `C:\Users\machi\source\repos\filescout\README.md`

- [ ] **Step 1: Write the strategy doc first**

Capture:
- artifact shape
- runtime bundling strategy
- signing expectation
- install path
- upgrade path
- uninstall path

- [ ] **Step 2: Choose the beta delivery shape**

Recommended default for this repo:
- signed, self-contained unpackaged build
- zipped beta bundle
- `install-beta.ps1` and `uninstall-beta.ps1`

Only switch to MSIX if a concrete blocker is found for unpackaged beta delivery.

- [ ] **Step 3: Commit**

```bash
git add docs/beta/distribution-strategy.md README.md
git commit -m "docs(beta): lock closed beta distribution strategy"
```

### Task 2: Build The Reproducible Beta Artifact Flow

**Files:**
- Create: `C:\Users\machi\source\repos\filescout\scripts\build-beta.ps1`
- Create: `C:\Users\machi\source\repos\filescout\scripts\install-beta.ps1`
- Create: `C:\Users\machi\source\repos\filescout\scripts\uninstall-beta.ps1`
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\FileScout.Native.vcxproj`
- Create: `C:\Users\machi\source\repos\filescout\.github\workflows\beta-package.yml`

- [ ] **Step 1: Script the artifact build**

`build-beta.ps1` should:
- build Release x64
- collect required binaries/resources
- include runtime dependencies per the chosen strategy
- produce a named beta artifact directory or zip

- [ ] **Step 2: Script install and uninstall**

Install script must:
- place the app predictably
- create a shortcut only if the strategy says so
- avoid requiring Visual Studio or manual runtime setup

Uninstall script must reverse the install flow safely.

- [ ] **Step 3: Add packaging automation**

Add a manual GitHub Actions workflow for packaging beta artifacts. Keep it separate from the manual Sonar workflow.

- [ ] **Step 4: Commit**

```bash
git add scripts/build-beta.ps1 scripts/install-beta.ps1 scripts/uninstall-beta.ps1 native/FileScout.Native/FileScout.Native.vcxproj .github/workflows/beta-package.yml
git commit -m "build(beta): add reproducible beta packaging flow"
```

### Task 3: Write Tester Install And Upgrade Docs

**Files:**
- Create: `C:\Users\machi\source\repos\filescout\docs\beta\installing-file-scout-beta.md`
- Modify: `C:\Users\machi\source\repos\filescout\README.md`
- Modify: `C:\Users\machi\source\repos\filescout\docs\ai\status.md`

- [ ] **Step 1: Write the tester guide**

Include:
- install
- first launch
- update/replace with a newer beta
- uninstall
- what to do if Windows blocks launch/signature

- [ ] **Step 2: Link docs from the repo**

Add a short beta-distribution note in README and `docs/ai/status.md`.

- [ ] **Step 3: Commit**

```bash
git add docs/beta/installing-file-scout-beta.md README.md docs/ai/status.md
git commit -m "docs(beta): add installer and upgrade guide"
```

### Task 4: Verify The Artifact On A Clean Tester Flow

**Files:**
- Verify only

- [ ] **Step 1: Build the Release artifact**

Run: `powershell -ExecutionPolicy Bypass -File .\scripts\build-beta.ps1`
Expected: packaged beta output created successfully.

- [ ] **Step 2: Run install/uninstall smoke test**

Run:
- `powershell -ExecutionPolicy Bypass -File .\scripts\install-beta.ps1`
- launch the installed app
- `powershell -ExecutionPolicy Bypass -File .\scripts\uninstall-beta.ps1`

Expected: install, launch, and uninstall all succeed.

- [ ] **Step 3: Refresh from latest `master` before merge**

1. `git checkout master`
2. `git pull --ff-only`
3. return to `codex/beta-distribution`
4. merge or rebase latest `master`
5. rerun packaging smoke tests

- [ ] **Step 4: Commit any merge-fixups**

```bash
git add -A
git commit -m "chore: refresh beta distribution branch from master"
```
