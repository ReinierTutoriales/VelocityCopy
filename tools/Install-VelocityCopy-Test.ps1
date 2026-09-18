param(
    [switch]$Uninstall,
    [string]$LogPath = (Join-Path $env:TEMP "VelocityCopy-Install.log")
)

$ErrorActionPreference = "Stop"

function Write-InstallLog {
    param([string]$Message)
    $line = "[{0:yyyy-MM-dd HH:mm:ss.fff}] {1}" -f (Get-Date), $Message
    Write-Host $line
    try {
        Add-Content -LiteralPath $LogPath -Value $line -Encoding UTF8 -ErrorAction SilentlyContinue
    } catch {
    }
}

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

try {
    if (Test-Path -LiteralPath $LogPath -PathType Leaf) {
        Remove-Item -LiteralPath $LogPath -Force -ErrorAction SilentlyContinue
    }

    Write-InstallLog "VelocityCopy package operation started."

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
        Write-InstallLog "Removing VelocityCopy MSIX package."
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

        Write-InstallLog "VelocityCopy and its test signing trust were removed."
        exit 0
    }

    if (-not (Test-Path -LiteralPath $certificate -PathType Leaf)) {
        throw "VelocityCopy-Test.cer is missing."
    }

    $bundle = Join-Path $root "VelocityCopy.msixbundle"
    if (-not (Test-Path -LiteralPath $bundle -PathType Leaf)) {
        throw "VelocityCopy.msixbundle is missing."
    }

    $bundledCert = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($certificate)
    try {
        $signature = Get-AuthenticodeSignature -FilePath $bundle
        if (-not $signature.SignerCertificate) {
            throw "VelocityCopy bundle has no signer certificate."
        }
        if ($signature.SignerCertificate.Thumbprint -ne $bundledCert.Thumbprint) {
            throw "Bundled certificate does not match the VelocityCopy bundle signer."
        }
        $currentThumbprint = $bundledCert.Thumbprint
    }
    finally {
        $bundledCert.Dispose()
    }

    $trustedPath = "$machineStore\$currentThumbprint"
    if (-not (Test-Path -LiteralPath $trustedPath)) {
        Write-InstallLog "Trusting the exact VelocityCopy test signing certificate in LocalMachine\TrustedPeople."
        Import-Certificate -FilePath $certificate -CertStoreLocation $machineStore | Out-Null
    }

    # Select dependencies for the native OS architecture, not the bitness of the
    # PowerShell host that NSIS happened to launch.
    $osArchitecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    $dependencyArchitecture = switch ($osArchitecture) {
        "X64" { "x64" }
        "Arm64" { "arm64" }
        default { throw "Unsupported Windows architecture: $osArchitecture" }
    }
    $dependencyRoot = Join-Path $root ("Dependencies\" + $dependencyArchitecture)
    if (-not (Test-Path -LiteralPath $dependencyRoot -PathType Container)) {
        throw "Required $dependencyArchitecture dependency directory was not found."
    }

    $dependencies = @(
        Get-ChildItem -LiteralPath $dependencyRoot -File |
            Where-Object { $_.Extension -in ".appx", ".msix" } |
            Select-Object -ExpandProperty FullName
    )
    if ($dependencies.Count -eq 0) {
        throw "Required $dependencyArchitecture runtime packages were not found."
    }
    if (-not ($dependencies | Where-Object { $_ -match "Microsoft\.VCLibs" })) {
        throw "Microsoft Visual C++ $dependencyArchitecture framework package was not found."
    }
    if (-not ($dependencies | Where-Object { $_ -match "Microsoft\.WindowsAppRuntime" })) {
        throw "Microsoft Windows App Runtime $dependencyArchitecture framework package was not found."
    }

    Write-InstallLog "Deploying VelocityCopy bundle for $dependencyArchitecture with $($dependencies.Count) dependency package(s)."

    # Let AppX Deployment resolve the package graph atomically. This handles
    # framework ordering and already-installed newer framework versions correctly.
    Add-AppxPackage `
        -Path $bundle `
        -DependencyPath $dependencies `
        -ForceApplicationShutdown `
        -ErrorAction Stop

    $installedApp = Get-AppxPackage -Name $packageName -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $installedApp) {
        throw "VelocityCopy package did not register successfully."
    }

    if ($previousThumbprint -and $previousThumbprint -ne $currentThumbprint) {
        $previousTrustedPath = "$machineStore\$previousThumbprint"
        if (Test-Path -LiteralPath $previousTrustedPath) {
            Remove-Item -LiteralPath $previousTrustedPath -Force -ErrorAction SilentlyContinue
        }
    }

    Write-InstallLog "VelocityCopy installed successfully as $($installedApp.PackageFullName)."
    exit 0
}
catch {
    Write-InstallLog ("ERROR: " + $_.Exception.Message)
    if ($_.InvocationInfo -and $_.InvocationInfo.PositionMessage) {
        Write-InstallLog $_.InvocationInfo.PositionMessage
    }
    exit 1
}
