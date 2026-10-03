param(
    [Parameter(Mandatory = $true)]
    [string]$Path,

    [Parameter(Mandatory = $true)]
    [string]$PfxPath,

    [Parameter(Mandatory = $true)]
    [string]$PfxPassword,

    [string]$TimestampUrl = 'https://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Path)) { throw "Signing target does not exist: $Path" }
if (-not (Test-Path -LiteralPath $PfxPath)) { throw "Signing PFX does not exist: $PfxPath" }

$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -File -Filter signtool.exe |
    Where-Object { $_.FullName -match '[\\/]x64[\\/]signtool\.exe$' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if (-not $signtool) { throw 'signtool.exe was not found' }

& $signtool.FullName sign /f $PfxPath /p $PfxPassword /fd SHA256 /tr $TimestampUrl /td SHA256 $Path
if ($LASTEXITCODE -ne 0) { throw "Authenticode signing failed for $Path" }

& $signtool.FullName verify /pa /all $Path
if ($LASTEXITCODE -ne 0) { throw "signtool verification failed for $Path" }

$signature = Get-AuthenticodeSignature -FilePath $Path
if ($signature.Status -ne 'Valid') {
    throw "Invalid Authenticode signature for $Path: $($signature.Status)"
}
if ($null -eq $signature.TimeStamperCertificate) {
    throw "Authenticode signature has no RFC3161 timestamp certificate: $Path"
}

Write-Host "Verified Authenticode signature and timestamp: $Path"
