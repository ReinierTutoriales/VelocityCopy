# Open-source technology assessment — 2026-10-15

The following primary GitHub pages displayed more than 1,000 stars when checked.
Counts are rounded snapshots, not proof of speed, correctness, compatibility or
appearance in GitHub's daily Trending list. The projects provide open-source
code without a required commercial license purchase; redistribution obligations
still apply. Verify the exact license/tag of any future adopted dependency.

| Project | Stars shown | License | Potential use in VelocityCopy | Decision for this stabilization block |
| --- | ---: | --- | --- | --- |
| [Windows App SDK](https://github.com/microsoft/WindowsAppSDK) | 4.7k | MIT | Native Windows UI and platform APIs | Already used. Keep the validated component set; no unmeasured dependency upgrade. |
| [WinUI Gallery](https://github.com/microsoft/WinUI-Gallery) | 3.7k | MIT | Official control, accessibility and Fluent examples | Use as design/API reference. Current queue simplification follows native layout guidance; do not import another UI framework. |
| [WIL](https://github.com/microsoft/wil) | 3.0k | MIT | Win32 resource ownership and coroutine helpers | Candidate when replacing a demonstrated unsafe wrapper. Existing WRL/WinRT ownership is not rewritten just to add a library. |
| [mimalloc](https://github.com/microsoft/mimalloc) | 13.5k | MIT | Allocation-heavy planning/queue workloads | Candidate for a controlled allocator comparison after CPU/private-memory profiling; no global allocator replacement without measured benefit and CRT/ASan validation. |
| [Tracy](https://github.com/wolfpld/tracy) | 17.0k | [BSD-3-Clause](https://github.com/wolfpld/tracy/blob/master/LICENSE) | CPU, locks, allocations and frame instrumentation | Candidate for an explicit diagnostic-only build. Do not ship a profiling listener or collect filenames by default. |
| [Google Benchmark](https://github.com/google/benchmark) | 10.5k | Apache-2.0 | Repeated isolated microbenchmarks and comparisons | Retain existing benchmark executables first. Add a runner-side repeated planner baseline instead of duplicating the toolchain. |

These are adoption decisions, not claims that a library has already accelerated
VelocityCopy. No source from these projects was copied into the product by this
change, and no additional runtime dependency was added.

## Comparison protocol

Record exact application/dependency commits, compiler/CRT, architecture,
workload, operation, topology and cache conditions. Compare repeated samples,
CPU, private memory, planning latency and responsiveness alongside throughput.
Keep correctness, cancellation, Move deletion and recovery gates mandatory.
Hosted runner measurements are useful for repeatability and spotting regressions;
they do not represent a particular NVMe/HDD/USB/SMB device.

The current CI report uses `VelocityCopyBenchmark --plan-only --json`, five
samples and 1,000 disposable files. Its raw samples and median are uploaded as
`VelocityCopy-planner-baseline-<run-id>`. The existing benchmark matrix remains
the tool for larger and device-specific transfer experiments. Do not run it in
the owner's session without a new request.
