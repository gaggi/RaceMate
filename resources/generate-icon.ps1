$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class RaceMateIconHandles {
    [DllImport("user32.dll")] public static extern bool DestroyIcon(IntPtr handle);
}
'@
$bitmap = [System.Drawing.Bitmap]::new(32,32)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$graphics.Clear([System.Drawing.Color]::FromArgb(255,27,32,41))
$pen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255,246,204,66),3)
$graphics.DrawLine($pen,9,25,9,7)
$points = [System.Drawing.PointF[]]@([System.Drawing.PointF]::new(9,8),[System.Drawing.PointF]::new(18,8),[System.Drawing.PointF]::new(23,12),[System.Drawing.PointF]::new(18,17),[System.Drawing.PointF]::new(9,17))
$graphics.DrawLines($pen,$points)
$graphics.DrawLine($pen,16,17,24,25)
$handle = $bitmap.GetHicon()
$icon = [System.Drawing.Icon]::FromHandle($handle)
$stream = [System.IO.File]::Create((Join-Path $PSScriptRoot 'RaceMate.ico'))
try { $icon.Save($stream) } finally {
    $stream.Dispose()
    $icon.Dispose()
    [RaceMateIconHandles]::DestroyIcon($handle) | Out-Null
    $pen.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
}
