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
