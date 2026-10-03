param(
    [Parameter(Mandatory = $true)]
    [string]$Path,

    [Parameter(Mandatory = $true)]
    [string]$PfxPath,

    [Parameter(Mandatory = $true)]
    [string]$PfxPassword,

    [switch]$SkipTimestamp,

    [string]$ExpectedSignerThumbprint,

    [int]$ToolTimeoutSeconds = 20
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Path)) { throw "Signing target does not exist: $Path" }
if (-not (Test-Path -LiteralPath $PfxPath)) { throw "Signing PFX does not exist: $PfxPath" }

$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -File -Filter signtool.exe |
    Where-Object { $_.FullName -match '[\\/]x64[\\/]signtool\.exe$' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if (-not $signtool) { throw 'signtool.exe was not found' }

function Invoke-BoundedSignTool {
    param([string[]]$Arguments)

    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $signtool.FullName
    $start.UseShellExecute = $false
    foreach ($argument in $Arguments) { [void]$start.ArgumentList.Add($argument) }

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw 'Unable to start signtool.exe' }
    if (-not $process.WaitForExit($ToolTimeoutSeconds * 1000)) {
        try { $process.Kill($true) } catch {}
        if (-not $process.WaitForExit(5000)) { throw "signtool did not terminate after timeout" }
        throw "signtool timed out after $ToolTimeoutSeconds seconds"
    }
    return $process.ExitCode
}

if ($SkipTimestamp) {
    Write-Host "Signing $Path without timestamp for deterministic CI verification"
    $signExit = Invoke-BoundedSignTool @('sign','/f',$PfxPath,'/p',$PfxPassword,'/fd','SHA256',$Path)
    if ($signExit -ne 0) { throw "Authenticode signing failed for $Path with code $signExit" }
} else {
    $timestampUrls = @('https://timestamp.digicert.com','http://timestamp.sectigo.com')
    $signed = $false
    $lastFailure = $null
    foreach ($timestampUrl in $timestampUrls) {
        try {
            Write-Host "Signing $Path with RFC3161 timestamp $timestampUrl"
            $exit = Invoke-BoundedSignTool @('sign','/f',$PfxPath,'/p',$PfxPassword,'/fd','SHA256','/tr',$timestampUrl,'/td','SHA256',$Path)
            if ($exit -eq 0) { $signed = $true; break }
            $lastFailure = "signtool sign exited with code $exit using $timestampUrl"
        } catch {
            $lastFailure = $_.Exception.Message
            Write-Warning "Timestamp/sign attempt failed: $lastFailure"
        }
    }
    if (-not $signed) { throw "Authenticode signing failed for $Path. Last failure: $lastFailure" }
}

if ($SkipTimestamp) {
    # A self-signed ephemeral certificate is deliberately not added to a trust
    # store in PR CI. /pa would therefore test trust, not whether the signing
    # pipeline produced the expected Authenticode signature.
    $verifyExit = Invoke-BoundedSignTool @('verify','/all','/v',$Path)
    if ($verifyExit -ne 0) { throw "signtool cryptographic verification failed for $Path with code $verifyExit" }

    $signature = Get-AuthenticodeSignature -FilePath $Path
    if ($null -eq $signature.SignerCertificate) { throw "Authenticode signer certificate is missing: $Path" }
    if ($ExpectedSignerThumbprint -and $signature.SignerCertificate.Thumbprint -ne $ExpectedSignerThumbprint) {
        throw "Unexpected Authenticode signer for $Path"
    }
    if ($signature.Status -notin @('Valid','UnknownError')) {
        throw "Unexpected Authenticode status for untrusted ephemeral signer on ${Path}: $($signature.Status)"
    }
    Write-Host "Verified ephemeral Authenticode signature: $Path"
    return
}

$verifyExit = Invoke-BoundedSignTool @('verify','/pa','/all',$Path)
if ($verifyExit -ne 0) { throw "signtool verification failed for $Path with code $verifyExit" }

$signature = Get-AuthenticodeSignature -FilePath $Path
if ($signature.Status -ne 'Valid') { throw "Invalid Authenticode signature for ${Path}: $($signature.Status)" }
if ($null -eq $signature.TimeStamperCertificate) { throw "Authenticode signature has no RFC3161 timestamp certificate: $Path" }
Write-Host "Verified Authenticode signature and RFC3161 timestamp: $Path"
