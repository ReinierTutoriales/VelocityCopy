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
$cer = Join-Path $env:RUNNER_TEMP ("velocitycopy-ci-" + [guid]::NewGuid().ToString('N') + '.cer')
$plain = 'VcCi-' + [guid]::NewGuid().ToString('N') + '!9aA'
$secure = ConvertTo-SecureString $plain -AsPlainText -Force
$cert = $null
$trusted = $null

try {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject -CertStoreLocation 'Cert:\CurrentUser\My' -KeyAlgorithm RSA -KeyLength 2048 -HashAlgorithm SHA256 -KeyExportPolicy Exportable -NotAfter (Get-Date).AddDays(1)
    Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $secure | Out-Null
    Export-Certificate -Cert $cert -FilePath $cer | Out-Null
    $trusted = Import-Certificate -FilePath $cer -CertStoreLocation 'Cert:\CurrentUser\Root'

    & "$PSScriptRoot\Sign-VelocityCopyBinary.ps1" -Path $Path -PfxPath $pfx -PfxPassword $plain
    if ($LASTEXITCODE -ne 0) {
        throw "Ephemeral Authenticode pipeline failed for $Path"
    }
} finally {
    if ($trusted) {
        Remove-Item -LiteralPath ("Cert:\CurrentUser\Root\" + $trusted.Thumbprint) -Force -ErrorAction SilentlyContinue
    }
    if ($cert) {
        Remove-Item -LiteralPath ("Cert:\CurrentUser\My\" + $cert.Thumbprint) -Force -ErrorAction SilentlyContinue
    }
    Remove-Item $pfx,$cer -Force -ErrorAction SilentlyContinue
}
