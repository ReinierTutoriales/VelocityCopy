param(
    [Parameter(Mandatory = $true)][string]$WorkloadRoot,
    [Parameter(Mandatory = $true)][string]$DestinationRoot,
    [string]$Sweep = ".\\tools\\Run-VelocityCopy-BenchmarkSweep.ps1",
    [string]$Benchmark = ".\\build\\Release\\VelocityCopyBenchmark.exe",
    [string]$Output = ".\\velocitycopy-benchmark-matrix.jsonl",
    [ValidateRange(1, 20)][int]$Repeats = 3
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Sweep -PathType Leaf)) {
    throw "Benchmark sweep script was not found: $Sweep"
}

$workloads = @(
    @{ scenario = "A"; path = "A-large-file" },
    @{ scenario = "B"; path = "B-medium-files" },
    @{ scenario = "C"; path = "C-small-files" },
    @{ scenario = "D"; path = "D-deep-tree" },
    @{ scenario = "E"; path = "E-empty-directories" }
)

foreach ($workload in $workloads) {
    $source = Join-Path $WorkloadRoot $workload.path
    if (-not (Test-Path -LiteralPath $source -PathType Container)) {
        throw "Missing benchmark workload $($workload.scenario): $source"
    }

    $scenarioDestination = Join-Path $DestinationRoot ("scenario-" + $workload.scenario)
    New-Item -ItemType Directory -Force -Path $scenarioDestination | Out-Null
    Get-ChildItem -LiteralPath $scenarioDestination -Force -ErrorAction SilentlyContinue |
        Remove-Item -Recurse -Force

    $temporaryOutput = [System.IO.Path]::GetTempFileName()
    try {
        & $Sweep -Source $source -Destination $scenarioDestination -Benchmark $Benchmark -Output $temporaryOutput -Repeats $Repeats
        if ($LASTEXITCODE -ne 0) {
            throw "Sweep failed for scenario $($workload.scenario)"
        }

        Get-Content -LiteralPath $temporaryOutput | ForEach-Object {
            if ([string]::IsNullOrWhiteSpace($_)) { return }
            $measurement = $_ | ConvertFrom-Json
            $measurement | Add-Member -NotePropertyName scenario -NotePropertyValue $workload.scenario
            $measurement | ConvertTo-Json -Compress | Add-Content -LiteralPath $Output -Encoding utf8
        }
    }
    finally {
        Remove-Item -LiteralPath $temporaryOutput -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "Benchmark matrix appended to $Output"
