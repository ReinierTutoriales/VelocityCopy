param(
    [Parameter(Mandatory)][string]$RootPath,
    [Parameter(Mandatory)][string]$AssetsPath,
    [Parameter(Mandatory)][string]$OutputPath
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $RootPath).Path
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$dumpbin = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe' |
    Select-Object -First 1
if (-not $dumpbin) { throw 'DUMPBIN was not found' }
$files = @(Get-ChildItem -LiteralPath $root -Recurse -File)
$assets = Get-Content -LiteralPath $AssetsPath -Raw | ConvertFrom-Json -AsHashtable
if (-not $assets.libraries -or -not $assets.packageFolders) { throw 'Invalid NuGet assets graph' }
# Index candidates once; compare bytes, not filenames, before assigning provenance.
$candidates = @{}
$payloadNames = @{}
foreach ($file in $files) { $payloadNames[$file.Name] = $true }
foreach ($library in $assets.libraries.GetEnumerator()) {
    if ($library.Value.type -ne 'package') { continue }
    foreach ($folder in $assets.packageFolders.Keys) {
        $packageRoot = Join-Path $folder $library.Value.path
        if (-not (Test-Path -LiteralPath $packageRoot -PathType Container)) { continue }
        foreach ($relative in $library.Value.files) {
            $name = [IO.Path]::GetFileName($relative)
            if (-not $payloadNames.ContainsKey($name)) { continue }
            if (-not $candidates.ContainsKey($name)) { $candidates[$name] = [Collections.Generic.List[object]]::new() }
            $candidates[$name].Add([pscustomobject]@{ package = $library.Key; path = Join-Path $packageRoot $relative })
        }
    }
}
$hashes = @{}
$provenance = @($files | Sort-Object FullName | ForEach-Object {
    $file = $_
    $owners = @()
    if ($candidates.ContainsKey($file.Name)) {
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        $owners = @($candidates[$file.Name] | ForEach-Object {
            $candidate = $_
            $source = Get-Item -LiteralPath $candidate.path
            if ($source.Length -eq $file.Length) {
                if (-not $hashes.ContainsKey($source.FullName)) {
                    $hashes[$source.FullName] = (Get-FileHash -LiteralPath $source.FullName -Algorithm SHA256).Hash
                }
                if ($hashes[$source.FullName] -eq $hash) { $candidate.package }
            }
        } | Sort-Object -Unique)
    }
    [ordered]@{
        path = [IO.Path]::GetRelativePath($root, $file.FullName)
        bytes = $file.Length
        matching_packages = $owners
    }
})
$applicationArtifacts = @(
    'App.xbf', 'DesignTokens.xbf', 'MainWindow.xbf',
    'Assets\VelocityCopy.ico', 'Assets\VelocityCopy.png',
    'VelocityCopy.WinUI.exe', 'VelocityCopy.Shell.dll', 'VelocityCopy.StartupHelper.exe',
    'VelocityCopy.WinUI.pri', 'VelocityCopyUI.winmd'
)
$unreviewed = @($provenance | Where-Object {
    $_.matching_packages.Count -eq 0 -and $_.path -notin $applicationArtifacts
})
if ($unreviewed.Count) {
    throw "Payload has files without reviewed application or byte-identical NuGet provenance: $($unreviewed.path -join ', ')"
}
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
foreach ($module in $modules) {
    $name = [IO.Path]::GetFileName($module.path)
    $module.imported_by = @($modules | Where-Object { $_.imports -contains $name } | ForEach-Object { $_.path })
}
$types = @($files | Group-Object Extension | Sort-Object Name | ForEach-Object {
    [ordered]@{ extension = $_.Name; count = $_.Count; bytes = [long]($_.Group | Measure-Object Length -Sum).Sum }
})
$total = [long]($files | Measure-Object Length -Sum).Sum
$application = [long]($appFiles | Measure-Object Length -Sum).Sum
[ordered]@{
    schema = 2
    bytes = $total
    application_binary_bytes = $application
    other_payload_bytes = $total - $application
    note = 'Static and delay-load imports only. Absence is not proof of non-use: WinRT activation, LoadLibrary, resources and optional paths require separate validation. Other payload includes runtime AND application resources.'
    modules = $modules
    file_types = $types
    resolved_packages = @($assets.libraries.Keys | Sort-Object)
    provenance = $provenance
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Host ("Payload: {0} bytes; application binaries: {1}; other payload: {2}; audited modules: {3}" -f $total, $application, ($total - $application), $modules.Count)
Write-Host ("NuGet byte-identical provenance: {0}/{1} files" -f @($provenance | Where-Object { $_.matching_packages.Count -gt 0 }).Count, $files.Count)
