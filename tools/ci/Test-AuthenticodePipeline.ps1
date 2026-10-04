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

    Write-Host "Proving Authenticode detects post-signing content modification"
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 0x100) { throw "Signed Authenticode fixture is too small to be a PE image: $Path" }

    $pe = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($pe -lt 0 -or $pe + 24 -gt $bytes.Length) { throw "Invalid PE header offset in signed fixture: $Path" }
    if ($bytes[$pe] -ne 0x50 -or $bytes[$pe + 1] -ne 0x45 -or $bytes[$pe + 2] -ne 0 -or $bytes[$pe + 3] -ne 0) {
        throw "Signed fixture has no valid PE signature: $Path"
    }

    $magic = [BitConverter]::ToUInt16($bytes, $pe + 24)
    if ($magic -eq 0x20B) {
        $dirs = $pe + 24 + 112
    } elseif ($magic -eq 0x10B) {
        $dirs = $pe + 24 + 96
    } else {
        throw "Unsupported PE optional-header magic in signed fixture: 0x$($magic.ToString('X'))"
    }

    $securityDirectory = $dirs + (4 * 8)
    if ($securityDirectory + 8 -gt $bytes.Length) { throw "PE security directory is outside signed fixture: $Path" }
    $certOffset = [BitConverter]::ToInt32($bytes, $securityDirectory)
    $certSize = [BitConverter]::ToInt32($bytes, $securityDirectory + 4)
    if ($certOffset -le 0x400 -or $certSize -le 0 -or $certOffset + $certSize -gt $bytes.Length) {
        throw "Signed fixture has no valid Authenticode certificate table: $Path"
    }

    $target = [int]($certOffset / 2)
    $bytes[$target] = $bytes[$target] -bxor 0x01
    [IO.File]::WriteAllBytes($Path, $bytes)
    $tampered = Get-AuthenticodeSignature -FilePath $Path
    if ($tampered.Status -ne 'HashMismatch') {
        throw "Tampered Authenticode fixture was not rejected with HashMismatch: $($tampered.Status)"
    }
    Write-Host "Verified Authenticode HashMismatch after content modification"
} finally {
    if ($cert) {
        Remove-Item -LiteralPath ("Cert:\CurrentUser\My\" + $cert.Thumbprint) -DeleteKey -Force -ErrorAction SilentlyContinue
    }
    Remove-Item $pfx -Force -ErrorAction SilentlyContinue
}
