param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Destination,
    [string]$Benchmark = ".\\build\\Release\\VelocityCopyBenchmark.exe",
    [string]$Output = ".\\velocitycopy-benchmark.jsonl"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Benchmark -PathType Leaf)) {
    throw "VelocityCopyBenchmark was not found: $Benchmark"
}

$results = @()
foreach ($workers in 1, 2, 4) {
    $json = & $Benchmark $Source $Destination "--workers=$workers" --json
    if ($LASTEXITCODE -ne 0) {
        throw "Benchmark failed for worker depth $workers with exit code $LASTEXITCODE"
    }

    $measurement = $json | ConvertFrom-Json
    $measurement | Add-Member -NotePropertyName measured_at_utc -NotePropertyValue ([DateTime]::UtcNow.ToString("o"))
    $results += $measurement
    $measurement | ConvertTo-Json -Compress | Add-Content -LiteralPath $Output -Encoding utf8
}

$results |
    Select-Object workers, recommended_workers, elapsed_seconds, mib_per_second, files_per_second, shared_physical_disk |
    Format-Table -AutoSize
