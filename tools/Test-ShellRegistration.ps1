param([ValidateSet('SeedLegacy','Installed','Uninstalled')][string]$Phase)
$ErrorActionPreference = 'Stop'
$legacy = @(
  '*\shell\VelocityCopy.Copy', '*\shell\VelocityCopy.CopyTo',
  'Directory\shell\VelocityCopy.Copy', 'Directory\shell\VelocityCopy.CopyTo',
  'Directory\shell\VelocityCopy.Paste', 'Directory\Background\shell\VelocityCopy.Paste',
  'Directory\Background\shell\VelocityCopy.Open',
  'CLSID\{7E1D27A7-BA17-4EEA-9B93-967EE777BD21}',
  'CLSID\{CBBA1A7E-35B4-4708-9D03-9446D03FC843}',
  'CLSID\{D0B92E7D-7A23-4C9A-9AE2-2B2A1A6F3A0D}',
  'CLSID\{A6209C12-10B0-4D25-8BF3-2D3C3E6A7B11}'
)
$clsid = '{6BD80C35-7CE8-4A63-92D4-51AF4DACB821}'
$registry = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine','Registry64')
try {
  foreach ($path in $legacy) {
    $full = 'Software\Classes\' + $path
    if ($Phase -eq 'SeedLegacy') {
      $key = $registry.CreateSubKey($full)
      $key.SetValue('LegacySmokeMarker','1')
      $key.Dispose()
    } else {
      $key = $registry.OpenSubKey($full)
      if ($null -ne $key) { $key.Dispose(); throw "Retired registration remains: $full" }
    }
  }
  if ($Phase -eq 'SeedLegacy') { return }
  foreach ($kind in 'Directory','Drive','Folder') {
    $path = "Software\Classes\$kind\shellex\DragDropHandlers\VelocityCopy"
    $key = $registry.OpenSubKey($path)
    try {
      if ($Phase -eq 'Installed') {
        if ($null -eq $key -or $key.GetValue('') -ne $clsid) { throw "Missing drop registration: $path" }
      } elseif ($null -ne $key) { throw "Registration remains: $path" }
    } finally { if ($key) { $key.Dispose() } }
  }
  $key = $registry.OpenSubKey("Software\Classes\CLSID\$clsid\InprocServer32")
  try {
    if ($Phase -eq 'Installed') {
      if ($null -eq $key -or $key.GetValue('ThreadingModel') -ne 'Apartment' -or
          $key.GetValue('') -ne (Join-Path $env:ProgramFiles 'VelocityCopy\VelocityCopy.Shell.dll')) {
        throw 'Invalid native COM server registration'
      }
    } elseif ($null -ne $key) { throw 'COM server registration remains' }
  } finally { if ($key) { $key.Dispose() } }
} finally { $registry.Dispose() }
