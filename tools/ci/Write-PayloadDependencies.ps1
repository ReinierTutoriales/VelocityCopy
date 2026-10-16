param(
    [Parameter(Mandatory)][string]$RootPath,
    [Parameter(Mandatory)][string]$OutputPath
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $RootPath).Path
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$dumpbin = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe' |
    Select-Object -First 1
if (-not $dumpbin) { throw 'DUMPBIN was not found' }
$files = @(Get-ChildItem -LiteralPath $root -Recurse -File)
$appNames = @('VelocityCopy.WinUI.exe', 'VelocityCopy.Shell.dll', 'VelocityCopy.StartupHelper.exe')
$appFiles = @($files | Where-Object { $_.Name -in $appNames })
if ($appFiles.Count -ne $appNames.Count) { throw 'Expected exactly three application binaries' }
$modules = @($files | Where-Object { $_.Extension -in '.exe', '.dll' } | Sort-Object FullName | ForEach-Object {
    $file = $_
    # /IMPORTS includes static and delay-load tables; it does not load the image.
    $imports = & $dumpbin /NOLOGO /IMPORTS $file.FullName
    if ($LASTEXITCODE -ne 0) { throw "DUMPBIN failed for $($file.Name)" }
    $names = @($imports | ForEach-Object {
        if ($_ -match '^\s+([^\s]+\.dll)\s*$') { $Matches[1] }
    } | Sort-Object -Unique)
    if ($file.Name -in $appNames -and $names.Count -eq 0) {
        throw "No imports found for $($file.Name); check the report parser"
    }
    [ordered]@{
        path = [IO.Path]::GetRelativePath($root, $file.FullName)
        bytes = $file.Length
        file_version = $file.VersionInfo.FileVersion
        imports = $names
    }
})
$total = [long]($files | Measure-Object Length -Sum).Sum
$application = [long]($appFiles | Measure-Object Length -Sum).Sum
[ordered]@{
    schema = 1
    bytes = $total
    application_binary_bytes = $application
    other_payload_bytes = $total - $application
    note = 'Static and delay-load imports only. Absence is not proof of non-use: WinRT activation, LoadLibrary, resources and optional paths require separate validation. Other payload includes runtime AND application resources.'
    modules = $modules
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Host ("Payload: {0} bytes; application binaries: {1}; other payload: {2}; audited modules: {3}" -f $total, $application, ($total - $application), $modules.Count)
