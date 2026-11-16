# FileScout overhaul (2026-11) — plan and decision trail

Goal: FileScout replaces Explorer as the default file manager on Windows 11, with a new look,
measured performance gains and the known bugs fixed. The Rust migration is cancelled; the app stays
C++20 + WinUI 3. `docs/ai/rust-migration/plan.md` and `parity.md` are obsolete. The three specs there
(`spec-domain.md`, `spec-shell.md`, `spec-ui.md`, local only because `docs/ai/*` is git-ignored) are
the inventory the bug ledger works from.

## Definition of done

1. On win1, after one action inside the app, Win+E, opening a folder from the desktop or another app,
   and a browser download's "Open folder" open FileScout. A reverse action restores Explorer with no
   traces. Shown on video. → [default-file-manager.md](default-file-manager.md)
2. Every "Do not port as is" item of the three specs is fixed with a test that failed first, or has a
   written reason why it is not a bug. `DefaultFileManagerServiceTests` is in `FileScout.Tests.vcxproj`
   and passes. All gtests pass. → [bug-ledger.md](bug-ledger.md)
3. Performance measured on the same machine before and after, alternating runs, on a 100k-file folder
   made by a script. → [perf.md](perf.md)
4. A new look chosen from 2–3 prototypes with screenshots on win1. → [look.md](look.md)
5. Every change is a small PR with green CI, stacked where one builds on another.

## Decision log

Newest last. Each entry: date, decision, why, alternatives rejected.

- **2026-11-15 — Build and test on win1 only.** ubuntu-containers cannot build WinUI 3. win1 had no
  compiler; installed VS 2022 Build Tools (VCTools workload, ATL, Windows 11 SDK 26100) so the commands in
  `CLAUDE.md` keep working. Toolchain upgrade is a separate decision (see stack entry).
- **2026-11-15 — CI runs on a self-hosted runner on win1.** The repo is private, so GitHub-hosted
  Windows minutes are billed at 2x and the quota could not be read from the CLI token. The runner is
  registered for this repo only, labelled `filescout`. The old `ci-sonar.yml` was `workflow_dispatch`
  only, so no PR ever had CI. The user ruled out paid hosted runners entirely (2026-11-15).
- **2026-11-15 — The runner is an S4U scheduled task as Administrator, not a service.** As a service
  (NETWORK SERVICE, then a local admin user) the `IFileOperation` tests failed with `E_NOINTERFACE` and
  impersonation errors: a service has no interactive window station. The same tests pass in an S4U task.
  Builds (CI and local) are serialised by `C:\fsdev\build.lock` (8 GB RAM: two WinUI builds at once ran
  out of commit and failed with C1076/C3859), and the perf harness holds the same lock so no build runs
  during measurements.
- **2026-11-15 — win1 housekeeping.** Pagefile fixed at 8–16 GB, Defender exclusions for the build
  folders, Windows Update download cache and WinSxS cleaned to free disk (60 GB disk, ~9 GB free).
- **2026-11-15 — "Before" numbers are taken from master plus a measurement-only PR.** The baseline app has
  no way to report when its first rows or full list were rendered. The first perf PR adds a `--bench`
  mode that only records timings; it changes no behaviour, and its build is the "before" binary.
  Cold start, private memory and idle CPU are measured from outside the process for both.
- **2026-11-15 — Stack versions (checked against nuget.org, Microsoft Learn and runner-images).**
  Windows App SDK 1.8 left servicing on 2026-09-24, so it moves to **2.6.5** (current, supported until
  2027-04-29). Only the component packages the app uses are referenced (Base, Foundation,
  InteractiveExperiences, WinUI, Runtime + SDK.BuildTools.MSIX); the metapackage would add AI, ML,
  Search, Widgets and DWrite, which FileScout does not use. The runtime package family becomes
  `Microsoft.WindowsAppRuntime.2` (users need the 2.x runtime). Windows SDK BuildTools
  10.0.26100.7705 → 10.0.26100.9457 (the 28000 line is early-release for OEMs), WebView2
  1.0.3179.45 → 1.0.4314.35 (WinUI 2.6.5 requires ≥ 1.0.3719.77).
  **Not upgraded, with reasons:** C++/WinRT stays on 2.0.250303.1, because 3.0 is a breaking major version
  (C++20 modules, removed pre-standard coroutines) and nothing here needs it. Toolset stays v143
  (VS 2022 17.14, an LTS) on the self-hosted runner; VS 2026 (v145) is a separate step with no benefit
  for this code yet. GoogleTest 1.8.1 (the old Microsoft NuGet) works; moving to 1.18 via vcpkg is
  worth doing but is not a blocker. ItemsView vs ListView: Microsoft documents data virtualization for
  ListView (`IItemsRangeInfo`) and no performance claim for ItemsView, so the list stays a ListView.

## Open items (2026-11-16)

- **Load time vs the intermediate p4 build:** the final build loads the 100k folder about 1 s slower
  than p4 (after #17) when measured in the same session. The sort part is recovered in #50; the
  enumeration and row-building part is not explained (see perf.md). Next step: a WPR/WPA profile on win1.
- **UB-10 follow-up:** drive enumeration (`GetDiskFreeSpaceEx` per drive) still runs on the UI
  thread at startup, on Refresh and on a volume change. Before #47 it ran on every navigation. It
  should move off the UI thread.
- **IME type-ahead:** text committed by an IME does not raise `CharacterReceived`, so type-ahead (UB-18)
  does not cover IME input.
- **UA-05:** rows filtered right after a load keep the placeholder icon until the next filter.
- **Case-sensitive NTFS folders:** favourites use a case-insensitive path key, so two folders whose
  names differ only by case (possible in per-directory case-sensitive folders) collide. This was
  already true for ASCII names before #45.
- **Look:** sidebar expanders keep the default WinUI chrome. After the first dual-pane toggle, the
  right pane takes the focus and the accent.
- **Default file manager:** Edge's per-item "Show in folder" is not in the video (its "Open downloads
  folder" is). A probe shows `SHOpenFolderAndSelectItems` opens FileScout, but without selecting the item.
