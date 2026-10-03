param(
    [Parameter(Mandatory = $true)]
    [string]$Path
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Path)) {
    throw "Authenticode test fixture does not exist: $Path"
}

$subject = 'CN=VelocityCopy CI Ephemeral Code Signing'
$pfx = Join-Path $env:RUNNER_TEMP ("velocitycopy-ci-" + [guid]::NewGuid().ToString('N') + '.pfx')
$plain = 'VcCi-' + [guid]::NewGuid().ToString('N') + '!9aA'
$secure = ConvertTo-SecureString $plain -AsPlainText -Force
$cert = $null
$rootInstalled = $false

function Open-CurrentUserRootStore {
    $store = [System.Security.Cryptography.X509Certificates.X509Store]::new(
        [System.Security.Cryptography.X509Certificates.StoreName]::Root,
        [System.Security.Cryptography.X509Certificates.StoreLocation]::CurrentUser)
    $store.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
    return $store
}

try {
    Write-Host "Creating ephemeral CI code-signing certificate"
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject -CertStoreLocation 'Cert:\CurrentUser\My' -KeyAlgorithm RSA -KeyLength 2048 -HashAlgorithm SHA256 -KeyExportPolicy Exportable -NotAfter (Get-Date).AddDays(1)
    Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $secure | Out-Null

    Write-Host "Trusting ephemeral CI certificate in CurrentUser Root store"
    $rootStore = Open-CurrentUserRootStore
    try {
        $rootStore.Add($cert)
        $rootInstalled = $true
    } finally {
        $rootStore.Close()
        $rootStore.Dispose()
    }

    Write-Host "Running bounded Authenticode signing and verification"
    & "$PSScriptRoot\Sign-VelocityCopyBinary.ps1" -Path $Path -PfxPath $pfx -PfxPassword $plain -SkipTimestamp
    if ($LASTEXITCODE -ne 0) {
        throw "Ephemeral Authenticode pipeline failed for $Path"
    }
} finally {
    if ($rootInstalled -and $cert) {
        Write-Host "Removing ephemeral CI certificate from CurrentUser Root store"
        try {
            $rootStore = Open-CurrentUserRootStore
            try {
                $rootStore.Remove($cert)
            } finally {
                $rootStore.Close()
                $rootStore.Dispose()
            }
        } catch {
            Write-Warning "Unable to remove ephemeral CI root certificate $($cert.Thumbprint): $($_.Exception.Message)"
        }
    }
    if ($cert) {
        Remove-Item -LiteralPath ("Cert:\CurrentUser\My\" + $cert.Thumbprint) -Force -ErrorAction SilentlyContinue
    }
    Remove-Item $pfx -Force -ErrorAction SilentlyContinue
}
