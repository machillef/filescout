# Performance: before and after

All numbers were measured on win1 (Proxmox VM, Windows Server 2025, **WARP software rendering, no GPU**),
in the interactive console session 1, with `tools/perf/Run-Bench.ps1`. Both builds are Release builds:

- **before** — `master` at the start of the overhaul plus the measurement-only bench mode (#8), which only
  records timings (`C:\fsbench\before`).
- **after** — final `master` (all PRs up to #43) plus #50 (sort), built 2026-11-16 (`C:\fsbench\after`).

Method: 5 runs per build, **alternating order** (A B, B A, …) in one session, holding the build lock so
no CI build competes. The test folder is `C:\fsperf\100k` from `tools/perf/New-PerfFolder.ps1`:
100,000 files plus 200 folders, deterministic names and dates. Values are medians; raw rows are in
[`perf/results-final.csv`](perf/results-final.csv).

## Results (100k-item folder)

| Metric | Before | After | Change | Target | Met |
|---|---:|---:|---:|---|---|
| Time to first rows | 4627 ms | 2971 ms | −36% | ≥ 30% faster | yes |
| Time to full list | 4627 ms | 3402 ms | −26% | ≥ 20% faster | yes |
| Scroll frame time p50 | 16.8 ms | 15.9 ms | −5% | ≤ 16.7 ms (60 Hz) | yes |
| Scroll frame time p95 | 34.3 ms | 20.6 ms | −40% | ≤ 33 ms | yes |
| Scroll frame time p99 | 38.1 ms | 24.5 ms | −36% | ≤ 33 ms | yes |
| Scroll frames over 33 ms (of 300) | 18 | 0 | −18 | ≤ 1 | yes |
| Private memory, idle with the folder open | 897 MB | 262 MB | −71% | ≥ 50% less | yes |
| Idle CPU (one core, 20 s after load) | 0% | 0% | — | 0% | yes |
| Cold start: first window frame (small folder) | 887 ms | 832 ms | −6% | not worse | yes |
| Cold start: first rows (small folder) | 854 ms | 811 ms | −5% | not worse | yes |

**The targets were set when this report was written, after the measurements**, not before; they are
the bar the overhaul aimed at (load clearly faster, memory at least halved, scrolling within two frames
at 60 Hz, no idle cost, no slower start), stated here so the next change can be judged against them.

"Before" has no staged load: its first rows and full list are the same moment.

## What the WARP numbers mean

win1 has no GPU, so every frame is rasterised on the CPU by WARP.

- **WARP-limited: scroll frame times and cold start.** Their absolute values say little about real
  hardware: a GPU machine renders the same frames much faster. Only the relative change is meaningful.
- **Partly WARP-limited: load times.** While a folder loads, WARP rendering of the window competes with
  enumeration and row building for the same 4 vCPUs. The same load stages measured in isolation
  (`PerfProbe.DISABLED_LoadStages100k`, no window) take about 400 ms to enumerate and 57 ms to sort,
  against about 1600 ms and 170 ms inside the app.
- **Not WARP-limited: private memory and idle CPU.**

## Run-to-run drift on win1

The same binary can measure about 2× slower on another day: an intermediate build (p4, after #17)
gave a full list of 1238 ms on 2026-11-15 and 2290 ms on 2026-11-16, and "before" ranged from 4.5 s to
10.6 s across sessions. Only builds measured in the same session, alternating, are compared here.

## Open: load time is above the intermediate p4 build

In one session on 2026-11-16 ([`perf/results-p4-vs-after.csv`](perf/results-p4-vs-after.csv), 3 runs
each), the final build before #50 loaded the 100k folder about 1.3 s slower than p4:

| Stage (in app) | p4 | after (before #50) |
|---|---:|---:|
| Enumeration | 734 ms | 1267 ms |
| Sort | 102 ms | 432 ms |
| Row building | 340 ms | 526 ms |

Scrolling was better in the final build (p99 22.7 vs 37.2 ms; 0 vs 22 frames over 33 ms). #50 recovers
the sort (169 ms in app). The rest is not explained yet. These were ruled out: the locale date formatter
(about 0.8 µs per call, cached per day and minute), Unicode case folding (table lookups), and the
progress bars (shown only for search and delete). The leading suspect is more CPU contention from window
rendering during the load. That needs a profiler (WPR/WPA) run on win1. It is listed as an open item
in the overhaul README.

## Earlier intermediate measurements

| Build | Session | Full list | Private memory |
|---|---|---:|---:|
| before | 2026-11-15 | 10663 ms | 894 MB |
| p1 (#10: shared icon pixels, no hidden right pane load, large-fetch enumeration) | 2026-11-15 | 1405 ms | 268 MB |
| p4 (#17: parallel load, first rows one frame before the full list) | 2026-11-15 | 1238 ms | 272 MB |
