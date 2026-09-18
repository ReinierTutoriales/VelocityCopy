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

if (-not [Environment]::Is64BitOperatingSystem) {
    throw "VelocityCopy test builds require 64-bit Windows."
}

$windowsBuild = [Environment]::OSVersion.Version.Build
if ($windowsBuild -lt 22000) {
    throw "VelocityCopy test builds require Windows 11 (build 22000 or newer)."
}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$certificate = Join-Path $root "VelocityCopy-Test.cer"
$packageName = "ReinierTutoriales.VelocityCopy"
$machineStore = "Cert:\LocalMachine\TrustedPeople"
$installedSupportCert = Join-Path $env:ProgramFiles "VelocityCopy\InstallerSupport\VelocityCopy-Test.cer"
$previousThumbprint = $null
if (Test-Path -LiteralPath $installedSupportCert -PathType Leaf) {
    try {
        $previousCert = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($installedSupportCert)
        $previousThumbprint = $previousCert.Thumbprint
        $previousCert.Dispose()
    } catch {
        $previousThumbprint = $null
    }
}

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

$packageCandidates = @(
    Get-ChildItem -LiteralPath $root -Recurse -File |
        Where-Object {
            $_.FullName -notmatch "[\\/]Dependencies[\\/]" -and
            $_.Extension -in ".msix", ".msixbundle"
        }
)
if ($packageCandidates.Count -ne 1) {
    throw "Expected exactly one VelocityCopy MSIX or MSIX bundle, found $($packageCandidates.Count)."
}
$main = $packageCandidates[0]

$bundledCert = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($certificate)
$signature = Get-AuthenticodeSignature -FilePath $main.FullName
if (-not $signature.SignerCertificate) {
    $bundledCert.Dispose()
    throw "VelocityCopy package has no signer certificate."
}
if ($signature.SignerCertificate.Thumbprint -ne $bundledCert.Thumbprint) {
    $bundledCert.Dispose()
    throw "Bundled certificate does not match the VelocityCopy package signer."
}

$trustedPath = "$machineStore\$($bundledCert.Thumbprint)"
if (-not (Test-Path -LiteralPath $trustedPath)) {
    Write-Host "Trusting VelocityCopy test signing certificate..."
    Import-Certificate -FilePath $certificate -CertStoreLocation $machineStore | Out-Null
}
$bundledCert.Dispose()

$processArchitecture = [System.Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
$dependencyArchitecture = switch ($processArchitecture) {
    "X64" { "x64" }
    "Arm64" { "arm64" }
    default { throw "Unsupported Windows architecture: $processArchitecture" }
}
$dependencyPattern = "[\\/]Dependencies[\\/]" + [regex]::Escape($dependencyArchitecture) + "[\\/]"

$dependencies = @(
    Get-ChildItem -LiteralPath $root -Recurse -File |
        Where-Object {
            $_.FullName -match $dependencyPattern -and
            $_.Extension -in ".appx", ".msix"
        } |
        Select-Object -ExpandProperty FullName
)
if ($dependencies.Count -eq 0) {
    throw "Required $dependencyArchitecture runtime packages were not found."
}

$vclibs = @($dependencies | Where-Object { $_ -match "Microsoft\.VCLibs" })
$appRuntime = @($dependencies | Where-Object { $_ -match "Microsoft\.WindowsAppRuntime" })
$orderedDependencies = @($vclibs + $appRuntime)
if ($vclibs.Count -eq 0) {
    throw "Microsoft Visual C++ $dependencyArchitecture runtime packages were not found."
}
if ($appRuntime.Count -eq 0) {
    throw "Microsoft Windows App Runtime $dependencyArchitecture package was not found."
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

if ($previousThumbprint -and $previousThumbprint -ne $signature.SignerCertificate.Thumbprint) {
    $previousTrustedPath = "$machineStore\$previousThumbprint"
    if (Test-Path -LiteralPath $previousTrustedPath) {
        Remove-Item -LiteralPath $previousTrustedPath -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "VelocityCopy installed successfully."
