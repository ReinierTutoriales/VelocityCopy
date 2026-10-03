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

try {
    Write-Host "Creating ephemeral CI code-signing certificate"
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject -CertStoreLocation 'Cert:\CurrentUser\My' -KeyAlgorithm RSA -KeyLength 2048 -HashAlgorithm SHA256 -KeyExportPolicy Exportable -NotAfter (Get-Date).AddDays(1)
    Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $secure | Out-Null

    Write-Host "Running bounded Authenticode signing proof without modifying trust stores"
    & "$PSScriptRoot\Sign-VelocityCopyBinary.ps1" -Path $Path -PfxPath $pfx -PfxPassword $plain -SkipTimestamp -ExpectedSignerThumbprint $cert.Thumbprint
    if ($LASTEXITCODE -ne 0) {
        throw "Ephemeral Authenticode pipeline failed for $Path"
    }
} finally {
    if ($cert) {
        Remove-Item -LiteralPath ("Cert:\CurrentUser\My\" + $cert.Thumbprint) -Force -ErrorAction SilentlyContinue
    }
    Remove-Item $pfx -Force -ErrorAction SilentlyContinue
}
