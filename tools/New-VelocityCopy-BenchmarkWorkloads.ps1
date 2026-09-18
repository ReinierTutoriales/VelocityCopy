param(
    [Parameter(Mandatory = $true)][string]$Root,
    [switch]$IncludeLarge
)

$ErrorActionPreference = "Stop"
$rootPath = [System.IO.Path]::GetFullPath($Root)
New-Item -ItemType Directory -Force -Path $rootPath | Out-Null

function New-SparseFile([string]$Path, [long]$Bytes) {
    $parent = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try { $stream.SetLength($Bytes) } finally { $stream.Dispose() }
}

# A: one large file. 10 GiB is opt-in to avoid surprising disk consumption.
$a = Join-Path $rootPath "A-large-file"
New-Item -ItemType Directory -Force -Path $a | Out-Null
New-SparseFile (Join-Path $a "large.bin") ($(if ($IncludeLarge) { 10GB } else { 1GB }))

# B: 100 medium files.
$b = Join-Path $rootPath "B-medium-files"
1..100 | ForEach-Object { New-SparseFile (Join-Path $b ("file-{0:D3}.bin" -f $_)) 100MB }

# C: 10,000 small files. Write deterministic payload bytes so this exercises metadata + data IO.
$c = Join-Path $rootPath "C-small-files"
New-Item -ItemType Directory -Force -Path $c | Out-Null
$payload = New-Object byte[] 4096
for ($i = 0; $i -lt 10000; $i++) {
    [System.IO.File]::WriteAllBytes((Join-Path $c ("file-{0:D5}.bin" -f $i)), $payload)
}

# D/E: deep tree plus empty directories.
$d = Join-Path $rootPath "D-deep-tree"
$current = $d
for ($depth = 1; $depth -le 64; $depth++) {
    $current = Join-Path $current ("level-{0:D2}" -f $depth)
    New-Item -ItemType Directory -Force -Path $current | Out-Null
    [System.IO.File]::WriteAllBytes((Join-Path $current "payload.bin"), $payload)
}
$e = Join-Path $rootPath "E-empty-directories"
1..1000 | ForEach-Object { New-Item -ItemType Directory -Force -Path (Join-Path $e ("empty-{0:D4}" -f $_)) | Out-Null }

[pscustomobject]@{
    root = $rootPath
    A = $a
    B = $b
    C = $c
    D = $d
    E = $e
    note = "F append-during-execution is an execution-control scenario; G/H same-drive/cross-drive depend on the selected destination topology."
} | ConvertTo-Json
