param(
    [Parameter(Mandatory = $true)][string]$PriPath,
    [Parameter(Mandatory = $true)][string]$Key,
    [Parameter(Mandatory = $true)][string[]]$Languages
)

$ErrorActionPreference = "Stop"
$PriPath = (Resolve-Path $PriPath).Path
$sdkBin = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
$makepri = Get-ChildItem $sdkBin -Recurse -File -Filter makepri.exe |
    Where-Object { $_.FullName -match '[\\/]x64[\\/]makepri\.exe$' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if (-not $makepri) { throw "makepri.exe was not found under $sdkBin" }

$dump = Join-Path $env:TEMP ("VelocityCopy-pri-" + [guid]::NewGuid().ToString("N") + ".xml")
try {
    & $makepri.FullName dump /if $PriPath /of $dump /dt detailed
    if ($LASTEXITCODE -ne 0) { throw "makepri dump failed with exit code $LASTEXITCODE" }
    if (-not (Test-Path $dump)) { throw "makepri did not create $dump" }

    [xml]$xml = Get-Content $dump -Raw
    $nodes = @($xml.SelectNodes('//*'))
    $resource = $nodes | Where-Object {
        $attrs = @($_.Attributes | ForEach-Object { $_.Value })
        ($attrs | Where-Object { $_ -ieq $Key }).Count -gt 0
    } | Select-Object -First 1
    if (-not $resource) { throw "PRI resource '$Key' was not found" }

    $values = @($resource.SelectNodes('.//*') | ForEach-Object {
        @($_.Attributes | ForEach-Object { $_.Value })
        if ($_.InnerText) { $_.InnerText }
    }) | ForEach-Object { ([string]$_).Trim().ToUpperInvariant() } | Where-Object { $_ }

    $missing = @($Languages | Where-Object { $values -notcontains $_.ToUpperInvariant() })
    if ($missing.Count) {
        throw "PRI resource '$Key' is missing language candidate(s): $($missing -join ', ')"
    }
    Write-Host "PRI resource '$Key' contains candidates: $($Languages -join ', ')"
}
finally {
    Remove-Item $dump -Force -ErrorAction SilentlyContinue
}
