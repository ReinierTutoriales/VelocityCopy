param(
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$certificate = Join-Path $root "VelocityCopy-Test.cer"
$packageName = "ReinierTutoriales.VelocityCopy"

if ($Uninstall) {
    Get-AppxPackage -Name $packageName -ErrorAction SilentlyContinue |
        Remove-AppxPackage -ErrorAction Stop

    if (Test-Path -LiteralPath $certificate -PathType Leaf) {
        $cert = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($certificate)
        $trustedPeoplePath = "Cert:\CurrentUser\TrustedPeople\$($cert.Thumbprint)"
        $trustedRootPath = "Cert:\CurrentUser\Root\$($cert.Thumbprint)"
        foreach ($path in @($trustedPeoplePath, $trustedRootPath)) {
            if (Test-Path -LiteralPath $path) {
                Remove-Item -LiteralPath $path -Force
            }
        }
        $cert.Dispose()
    }

    Write-Host "VelocityCopy and its test signing certificate were removed for the current user."
    exit 0
}
if (-not (Test-Path -LiteralPath $certificate -PathType Leaf)) {
    throw "VelocityCopy-Test.cer is missing."
}

Import-Certificate -FilePath $certificate -CertStoreLocation "Cert:\CurrentUser\TrustedPeople" | Out-Null
Import-Certificate -FilePath $certificate -CertStoreLocation "Cert:\CurrentUser\Root" | Out-Null

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
            $_.FullName -match "[\\/]Dependencies[\\/]x64[\\/]" -and
            $_.Extension -in ".appx", ".msix"
        } |
        Select-Object -ExpandProperty FullName
)

if ($dependencies.Count -eq 0) {
    throw "Required x64 package dependencies were not found."
}

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
