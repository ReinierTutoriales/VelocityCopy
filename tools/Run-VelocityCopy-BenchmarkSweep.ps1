param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Destination,
    [string]$Benchmark = ".\\build\\Release\\VelocityCopyBenchmark.exe",
    [string]$Output = ".\\velocitycopy-benchmark.jsonl",
    [ValidateRange(1, 20)][int]$Repeats = 3,
    [switch]$KeepOutputs
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Benchmark -PathType Leaf)) {
    throw "VelocityCopyBenchmark was not found: $Benchmark"
}

$results = @()
$session = "session-" + [DateTime]::UtcNow.ToString("yyyyMMdd-HHmmss-fffffff")
$ownedRoot = Join-Path $Destination ".velocitycopy-benchmark"
$sessionRoot = Join-Path $ownedRoot $session
New-Item -ItemType Directory -Path $sessionRoot -Force | Out-Null
foreach ($workers in 1, 2, 4) {
    for ($repeat = 1; $repeat -le $Repeats; $repeat++) {
        $runDestination = Join-Path $sessionRoot ("workers-{0}-repeat-{1}" -f $workers, $repeat)
        New-Item -ItemType Directory -Path $runDestination -Force | Out-Null
        try {
            $json = & $Benchmark $Source $runDestination "--workers=$workers" --json
        } finally {
            if (-not $KeepOutputs -and (Test-Path -LiteralPath $runDestination)) {
                Remove-Item -LiteralPath $runDestination -Recurse -Force
            }
        }
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


if (-not $KeepOutputs -and (Test-Path -LiteralPath $sessionRoot)) {
    Remove-Item -LiteralPath $sessionRoot -Recurse -Force
    if ((Test-Path -LiteralPath $ownedRoot) -and -not (Get-ChildItem -LiteralPath $ownedRoot -Force | Select-Object -First 1)) {
        Remove-Item -LiteralPath $ownedRoot -Force
    }
}
