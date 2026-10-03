param(
    [Parameter(Mandatory = $true)][string]$HelperPath,
    [Parameter(Mandatory = $true)][string]$ApplicationPath
)

$ErrorActionPreference = 'Stop'

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class VCLogon {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct SI {
        public int cb; public string r; public string desktop; public string title;
        public int x; public int y; public int xs; public int ys; public int xc; public int yc;
        public int fill; public int flags; public short show; public short cb2; public IntPtr r2;
        public IntPtr input; public IntPtr output; public IntPtr error;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct PI { public IntPtr process; public IntPtr thread; public int pid; public int tid; }

    [DllImport("advapi32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    public static extern bool CreateProcessWithLogonW(
        string user, string domain, string secret, int logonFlags,
        string app, string command, int creationFlags, IntPtr environment,
        string currentDirectory, ref SI startup, out PI processInfo);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern int WaitForSingleObject(IntPtr handle, int milliseconds);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool GetExitCodeProcess(IntPtr handle, out int exitCode);
    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr handle);
}
'@

function Invoke-AsLocalUser([string]$User, [string]$Secret, [string]$Application, [string]$CommandLine) {
    $si = [VCLogon+SI]::new()
    $si.cb = [Runtime.InteropServices.Marshal]::SizeOf($si)
    $pi = [VCLogon+PI]::new()
    $ok = [VCLogon]::CreateProcessWithLogonW(
        $User, '.', $Secret, 1, $Application, $CommandLine, 0,
        [IntPtr]::Zero, $env:SystemRoot, [ref]$si, [ref]$pi)
    if (-not $ok) {
        throw "CreateProcessWithLogonW failed for ${User}: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
    try {
        if ([VCLogon]::WaitForSingleObject($pi.process, 30000) -ne 0) {
            throw "Process for $User did not finish"
        }
        $exit = 0
        if (-not [VCLogon]::GetExitCodeProcess($pi.process, [ref]$exit)) {
            throw "GetExitCodeProcess failed"
        }
        return $exit
    } finally {
        [void][VCLogon]::CloseHandle($pi.thread)
        [void][VCLogon]::CloseHandle($pi.process)
    }
}

function Quote([string]$Value) { return '"' + $Value.Replace('"', '\"') + '"' }

$userA = 'VCStartupA'
$userB = 'VCStartupB'
$secret = 'Vc!' + [guid]::NewGuid().ToString('N') + 'aA9'
$runPath = 'HKCU\Software\Microsoft\Windows\CurrentVersion\Run'
$outputA = Join-Path $env:PUBLIC 'vc-startup-a.txt'
$outputB = Join-Path $env:PUBLIC 'vc-startup-b.txt'
$outputAfter = Join-Path $env:PUBLIC 'vc-startup-after.txt'
$expected = '"' + $ApplicationPath + '" --startup'
$cmd = $env:ComSpec

Get-LocalUser -Name $userA -ErrorAction SilentlyContinue | Remove-LocalUser -ErrorAction SilentlyContinue
Get-LocalUser -Name $userB -ErrorAction SilentlyContinue | Remove-LocalUser -ErrorAction SilentlyContinue

try {
    $secureSecret = ConvertTo-SecureString $secret -AsPlainText -Force
    New-LocalUser -Name $userA -Password $secureSecret -AccountNeverExpires -PasswordNeverExpires -UserMayNotChangePassword | Out-Null
    New-LocalUser -Name $userB -Password $secureSecret -AccountNeverExpires -PasswordNeverExpires -UserMayNotChangePassword | Out-Null

    if ((Invoke-AsLocalUser $userA $secret $cmd "$cmd /d /c exit 0") -ne 0) { throw "Profile A init failed" }
    if ((Invoke-AsLocalUser $userB $secret $cmd "$cmd /d /c exit 0") -ne 0) { throw "Profile B init failed" }

    $installCommand = "$(Quote $HelperPath) --apply-install $(Quote $ApplicationPath)"
    if ((Invoke-AsLocalUser $userA $secret $HelperPath $installCommand) -ne 0) {
        throw "Profile A startup registration failed"
    }

    Remove-Item $outputA,$outputB,$outputAfter -Force -ErrorAction SilentlyContinue

    $queryA = $cmd + ' /d /c reg.exe query "' + $runPath + '" /v VelocityCopy > "' + $outputA + '" 2>&1'
    if ((Invoke-AsLocalUser $userA $secret $cmd $queryA) -ne 0) { throw "Profile A Run value missing" }
    if (-not (Get-Content $outputA -Raw).Contains($expected)) { throw "Profile A Run value is incorrect" }

    $queryB = $cmd + ' /d /c reg.exe query "' + $runPath + '" /v VelocityCopy > "' + $outputB + '" 2>&1'
    if ((Invoke-AsLocalUser $userB $secret $cmd $queryB) -eq 0) { throw "Profile B was modified" }

    $runnerValue = (Get-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name VelocityCopy -ErrorAction SilentlyContinue).VelocityCopy
    if ($runnerValue -eq $expected) { throw "Runner HKCU was modified" }

    $removeCommand = "$(Quote $HelperPath) --apply-remove $(Quote $ApplicationPath)"
    if ((Invoke-AsLocalUser $userA $secret $HelperPath $removeCommand) -ne 0) {
        throw "Profile A startup removal failed"
    }

    $queryAfter = $cmd + ' /d /c reg.exe query "' + $runPath + '" /v VelocityCopy > "' + $outputAfter + '" 2>&1'
    if ((Invoke-AsLocalUser $userA $secret $cmd $queryAfter) -eq 0) { throw "Profile A Run value remains" }

    Write-Host "Two-profile startup registration test passed."
} finally {
    Remove-Item $outputA,$outputB,$outputAfter -Force -ErrorAction SilentlyContinue
    Get-LocalUser -Name $userA -ErrorAction SilentlyContinue | Remove-LocalUser -ErrorAction SilentlyContinue
    Get-LocalUser -Name $userB -ErrorAction SilentlyContinue | Remove-LocalUser -ErrorAction SilentlyContinue
}
