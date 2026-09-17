param(
    [Parameter(Mandatory = $true)]
    [string]$BenchmarkExe,

    [Parameter(Mandatory = $true)]
    [string]$CasesFile,

    [ValidateRange(1, 100)]
    [int]$Iterations = 3,

    [string]$OutputCsv = "velocitycopy-benchmark.csv"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $BenchmarkExe -PathType Leaf)) {
    throw "Benchmark executable not found: $BenchmarkExe"
}
if (-not (Test-Path -LiteralPath $CasesFile -PathType Leaf)) {
    throw "Cases file not found: $CasesFile"
}

$cases = Get-Content -LiteralPath $CasesFile -Raw | ConvertFrom-Json
if ($null -eq $cases) {
    throw "Cases file is empty."
}
if ($cases -isnot [System.Collections.IEnumerable] -or $cases -is [string]) {
    $cases = @($cases)
}

$rows = [System.Collections.Generic.List[object]]::new()

foreach ($case in $cases) {
    $name = [string]$case.name
    $source = [string]$case.source
    $destinationRoot = [string]$case.destinationRoot

    if ([string]::IsNullOrWhiteSpace($name) -or
        [string]::IsNullOrWhiteSpace($source) -or
        [string]::IsNullOrWhiteSpace($destinationRoot)) {
        throw "Each case requires name, source, and destinationRoot."
    }
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Source does not exist for case '$name': $source"
    }

    New-Item -ItemType Directory -Force -Path $destinationRoot | Out-Null

    for ($iteration = 1; $iteration -le $Iterations; ++$iteration) {
        $runId = [Guid]::NewGuid().ToString("N")
        $runDestination = Join-Path $destinationRoot ".velocitycopy-benchmark-$runId"
        New-Item -ItemType Directory -Force -Path $runDestination | Out-Null

        try {
            Write-Host "[$name] iteration $iteration/$Iterations"
            $json = & $BenchmarkExe $source $runDestination --json
            if ($LASTEXITCODE -ne 0) {
                throw "VelocityCopyBenchmark failed for case '$name' iteration $iteration with exit code $LASTEXITCODE."
            }

            $measurement = $json | ConvertFrom-Json
            $rows.Add([pscustomobject]@{
                case_name = $name
                iteration = $iteration
                timestamp_utc = [DateTime]::UtcNow.ToString("o")
                source_kind = $measurement.source_kind
                destination_kind = $measurement.destination_kind
                source_seek = $measurement.source_seek
                destination_seek = $measurement.destination_seek
                total_bytes = [uint64]$measurement.total_bytes
                file_count = [uint64]$measurement.file_count
                largest_file_bytes = [uint64]$measurement.largest_file_bytes
                strategy = $measurement.strategy
                workers = [uint32]$measurement.workers
                copy_flags = [uint32]$measurement.copy_flags
                buffer_bytes = [uint32]$measurement.buffer_bytes
                async_candidate = [bool]$measurement.async_candidate
                compressed_traffic = [bool]$measurement.compressed_traffic
                elapsed_seconds = [double]$measurement.elapsed_seconds
                mib_per_second = [double]$measurement.mib_per_second
            })
        }
        finally {
            if (Test-Path -LiteralPath $runDestination) {
                Remove-Item -LiteralPath $runDestination -Recurse -Force
            }
        }
    }
}

$rows | Export-Csv -LiteralPath $OutputCsv -NoTypeInformation -Encoding utf8
Write-Host "Wrote $($rows.Count) measurements to $OutputCsv"
