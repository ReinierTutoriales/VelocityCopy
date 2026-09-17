param(
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"

$packageName = "ReinierTutoriales.VelocityCopy"
if ($Uninstall) {
    Get-AppxPackage -Name $packageName -ErrorAction SilentlyContinue |
        Remove-AppxPackage -ErrorAction Stop
    Write-Host "VelocityCopy removed for the current user."
    exit 0
}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$certificate = Join-Path $root "VelocityCopy-Test.cer"
if (-not (Test-Path -LiteralPath $certificate -PathType Leaf)) {
    throw "VelocityCopy-Test.cer is missing."
}

Import-Certificate -FilePath $certificate -CertStoreLocation "Cert:\CurrentUser\TrustedPeople" | Out-Null

$packages = Get-ChildItem -LiteralPath $root -Recurse -File -Filter *.msix
$main = $packages |
    Where-Object { $_.FullName -notmatch "[\\/]Dependencies[\\/]" } |
    Sort-Object Length -Descending |
    Select-Object -First 1
if (-not $main) {
    throw "VelocityCopy MSIX package was not found."
}

$dependencies = @(
    Get-ChildItem -LiteralPath $root -Recurse -File |
        Where-Object {
            $_.FullName -match "[\\/]Dependencies[\\/]" -and
            $_.Extension -in ".appx", ".msix"
        } |
        Select-Object -ExpandProperty FullName
)

$params = @{
    Path = $main.FullName
    ForceApplicationShutdown = $true
}
if ($dependencies.Count -gt 0) {
    $params.DependencyPath = $dependencies
}

Add-AppxPackage @params
Write-Host "VelocityCopy installed for the current user."
Write-Host "Launch VelocityCopy once to register its enabled startup task."
Write-Host "Windows may reload File Explorer integration after Explorer restart or sign-out/sign-in."
