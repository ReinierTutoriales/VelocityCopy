param(
    [switch]$Uninstall,
    [string]$SetupPath,
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

function Get-NativeSetupName {
    $osArchitecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    switch ($osArchitecture) {
        "X64" { return "VelocityCopy-Setup-x64.exe" }
        "Arm64" { return "VelocityCopy-Setup-ARM64.exe" }
        default { throw "Unsupported Windows architecture: $osArchitecture" }
    }
}

function Get-VelocityCopyProcess {
    return @(Get-Process -Name "VelocityCopy.WinUI" -ErrorAction SilentlyContinue)
}

try {
    if (Test-Path -LiteralPath $LogPath -PathType Leaf) {
        Remove-Item -LiteralPath $LogPath -Force -ErrorAction SilentlyContinue
    }

    Write-InstallLog "VelocityCopy classic installer operation started."

    if (-not (Test-IsAdministrator)) {
        throw "VelocityCopy installer requires Administrator privileges."
    }

    if (-not [Environment]::Is64BitOperatingSystem) {
        throw "VelocityCopy requires 64-bit Windows."
    }

    $windowsBuild = [Environment]::OSVersion.Version.Build
    if ($windowsBuild -lt 22000) {
        throw "VelocityCopy requires Windows 11 (build 22000 or newer)."
    }

    $root = Split-Path -Parent $MyInvocation.MyCommand.Path
    $setupName = Get-NativeSetupName
    if (-not $SetupPath) {
        $candidates = @(
            (Join-Path $root $setupName),
            (Join-Path (Split-Path -Parent $root) "artifacts\installer\$setupName")
        )
        $SetupPath = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    }

    $installRoot = Join-Path $env:ProgramFiles "VelocityCopy"
    $exe = Join-Path $installRoot "VelocityCopy.WinUI.exe"
    $uninstaller = Join-Path $installRoot "Uninstall.exe"

    if ($Uninstall) {
        if (-not (Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
            Write-InstallLog "VelocityCopy is not installed."
            exit 0
        }

        # Exercise the real resident-app scenario: --startup should leave the
        # process hidden in the tray, and the uninstaller must force it closed
        # before deleting Program Files.
        if (Test-Path -LiteralPath $exe -PathType Leaf) {
            if ((Get-VelocityCopyProcess).Count -eq 0) {
                Write-InstallLog "Launching VelocityCopy in startup/tray mode before uninstall smoke test."
                Start-Process -FilePath $exe -ArgumentList "--startup" | Out-Null
                $deadline = (Get-Date).AddSeconds(10)
                while ((Get-VelocityCopyProcess).Count -eq 0 -and (Get-Date) -lt $deadline) {
                    Start-Sleep -Milliseconds 200
                }
            }
            if ((Get-VelocityCopyProcess).Count -eq 0) {
                throw "VelocityCopy did not remain resident for the uninstall smoke scenario."
            }
        }

        Write-InstallLog "Running classic uninstaller while VelocityCopy is resident."
        $proc = Start-Process -FilePath $uninstaller -ArgumentList "/S" -Wait -PassThru
        if ($proc.ExitCode -ne 0) {
            throw "Uninstaller failed with exit code $($proc.ExitCode)."
        }
        if ((Get-VelocityCopyProcess).Count -ne 0) {
            throw "Uninstaller returned successfully but VelocityCopy is still running."
        }
        if (Test-Path -LiteralPath $installRoot) {
            throw "Uninstaller returned successfully but the install directory still exists: $installRoot"
        }
        Write-InstallLog "VelocityCopy was removed completely."
        exit 0
    }

    if (-not $SetupPath -or -not (Test-Path -LiteralPath $SetupPath -PathType Leaf)) {
        throw "Classic installer $setupName was not found. Build Windows Package first."
    }

    Write-InstallLog "Installing $($setupName) from $SetupPath."
    $proc = Start-Process -FilePath $SetupPath -ArgumentList "/S" -Wait -PassThru
    if ($proc.ExitCode -ne 0) {
        throw "Installer failed with exit code $($proc.ExitCode)."
    }

    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        throw "Installed executable is missing."
    }

    Write-InstallLog "VelocityCopy installed successfully for the native OS architecture."
    exit 0
}
catch {
    Write-InstallLog ("ERROR: " + $_.Exception.Message)
    if ($_.InvocationInfo -and $_.InvocationInfo.PositionMessage) {
        Write-InstallLog $_.InvocationInfo.PositionMessage
    }
    exit 1
}
