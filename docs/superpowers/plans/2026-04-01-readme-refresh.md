# README Refresh Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refresh `README.md` so it accurately presents FileScout's current capabilities and contributor workflow.

**Architecture:** Replace the current milestone-flavored README with a durable, product-forward document. Keep the structure shallow: intro, highlights, grouped capabilities, build/test commands, repo layout, and a short status note.

**Tech Stack:** Markdown, Git, existing repository docs and build/test commands

---

### Task 1: Rewrite README Content

**Files:**
- Modify: `README.md`
- Reference: `docs/superpowers/specs/2026-04-01-readme-refresh-design.md`
- Reference: `docs/ai/status.md`
- Reference: `native/README.md`

- [ ] **Step 1: Review the approved README design and supporting project status**

Run: `Get-Content 'docs\superpowers\specs\2026-04-01-readme-refresh-design.md'`
Expected: approved README structure and content guidance

Run: `Get-Content 'README.md'`
Expected: current README content to replace

- [ ] **Step 2: Rewrite `README.md` to match the approved structure**

Update the file to include:

```md
# FileScout

[short intro paragraph]

## Highlights

- [high-signal capability]

## What FileScout Can Do Today

### Navigation and workspace

- [current capability]

### File operations and shell integration

- [current capability]

### Search and discovery

- [current capability]

### Power-user tools

- [current capability]

## Build and test

[prerequisites]
[restore/build/test commands]

## Project layout

- `native/FileScout.Native/` - [purpose]

## Status

[short durable note]
```

- [ ] **Step 3: Verify the README content for stale claims and command accuracy**

Run: `Get-Content 'README.md'`
Expected: updated README with explicit test command, accurate output path, and no milestone codes like `R13` or `R17`

- [ ] **Step 4: Review git diff**

Run: `git diff -- README.md`
Expected: only the intended README refresh changes

- [ ] **Step 5: Commit**

```bash
git add README.md
git commit -m "docs: refresh readme"
```

### Task 2: Push and Open Pull Request

**Files:**
- Modify: git branch state only

- [ ] **Step 1: Confirm working tree is clean after the README commit**

Run: `git status --short --branch`
Expected: clean branch with local commit(s) ready to publish

- [ ] **Step 2: Push branch**

Run: `git push origin <branch>`
Expected: remote branch updated successfully

- [ ] **Step 3: Open pull request**

Run: `gh pr create --base master --head <branch> --title "docs: refresh readme" --body "[summary and verification]"`
Expected: PR URL returned
