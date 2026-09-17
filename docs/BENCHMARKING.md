# VelocityCopy Benchmark Protocol

VelocityCopy keeps `CopyFile2` as the production baseline until measured results justify a different I/O path.

## Build

Build the repository in Release mode and use the generated `VelocityCopyBenchmark.exe`.

## Single measurement

Human-readable output:

```powershell
VelocityCopyBenchmark.exe C:\bench\dataset D:\bench\run
```

Machine-readable output:

```powershell
VelocityCopyBenchmark.exe C:\bench\dataset D:\bench\run --json
```

The JSON result records the detected source/destination storage classes, seek-penalty information, workload size, production strategy, worker count, native copy flags, suggested buffer size, async-candidate decision, elapsed time, and throughput.

## Matrix runs

Copy `tools/benchmark_cases.example.json` and edit only the source and destination roots for the devices being tested.

Run:

```powershell
pwsh tools/run_benchmark_matrix.ps1 `
  -BenchmarkExe .\build\Release\VelocityCopyBenchmark.exe `
  -CasesFile .\tools\benchmark_cases.json `
  -Iterations 5 `
  -OutputCsv .\velocitycopy-benchmark.csv
```

Each iteration creates a unique `.velocitycopy-benchmark-<guid>` directory beneath the configured `destinationRoot`, runs the production executor, records the result, and removes only that generated directory afterward.

## Required media coverage

Before enabling an asynchronous engine in production, collect representative results for:

- rotational HDD
- SATA SSD
- NVMe SSD
- removable/USB storage
- network paths

Use the same dataset for comparisons where practical. Run multiple iterations and compare medians rather than relying on one run. Keep correctness tests green before treating any throughput result as actionable.

The benchmark must exercise `JobExecutor` and the same selected `CopyFile2` flags used by production. Experimental engines must be benchmarked separately and must not silently replace the active path.
