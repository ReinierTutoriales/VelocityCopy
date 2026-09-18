param(
    [Parameter(Mandatory = $true)][string]$InitialSource,
    [Parameter(Mandatory = $true)][string]$AppendSource,
    [Parameter(Mandatory = $true)][string]$Destination,
    [string]$Benchmark = ".\\build\\Release\\VelocityCopyAppendBenchmark.exe",
    [string]$Output = ".\\velocitycopy-append-benchmark.jsonl",
    [ValidateRange(1, 20)][int]$Repeats = 3,
    [switch]$KeepOutputs
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $Benchmark -PathType Leaf)) { throw "Append benchmark was not found: $Benchmark" }

$session = "append-" + [DateTime]::UtcNow.ToString("yyyyMMdd-HHmmss-fffffff")
$ownedRoot = Join-Path $Destination ".velocitycopy-benchmark"
$sessionRoot = Join-Path $ownedRoot $session
New-Item -ItemType Directory -Path $sessionRoot -Force | Out-Null

for ($repeat = 1; $repeat -le $Repeats; $repeat++) {
    $runDestination = Join-Path $sessionRoot ("repeat-{0}" -f $repeat)
    New-Item -ItemType Directory -Path $runDestination -Force | Out-Null
    try {
        $json = & $Benchmark $InitialSource $AppendSource $runDestination
        if ($LASTEXITCODE -ne 0) { throw "Append benchmark repeat $repeat failed with exit code $LASTEXITCODE" }
        $measurement = $json | ConvertFrom-Json
        $measurement | Add-Member -NotePropertyName repeat -NotePropertyValue $repeat
        $measurement | Add-Member -NotePropertyName measured_at_utc -NotePropertyValue ([DateTime]::UtcNow.ToString("o"))
        $measurement | ConvertTo-Json -Compress | Add-Content -LiteralPath $Output -Encoding utf8
    }
    finally {
        if (-not $KeepOutputs -and (Test-Path -LiteralPath $runDestination)) {
            Remove-Item -LiteralPath $runDestination -Recurse -Force
        }
    }
}

if (-not $KeepOutputs -and (Test-Path -LiteralPath $sessionRoot)) {
    Remove-Item -LiteralPath $sessionRoot -Recurse -Force
    if ((Test-Path -LiteralPath $ownedRoot) -and -not (Get-ChildItem -LiteralPath $ownedRoot -Force | Select-Object -First 1)) {
        Remove-Item -LiteralPath $ownedRoot -Force
    }
}
Write-Host "Append benchmark measurements appended to $Output"
