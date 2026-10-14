# Planner benchmark protocol

Baseline: `d8276d7f4269351037280a1bb330f56f846e373a`.
Optimized: `2ba9836b7e6573b9bb95c6556aa0f8bd6c8e6ab1`.

## Important measurement limitation

`VelocityCopyBenchmark <source> <destination> --json` performs a **real copy** after planning. Never run it against valuable destination data. Use disposable, initially empty destinations outside the source tree, one per trial. The original baseline does not expose `planning_seconds`. The dedicated `--plan-only --json` mode was introduced after the optimization. For a valid comparison, apply the **same measurement-only harness** to both historical revisions without altering their planner implementations; use separate worktrees and identical build settings. Never compare the optimized plan-only time to a baseline full-copy elapsed time.

## Current planner-only command

`VelocityCopyBenchmark <source> <destination> --plan-only --json` reports `mode`, `planning_seconds`, `file_count`, `directory_count` and `total_bytes` without copying files or profiling storage. The destination path must still be valid for planner semantics. `--workers` is not accepted in plan-only mode. The CTest smoke test `velocitycopy_planner_benchmark_mode` checks the JSON counts and confirms that no destination files are created.

## Workloads

Use disposable local-NTFS test trees: (1) 64 siblings, (2) 10,000 siblings, (3) 100,000 siblings, (4) nested directory tree with a documented depth, (5) collision-focused correctness fixtures. Record actual file/directory/byte counts. Never benchmark with junctions into external data.

## Procedure

1. Verify CI and Package on the exact baseline under test. Do not treat an empty combined-status response as a successful workflow.
2. Build Release x64 using CMake/MSVC on Windows; run `ctest --test-dir build/x64 -C Release --output-on-failure`.
3. Record hardware, Windows build, filesystem, storage device, Defender state and power mode. Keep these fixed.
4. Use a **planning-only** harness with identical timing boundaries on both revisions; no storage profiling or copy execution inside the timed region. Confirm plan file/directory/byte totals and collision outcomes match.
5. Warm up, then run at least seven paired trials in alternating revision order. Report medians, range, and ratio. Avoid claiming precision beyond timer/system variability.
6. For end-to-end measurements use the production benchmark separately, with a fresh empty disposable destination each trial, and verify output content.
7. Decide on incremental planning only if a measured latency or responsiveness problem remains after the ancestor optimization.

## Acceptance

No changed collision semantics, no missing files, no byte-count regressions, no skipped tests, and no optimization claim without comparable measurements. Windows-only integration remains unverified until physically exercised.
