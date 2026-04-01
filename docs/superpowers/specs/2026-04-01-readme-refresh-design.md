# README Refresh Design

## Goal

Refresh the repository `README.md` so it accurately reflects FileScout's current product surface and gives contributors a clear, current way to build and test the app.

## Problem

The current README mixes older milestone-era framing with newer feature bullets. It is directionally right, but it no longer feels like a coherent snapshot of the project after the recent search, command, chrome, sidebar, archive, and workflow improvements. It also lacks a proper test command and leans too heavily on internal planning language.

## Desired Outcome

The updated README should:

- Introduce FileScout in a concise, product-facing way.
- Summarize the strongest current capabilities without referencing internal slice IDs.
- Group features into readable categories instead of one long mixed list.
- Give contributors accurate prerequisites plus restore, build, and test commands.
- Briefly map the repository structure.
- Keep roadmap/status language light and durable.

## Non-Goals

- No feature implementation work.
- No exhaustive changelog or historical archive in the README.
- No promises about future milestones beyond a short directional note.
- No duplication of detailed internal planning docs from `docs/ai/`.

## Proposed Structure

### 1. Title and Intro

Keep the project name as `FileScout`. Replace the current short intro with a tighter paragraph that describes it as a Windows 11 file explorer built with C++/WinRT and WinUI 3, with an emphasis on native shell integration and power-user workflows.

### 2. Current Highlights

Add a short bullet list of the most compelling current capabilities, such as:

- shell-backed browsing and operations
- dual-pane and tabbed workflow
- command palette and keyboard-heavy navigation
- archive browsing and extraction affordances
- fuzzy search and search result navigation

This section should be high-signal and scannable.

### 3. What FileScout Can Do Today

Organize current functionality into grouped subsections instead of a single flat list:

- `Navigation and workspace`
- `File operations and shell integration`
- `Search and discovery`
- `Power-user tools`

Each subsection should use compact bullets and only mention functionality confirmed by the current codebase and status docs.

### 4. Build and Test

Keep the Windows-specific setup instructions, but make them more contributor-oriented:

- prerequisites
- restore command
- build command
- test command using `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no`

Mention the main app binary output path after build.

### 5. Project Layout

Add a short repo map that explains the role of:

- `native/FileScout.Native/`
- `native/FileScout.Tests/`
- `docs/ai/`
- `assets/`

This should stay brief and practical.

### 6. Status Note

Close with a short note that the project is under active development and evolving quickly, while avoiding internal milestone references.

## Content Guidance

- Prefer durable wording over dated milestone labels.
- Avoid raw initiative codes like `R13`, `R17`, or sprint slice names.
- Keep the README product-facing first and implementation-facing second.
- Keep section lengths modest so the document remains readable on GitHub.

## Verification

After the README update:

- reread the final document for stale claims, duplicate bullets, and roadmap overreach
- verify all commands and paths match the current repository layout
- ensure the README mentions testing explicitly

## Risks

- Overstating features that exist only partially or behind internal workflows
- Turning the README into a status dump instead of a polished entry point
- Leaving build/test instructions slightly inconsistent with the actual repo commands

## Recommendation

Proceed with a balanced README refresh: concise product introduction, grouped capability summary, and accurate contributor instructions. This matches both the current maturity of the repo and the user’s goal of bringing the public project surface up to date.
