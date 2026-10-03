param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Install','Remove')]
    [string]$Action,

    [Parameter(Mandatory = $true)]
    [string]$ExecutablePath
)

$ErrorActionPreference = 'Stop'
$runSubKey = 'Software\Microsoft\Windows\CurrentVersion\Run'
$valueName = 'VelocityCopy'
$expectedCommand = '"' + $ExecutablePath + '" --startup'

function Get-InteractiveUserSid {
    $sessionId = (Get-Process -Id $PID).SessionId
    $explorer = Get-CimInstance Win32_Process -Filter "Name='explorer.exe'" |
        Where-Object { $_.SessionId -eq $sessionId } |
        Select-Object -First 1
    if ($null -eq $explorer) {
        return $null
    }

    $owner = Invoke-CimMethod -InputObject $explorer -MethodName GetOwnerSid
    if ($owner.ReturnValue -ne 0 -or [string]::IsNullOrWhiteSpace($owner.Sid)) {
        return $null
    }
    return $owner.Sid
}

function Set-RunValueForSid([string]$Sid) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::Users,
        [Microsoft.Win32.RegistryView]::Registry64)
    try {
        $key = $base.CreateSubKey("$Sid\$runSubKey")
        try {
            $key.SetValue($valueName, $expectedCommand, [Microsoft.Win32.RegistryValueKind]::String)
        } finally {
            $key.Dispose()
        }
    } finally {
        $base.Dispose()
    }
}

function Remove-MatchingRunValueForSid([string]$Sid) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::Users,
        [Microsoft.Win32.RegistryView]::Registry64)
    try {
        $key = $base.OpenSubKey("$Sid\$runSubKey", $true)
        if ($null -eq $key) { return }
        try {
            $current = [string]$key.GetValue($valueName, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            if ($current -eq $expectedCommand) {
                $key.DeleteValue($valueName, $false)
            }
        } finally {
            $key.Dispose()
        }
    } finally {
        $base.Dispose()
    }
}

$sid = Get-InteractiveUserSid

if ($Action -eq 'Install') {
    if (-not [string]::IsNullOrWhiteSpace($sid)) {
        Set-RunValueForSid $sid
        exit 0
    }

    # Headless/silent installation can have no Explorer in the current session.
    # In that case there is no distinct interactive desktop user to target, so
    # use the installer account's HKCU rather than inventing another profile.
    $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($runSubKey)
    try {
        $key.SetValue($valueName, $expectedCommand, [Microsoft.Win32.RegistryValueKind]::String)
    } finally {
        $key.Dispose()
    }
    exit 0
}

# Uninstall removes only the exact VelocityCopy command for this install. Check
# the interactive user's loaded hive first, then every loaded user hive so an
# uninstall performed with different admin credentials does not leave autostart
# behind in the original desktop user's profile.
if (-not [string]::IsNullOrWhiteSpace($sid)) {
    Remove-MatchingRunValueForSid $sid
}

$users = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
    [Microsoft.Win32.RegistryHive]::Users,
    [Microsoft.Win32.RegistryView]::Registry64)
try {
    foreach ($name in $users.GetSubKeyNames()) {
        if ($name -match '^S-1-5-21-\d+-\d+-\d+-\d+$') {
            Remove-MatchingRunValueForSid $name
        }
    }
} finally {
    $users.Dispose()
}

$current = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($runSubKey, $true)
if ($null -ne $current) {
    try {
        $value = [string]$current.GetValue($valueName, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        if ($value -eq $expectedCommand) {
            $current.DeleteValue($valueName, $false)
        }
    } finally {
        $current.Dispose()
    }
}
