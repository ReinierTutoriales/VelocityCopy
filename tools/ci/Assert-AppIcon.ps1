param(
    [Parameter(Mandatory)][string]$ExePath,
    [Parameter(Mandatory)][string]$IcoPath,
    [switch]$FirstGroup
)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class AppIconResources {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll")] public static extern bool FreeLibrary(IntPtr module);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr FindResourceW(IntPtr module, IntPtr name, IntPtr type);
    [DllImport("kernel32.dll")] public static extern uint SizeofResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll")] public static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll")] public static extern IntPtr LockResource(IntPtr resource);
    public delegate bool ResourceNameCallback(IntPtr module, IntPtr type, IntPtr name, IntPtr context);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)]
    public static extern bool EnumResourceNamesW(IntPtr module, IntPtr type, ResourceNameCallback callback, IntPtr context);
    public static IntPtr FirstGroup(IntPtr module) {
        IntPtr result = IntPtr.Zero;
        ResourceNameCallback callback = (m,t,n,c) => { result=n; return false; };
        EnumResourceNamesW(module, new IntPtr(14), callback, IntPtr.Zero);
        return result;
    }
    public static byte[] Read(IntPtr module, IntPtr name, int type) {
        var resource=FindResourceW(module,name,new IntPtr(type));
        if(resource==IntPtr.Zero) throw new InvalidOperationException("Missing icon resource");
        var data=LockResource(LoadResource(module,resource));
        int size=checked((int)SizeofResource(module,resource));
        if(data==IntPtr.Zero || size==0) throw new InvalidOperationException("Empty icon resource");
        var bytes=new byte[size]; Marshal.Copy(data,bytes,0,size); return bytes;
    }
}
'@
function U16([byte[]]$bytes, [int]$offset) { [BitConverter]::ToUInt16($bytes, $offset) }
function U32([byte[]]$bytes, [int]$offset) { [BitConverter]::ToUInt32($bytes, $offset) }
function Hash([byte[]]$bytes) { [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)) }

$ico = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $IcoPath).Path)
if ($ico.Length -lt 6 -or (U16 $ico 0) -ne 0 -or (U16 $ico 2) -ne 1) { throw 'Invalid ICO header' }
$count = U16 $ico 4
if ($ico.Length -lt 6 + 16 * $count) { throw 'Truncated ICO directory' }
$frames = @{}
for ($i=0; $i -lt $count; $i++) {
    $entry = 6 + 16 * $i
    $width = if ($ico[$entry]) { [int]$ico[$entry] } else { 256 }
    $height = if ($ico[$entry+1]) { [int]$ico[$entry+1] } else { 256 }
    $length = U32 $ico ($entry+8)
    $offset = U32 $ico ($entry+12)
    if ($width -ne $height -or !$length -or [long]$offset + $length -gt $ico.Length) { throw 'Invalid ICO frame' }
    if ($frames.ContainsKey($width)) { throw 'Duplicate ICO size' }
    $bytes = [byte[]]::new($length)
    [Array]::Copy($ico, $offset, $bytes, 0, $length)
    $frames[$width] = Hash $bytes
}
foreach ($size in @(16,20,24,28,32,40,48,64,80,96,128,256)) {
    if (!$frames.ContainsKey($size)) { throw "Missing icon resolution: $size" }
}

# Exercise the native ICO decoder, including PNG-backed 256px icons. The solid
# silhouette must fill the canvas optically; transparent padding must not regress.
Add-Type -AssemblyName System.Drawing
foreach ($size in @(16,32,48,256)) {
    $stream = [IO.MemoryStream]::new($ico, $false)
    $icon = $null
    $bitmap = $null
    try {
        $icon = [Drawing.Icon]::new($stream, $size, $size)
        $bitmap = $icon.ToBitmap()
        if ($bitmap.Width -ne $size -or $bitmap.Height -ne $size) { throw "ICO decoding selected incorrect size: $size" }
        $left = $size; $top = $size; $right = -1; $bottom = -1
        for ($y=0; $y -lt $size; $y++) {
            for ($x=0; $x -lt $size; $x++) {
                if ($bitmap.GetPixel($x,$y).A -ge 128) {
                    $left = [Math]::Min($left,$x); $right = [Math]::Max($right,$x)
                    $top = [Math]::Min($top,$y); $bottom = [Math]::Max($bottom,$y)
                }
            }
        }
        if ($right-$left+1 -lt $size*0.87 -or $bottom-$top+1 -lt $size*0.87 -or
            [Math]::Abs($left-($size-1-$right)) -gt 2 -or [Math]::Abs($top-($size-1-$bottom)) -gt 2) {
            throw "ICO silhouette is undersized or off-center at ${size}x${size}"
        }
    } finally {
        if ($bitmap) { $bitmap.Dispose() }
        if ($icon) { $icon.Dispose() }
        $stream.Dispose()
    }
}

# LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE: inspect either
# architecture without executing code or loading the app's dependencies.
$module = [AppIconResources]::LoadLibraryExW((Resolve-Path -LiteralPath $ExePath).Path, [IntPtr]::Zero, 0x22)
if ($module -eq [IntPtr]::Zero) { throw 'Cannot load executable resources' }
try {
    $id = if ($FirstGroup) { [AppIconResources]::FirstGroup($module) } else { [IntPtr]::new(1) }
    if ($id -eq [IntPtr]::Zero) { throw 'Missing application icon group' }
    $group = [AppIconResources]::Read($module, $id, 14)
    if ($group.Length -lt 6 -or (U16 $group 2) -ne 1) { throw 'Invalid icon group' }
    $groupCount = U16 $group 4
    if ($groupCount -ne $count -or $group.Length -lt 6 + 14 * $groupCount) { throw 'Embedded icon resolutions differ from ICO' }
    $seen = @{}
    for ($i=0; $i -lt $groupCount; $i++) {
        $entry = 6 + 14 * $i
        $size = if ($group[$entry]) { [int]$group[$entry] } else { 256 }
        $resourceId = U16 $group ($entry+12)
        $bytes = [AppIconResources]::Read($module, [IntPtr]::new($resourceId), 3)
        if (!$frames.ContainsKey($size) -or (Hash $bytes) -ne $frames[$size] -or $seen.ContainsKey($size)) {
            throw "Embedded icon mismatch at ${size}x${size}"
        }
        $seen[$size] = $true
    }
    Write-Host "Verified identical $count-resolution icon in $ExePath"
} finally {
    [void][AppIconResources]::FreeLibrary($module)
}
