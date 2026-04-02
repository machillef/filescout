# Beta Surface Truth Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make FileScout's closed-beta surface truthful so the README, UI, and feature gates all describe the same product.

**Architecture:** Keep the existing `Wave1FeatureGate` model, but turn it into the single source of truth for what the closed beta exposes. Update app copy, shortcut wiring, and docs to align with that contract instead of advertising hidden or disabled capabilities.

**Tech Stack:** C++20, C++/WinRT, WinUI 3, GoogleTest, Markdown docs

---

## Branch Rule

Before creating `codex/beta-surface-truth`, start from freshly updated `master`:

1. `git checkout master`
2. `git pull --ff-only`
3. `git checkout -b codex/beta-surface-truth`

If another readiness branch merges while this one is in progress, re-sync from latest `master` before opening or updating the PR.

### Task 1: Freeze The Closed-Beta Feature Contract

**Files:**
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\Domain\Wave1FeatureGate.h`
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Tests\tests\Wave1FeatureGateTests.cpp`
- Test: `C:\Users\machi\source\repos\filescout\native\FileScout.Tests\tests\Wave1FeatureGateTests.cpp`

- [ ] **Step 1: Expand the failing gate tests**

Add coverage for:
- which features remain disabled
- whether dual-pane remains secondary on first launch
- whether unavailable-status strings still match the closed-beta contract

Run: `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=Wave1FeatureGate.*`
Expected: FAIL once the new expectations are added.

- [ ] **Step 2: Update the gate model**

Keep the policy in `Wave1FeatureGate.h`:

```cpp
enum class Wave1Feature
{
    CommandPalette,
    ArchiveBrowsing,
    BatchRename,
};

inline constexpr bool IsWave1FeatureEnabled(Wave1Feature feature) noexcept;
inline constexpr bool Wave1UsesSinglePaneFirstLaunch() noexcept;
inline constexpr std::wstring_view Wave1FeatureUnavailableStatus(Wave1Feature feature) noexcept;
```

- [ ] **Step 3: Re-run the focused gate tests**

Run: `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=Wave1FeatureGate.*`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add native/FileScout.Native/Domain/Wave1FeatureGate.h native/FileScout.Tests/tests/Wave1FeatureGateTests.cpp
git commit -m "docs: codify closed beta feature contract"
```

### Task 2: Align Runtime Messaging And Shortcut Exposure

**Files:**
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\MainWindow.xaml.cpp`
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\MainWindow.xaml`
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Native\Shell\CommandRegistry.cpp`
- Modify: `C:\Users\machi\source\repos\filescout\native\FileScout.Tests\tests\CommandRegistryTests.cpp`

- [ ] **Step 1: Write the failing command-surface tests**

Add or update tests so registry/search results reflect the real closed-beta surface. If a feature is gated off, it must not be promoted as an available command.

Run: `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=CommandRegistry.*`
Expected: FAIL

- [ ] **Step 2: Align runtime command and status behavior**

Update:
- keyboard accelerator exposure in `SetupKeyboardAccelerators()`
- command-palette exposure if still gated off
- status strings for unavailable features
- any beta wording that no longer matches the product contract

- [ ] **Step 3: Re-run focused registry tests**

Run: `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no --gtest_filter=CommandRegistry.*:Wave1FeatureGate.*`
Expected: PASS

- [ ] **Step 4: Manual smoke**

Verify:
- hidden/disabled features are not promoted misleadingly
- shortcuts only surface for features actually in beta
- status text explains unavailable features consistently

- [ ] **Step 5: Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml.cpp native/FileScout.Native/MainWindow.xaml native/FileScout.Native/Shell/CommandRegistry.cpp native/FileScout.Tests/tests/CommandRegistryTests.cpp
git commit -m "fix(beta): align runtime surface with closed beta contract"
```

### Task 3: Make Public Docs Match The Product

**Files:**
- Modify: `C:\Users\machi\source\repos\filescout\README.md`
- Modify: `C:\Users\machi\source\repos\filescout\docs\ai\status.md`

- [ ] **Step 1: Update README feature claims**

Rewrite the visible capability sections so they describe the actual closed-beta surface. Remove or qualify claims for gated-off features.

- [ ] **Step 2: Update internal status tracking**

Add a short note in `docs/ai/status.md` stating:
- which features are intentionally outside the closed beta
- that README and runtime messaging were aligned on 2026-04-02

- [ ] **Step 3: Verify docs consistency**

Manual check:
- README does not claim gated-off features as live
- internal status notes match the same decisions

- [ ] **Step 4: Commit**

```bash
git add README.md docs/ai/status.md
git commit -m "docs(beta): align README with closed beta surface"
```

### Task 4: Full Verification And Merge Prep

**Files:**
- Verify only

- [ ] **Step 1: Run full suite**

Run: `native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_color=no`
Expected: full pass

- [ ] **Step 2: Run the manual closed-beta surface checklist**

Verify:
- first-launch defaults remain correct
- dual-pane wording matches reality
- unavailable features explain themselves clearly
- no README-visible feature is secretly disabled

- [ ] **Step 3: Prepare merge**

Before PR/merge:
1. `git checkout master`
2. `git pull --ff-only`
3. return to `codex/beta-surface-truth`
4. merge or rebase latest `master`
5. rerun focused and full verification

- [ ] **Step 4: Commit any merge-fixups**

```bash
git add -A
git commit -m "chore: refresh beta surface truth branch from master"
```
