# Cloud Code Quality Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a cloud-first GitHub Actions workflow and SonarQube Cloud configuration for FileScout's native Windows solution.

**Architecture:** Introduce a single Windows GitHub Actions workflow that restores, builds, tests, and then invokes SonarQube Cloud analysis. Keep repository-side config minimal with one workflow file and one root Sonar properties file, while documenting the required GitHub secrets and variables in the workflow comments and final handoff.

**Tech Stack:** GitHub Actions, Windows runner, MSBuild, PowerShell, SonarQube Cloud

---

### Task 1: Add Sonar Project Configuration

**Files:**
- Create: `sonar-project.properties`
- Reference: `docs/superpowers/specs/2026-04-01-cloud-code-quality-design.md`

- [ ] **Step 1: Review the approved design and current repo layout**

Run: `Get-ChildItem -Force`
Expected: repo root shows `native/` and no existing `sonar-project.properties`

- [ ] **Step 2: Create the root Sonar config**

Add a root config that:

```properties
sonar.projectKey=<repo-specific-key>
sonar.organization=<sonar-org>
sonar.projectName=FileScout
sonar.sources=native/FileScout.Native
sonar.tests=native/FileScout.Tests
```

Also keep comments or values aligned with the chosen GitHub variables approach.

- [ ] **Step 3: Verify the file content**

Run: `Get-Content 'sonar-project.properties'`
Expected: project identity and source/test paths point at the current native layout

### Task 2: Add Windows CI + Sonar Workflow

**Files:**
- Create: `.github/workflows/ci-sonar.yml`
- Reference: `build_fs.bat`
- Reference: `README.md`

- [ ] **Step 1: Create the workflow directory**

Run: `Get-ChildItem -Force '.github'`
Expected: directory may not exist yet

- [ ] **Step 2: Create the workflow**

Add a workflow that:

```yaml
name: CI and Sonar
on:
  push:
    branches: [master]
  pull_request:
    branches: [master]
jobs:
  build-test-sonar:
    runs-on: windows-latest
```

The job should:

- check out the repo
- set up MSBuild / Visual Studio developer tooling
- restore the solution
- build `native\FileScout.Native.sln`
- run `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no`
- run SonarQube Cloud scan using `SONAR_TOKEN`

- [ ] **Step 3: Keep the workflow explicit about required repo settings**

Include environment variable usage or comments that make these setup requirements clear:

```yaml
env:
  SONAR_PROJECT_KEY: ${{ vars.SONAR_PROJECT_KEY }}
  SONAR_ORG: ${{ vars.SONAR_ORG }}
```

The workflow should still be readable even before those variables are configured.

- [ ] **Step 4: Verify the workflow content**

Run: `Get-Content '.github\workflows\ci-sonar.yml'`
Expected: syntactically plausible GitHub Actions YAML with restore, build, test, and Sonar steps

### Task 3: Validate Repository Changes

**Files:**
- Review: `sonar-project.properties`
- Review: `.github/workflows/ci-sonar.yml`

- [ ] **Step 1: Review the git diff**

Run: `git diff -- sonar-project.properties .github/workflows/ci-sonar.yml`
Expected: only the intended Sonar and workflow additions

- [ ] **Step 2: Confirm branch status**

Run: `git status --short --branch`
Expected: new workflow/config files staged or ready to stage without unrelated changes

- [ ] **Step 3: Commit**

```bash
git add sonar-project.properties .github/workflows/ci-sonar.yml
git commit -m "ci: add github actions and sonar scan"
```

### Task 4: Prepare User Handoff

**Files:**
- None

- [ ] **Step 1: Summarize what the user must configure manually**

Include:

- SonarQube Cloud project import
- `SONAR_TOKEN` repository secret
- `SONAR_PROJECT_KEY` repository variable
- `SONAR_ORG` repository variable

- [ ] **Step 2: Explain the first validation run**

Tell the user to push a branch or update a PR and inspect the Actions run for:

- restore/build success
- test execution
- Sonar authentication and analysis upload
