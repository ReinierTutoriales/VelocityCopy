param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [string]$Output = ".\\velocitycopy-benchmark-summary.json"
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $InputPath -PathType Leaf)) { throw "Benchmark dataset was not found: $InputPath" }

$rows = @(Get-Content -LiteralPath $InputPath | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | ForEach-Object { $_ | ConvertFrom-Json })
if ($rows.Count -eq 0) { throw "Benchmark dataset is empty." }

function Get-Median([double[]]$Values) {
    $sorted = @($Values | Sort-Object)
    $mid = [int][Math]::Floor($sorted.Count / 2)
    if (($sorted.Count % 2) -eq 0) { return ($sorted[$mid - 1] + $sorted[$mid]) / 2.0 }
    return $sorted[$mid]
}

$groups = $rows | Group-Object {
    $scenario = if ($null -ne $_.scenario) { $_.scenario } else { "unspecified" }
    $topology = if ($null -ne $_.topology_scenario) { $_.topology_scenario } else { "unknown" }
    "$scenario|$topology|$($_.workers)"
}

$summary = @($groups | ForEach-Object {
    $samples = @($_.Group)
    [pscustomobject]@{
        scenario = if ($null -ne $samples[0].scenario) { $samples[0].scenario } else { "unspecified" }
        topology_scenario = if ($null -ne $samples[0].topology_scenario) { $samples[0].topology_scenario } else { "unknown" }
        workers = [int]$samples[0].workers
        samples = $samples.Count
        median_mib_per_second = [double](Get-Median @($samples.mib_per_second))
        median_files_per_second = [double](Get-Median @($samples.files_per_second))
        min_mib_per_second = [double](($samples.mib_per_second | Measure-Object -Minimum).Minimum)
        max_mib_per_second = [double](($samples.mib_per_second | Measure-Object -Maximum).Maximum)
        median_cpu_seconds = if ($null -ne $samples[0].cpu_seconds) { [double](Get-Median @($samples.cpu_seconds)) } else { $null }
        median_cpu_cores_used = if ($null -ne $samples[0].cpu_cores_used) { [double](Get-Median @($samples.cpu_cores_used)) } else { $null }
        median_working_set_bytes = if ($null -ne $samples[0].working_set_bytes) { [double](Get-Median @($samples.working_set_bytes)) } else { $null }
        median_peak_working_set_bytes = if ($null -ne $samples[0].peak_working_set_bytes) { [double](Get-Median @($samples.peak_working_set_bytes)) } else { $null }
        median_private_usage_bytes = if ($null -ne $samples[0].private_usage_bytes) { [double](Get-Median @($samples.private_usage_bytes)) } else { $null }
        median_append_latency_ms = if ($null -ne $samples[0].append_latency_ms) { [double](Get-Median @($samples.append_latency_ms)) } else { $null }
    }
} | Sort-Object scenario, topology_scenario, workers)

# This report is descriptive only. It deliberately does not choose a production worker count.
$report = [pscustomobject]@{
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    sample_count = $rows.Count
    groups = $summary
}

$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $Output -Encoding utf8
$summary | Format-Table scenario, topology_scenario, workers, samples, median_mib_per_second, median_files_per_second, median_cpu_cores_used, median_append_latency_ms -AutoSize
Write-Host "Descriptive benchmark summary written to $Output"
