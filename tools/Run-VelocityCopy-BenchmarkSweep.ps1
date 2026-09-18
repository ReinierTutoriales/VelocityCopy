param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Destination,
    [string]$Benchmark = ".\\build\\Release\\VelocityCopyBenchmark.exe",
    [string]$Output = ".\\velocitycopy-benchmark.jsonl",
    [ValidateRange(1, 20)][int]$Repeats = 3
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Benchmark -PathType Leaf)) {
    throw "VelocityCopyBenchmark was not found: $Benchmark"
}

$results = @()
foreach ($workers in 1, 2, 4) {
    for ($repeat = 1; $repeat -le $Repeats; $repeat++) {
        $json = & $Benchmark $Source $Destination "--workers=$workers" --json
        if ($LASTEXITCODE -ne 0) {
            throw "Benchmark failed for worker depth $workers repeat $repeat with exit code $LASTEXITCODE"
        }

        $measurement = $json | ConvertFrom-Json
        $measurement | Add-Member -NotePropertyName repeat -NotePropertyValue $repeat
        $measurement | Add-Member -NotePropertyName measured_at_utc -NotePropertyValue ([DateTime]::UtcNow.ToString("o"))
        $results += $measurement
        $measurement | ConvertTo-Json -Compress | Add-Content -LiteralPath $Output -Encoding utf8
    }
}

$summary = $results |
    Group-Object workers |
    ForEach-Object {
        $samples = $_.Group
        $bandwidth = @($samples.mib_per_second | Sort-Object)
        $fileRate = @($samples.files_per_second | Sort-Object)
        $middle = [int][Math]::Floor($bandwidth.Count / 2)
        if (($bandwidth.Count % 2) -eq 0) {
            $medianBandwidth = ($bandwidth[$middle - 1] + $bandwidth[$middle]) / 2
            $medianFileRate = ($fileRate[$middle - 1] + $fileRate[$middle]) / 2
        } else {
            $medianBandwidth = $bandwidth[$middle]
            $medianFileRate = $fileRate[$middle]
        }

        [pscustomobject]@{
            workers = [int]$_.Name
            samples = $samples.Count
            median_mib_per_second = [double]$medianBandwidth
            median_files_per_second = [double]$medianFileRate
            recommended_workers = $samples[0].recommended_workers
            shared_physical_disk = $samples[0].shared_physical_disk
        }
    } |
    Sort-Object workers

$summary | Format-Table -AutoSize
