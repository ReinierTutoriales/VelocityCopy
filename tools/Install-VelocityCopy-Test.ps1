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

# Install x64 framework dependencies first so runtime failures such as
# MSVCP140.dll/VCRUNTIME140.dll missing are surfaced deterministically.
$vclibs = @($dependencies | Where-Object { $_ -match "Microsoft\.VCLibs" })
$appRuntime = @($dependencies | Where-Object { $_ -match "Microsoft\.WindowsAppRuntime" })
$orderedDependencies = @($vclibs + $appRuntime)

foreach ($dependency in $orderedDependencies) {
    Write-Host "Installing dependency: $(Split-Path -Leaf $dependency)"
    Add-AppxPackage -Path $dependency -ErrorAction Stop
}

$params = @{
    Path = $main.FullName
    ForceApplicationShutdown = $true
}
Add-AppxPackage @params

$requiredFrameworks = @(
    "Microsoft.VCLibs.140.00",
    "Microsoft.VCLibs.140.00.UWPDesktop",
    "Microsoft.WindowsAppRuntime.2"
)
foreach ($framework in $requiredFrameworks) {
    $installed = Get-AppxPackage -Name $framework -ErrorAction SilentlyContinue |
        Where-Object { $_.Architecture -eq "X64" -or $_.Architecture -eq "Neutral" } |
        Select-Object -First 1
    if (-not $installed) {
        throw "Required x64 framework was not installed: $framework"
    }
}

Write-Host "VelocityCopy installed for the current user."
Write-Host "Launch VelocityCopy once to register its enabled startup task."
Write-Host "Windows may reload File Explorer integration after Explorer restart or sign-out/sign-in."
