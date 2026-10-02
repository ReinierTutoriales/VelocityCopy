param(
    [Parameter(Mandatory = $true)][string]$RootPath,
    [Parameter(Mandatory = $true)][string]$EvidencePath,
    [Parameter(Mandatory = $true)][string]$Key,
    [Parameter(Mandatory = $true)][string[]]$Languages
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path -LiteralPath $RootPath).Path
New-Item -ItemType Directory -Force $EvidencePath | Out-Null
$evidence = (Resolve-Path -LiteralPath $EvidencePath).Path

$priFiles = @(Get-ChildItem -LiteralPath $root -File -Filter *.pri | Sort-Object Name)
$priFiles | ForEach-Object { "{0}	{1}" -f $_.Name, $_.Length } | Set-Content -LiteralPath (Join-Path $evidence "pri-files.txt") -Encoding utf8

$appPri = Join-Path $root "VelocityCopy.WinUI.pri"
if (-not (Test-Path -LiteralPath $appPri)) { throw "VelocityCopy.WinUI.pri is missing from $root" }
if (Test-Path -LiteralPath (Join-Path $root "resources.pri")) { throw "Legacy resources.pri is present in $root" }

$makepri = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -File -Filter makepri.exe |
    Where-Object { $_.FullName -match '\\x64\\makepri\.exe$' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if (-not $makepri) { throw "makepri.exe was not found in the Windows SDK" }

$dump = Join-Path $evidence "VelocityCopy.WinUI.pri.xml"
& $makepri.FullName dump /if $appPri /of $dump /o
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $dump)) { throw "makepri dump failed for $appPri" }

[xml]$xml = Get-Content -LiteralPath $dump -Raw
$named = @($xml.SelectNodes("//*[local-name()='NamedResource']"))
$resource = $named | Where-Object {
    $name = [string]$_.name
    $name -eq $Key -or $name -eq "Resources/$Key" -or $name.EndsWith("/$Key", [System.StringComparison]::Ordinal)
} | Select-Object -First 1
if (-not $resource) { throw "PRI dump does not contain resource key $Key" }

$text = $resource.OuterXml
foreach ($language in $Languages) {
    $qualifier = "Language-" + $language.ToUpperInvariant()
    $candidate = @($resource.SelectNodes("./*[local-name()='Candidate']")) |
        Where-Object { [string]$_.qualifiers -eq $qualifier } |
        Select-Object -First 1
    if (-not $candidate) {
        throw "PRI resource $Key has no candidate qualified for $language"
    }
    if ([string]::IsNullOrWhiteSpace([string]$candidate.Value)) {
        throw "PRI resource $Key has an empty value for $language"
    }
}
Write-Host "Validated VelocityCopy.WinUI.pri: $Key => $($Languages -join ', ')"
Write-Host "Observed PRI files: $($priFiles.Name -join ', ')"
