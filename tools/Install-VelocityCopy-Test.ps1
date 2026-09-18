param(
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-IsAdministrator)) {
    throw "VelocityCopy installer requires Administrator privileges."
}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$certificate = Join-Path $root "VelocityCopy-Test.cer"
$packageName = "ReinierTutoriales.VelocityCopy"
$machineStore = "Cert:\LocalMachine\TrustedPeople"

if ($Uninstall) {
    Get-AppxPackage -Name $packageName -ErrorAction SilentlyContinue |
        Remove-AppxPackage -ErrorAction Stop

    if (Test-Path -LiteralPath $certificate -PathType Leaf) {
        $cert = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($certificate)
        $trustedPath = "$machineStore\$($cert.Thumbprint)"
        if (Test-Path -LiteralPath $trustedPath) {
            Remove-Item -LiteralPath $trustedPath -Force
        }
        $cert.Dispose()
    }

    Write-Host "VelocityCopy and its test signing trust were removed."
    exit 0
}

if (-not (Test-Path -LiteralPath $certificate -PathType Leaf)) {
    throw "VelocityCopy-Test.cer is missing."
}

$packages = @(
    Get-ChildItem -LiteralPath $root -Recurse -File -Filter *.msix |
        Where-Object { $_.FullName -notmatch "[\\/]Dependencies[\\/]" }
)
if ($packages.Count -ne 1) {
    throw "Expected exactly one VelocityCopy MSIX package, found $($packages.Count)."
}
$main = $packages[0]

$bundledCert = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($certificate)
$signature = Get-AuthenticodeSignature -FilePath $main.FullName
if (-not $signature.SignerCertificate) {
    $bundledCert.Dispose()
    throw "VelocityCopy MSIX has no signer certificate."
}
if ($signature.SignerCertificate.Thumbprint -ne $bundledCert.Thumbprint) {
    $bundledCert.Dispose()
    throw "Bundled certificate does not match the VelocityCopy MSIX signer."
}

$trustedPath = "$machineStore\$($bundledCert.Thumbprint)"
if (-not (Test-Path -LiteralPath $trustedPath)) {
    Write-Host "Trusting VelocityCopy test signing certificate..."
    Import-Certificate -FilePath $certificate -CertStoreLocation $machineStore | Out-Null
}
$bundledCert.Dispose()

$dependencies = @(
    Get-ChildItem -LiteralPath $root -Recurse -File |
        Where-Object {
            $_.FullName -match "[\\/]Dependencies[\\/]x64[\\/]" -and
            $_.Extension -in ".appx", ".msix"
        } |
        Select-Object -ExpandProperty FullName
)
if ($dependencies.Count -eq 0) {
    throw "Required x64 runtime packages were not found."
}

$vclibs = @($dependencies | Where-Object { $_ -match "Microsoft\.VCLibs" })
$appRuntime = @($dependencies | Where-Object { $_ -match "Microsoft\.WindowsAppRuntime" })
$orderedDependencies = @($vclibs + $appRuntime)
if ($vclibs.Count -eq 0) {
    throw "Microsoft Visual C++ x64 runtime packages were not found."
}
if ($appRuntime.Count -eq 0) {
    throw "Microsoft Windows App Runtime x64 package was not found."
}

foreach ($dependency in $orderedDependencies) {
    Write-Host "Installing dependency: $(Split-Path -Leaf $dependency)"
    Add-AppxPackage -Path $dependency -ErrorAction Stop
}

Write-Host "Installing VelocityCopy..."
Add-AppxPackage -Path $main.FullName -ForceApplicationShutdown -ErrorAction Stop

$installedApp = Get-AppxPackage -Name $packageName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $installedApp) {
    throw "VelocityCopy package did not register successfully."
}

Write-Host "VelocityCopy installed successfully."
