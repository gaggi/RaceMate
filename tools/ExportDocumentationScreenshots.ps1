# Convert native diagnostic captures to the PNGs used by the Markdown guides.
# First run .\build.ps1 -Diagnostics -NoPackage from the repository root.
param()
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$repoRoot = Split-Path -Parent $PSScriptRoot
$captureRoot = Join-Path $repoRoot 'build/qa'
$destinationRoot = Join-Path $repoRoot 'docs/images'
# Each image: its source captures, side by side when there are several, and optionally the part of a
# main-window capture it shows (x, y, width, height in capture pixels at 100 % scaling), without the
# sidebar and the preview banner around it.
$images = [ordered]@{
    'main-overlays'         = @{ Sources = @('main-overlays') }
    'layouts'               = @{ Sources = @('main-layouts'); Crop = @(310, 612, 1132, 305) }
    'settings'              = @{ Sources = @('main-settings'); Crop = @(310, 185, 1132, 895) }
    'relative-settings'     = @{ Sources = @('main-0'); Crop = @(310, 180, 1132, 900) }
    'relative'              = @{ Sources = @('overlay-0') }
    'standings'             = @{ Sources = @('standings-interval') }
    'standings-pit-exit'    = @{ Sources = @('standings-pit-exit-around') }
    'fuel-calculator'       = @{ Sources = @('fuel-open') }
    'fuel-save'             = @{ Sources = @('fuel-save') }
    'fuel-surplus'          = @{ Sources = @('fuel-surplus') }
    'fuel-strip'            = @{ Sources = @('fuel-strip') }
    'pit-stop-settings'     = @{ Sources = @('settings-pit-stop'); Crop = @(310, 810, 1132, 270) }
    'pit-assist'            = @{ Sources = @('pit-setup-changes', 'pit-turn-off-limiter') }
    'pit-lane-progress'     = @{ Sources = @('pit-strip-approach', 'pit-strip-lane', 'pit-strip-exit') }
    'weather'               = @{ Sources = @('weather-all') }
    'wind-rose'             = @{ Sources = @('weather-wind-rose') }
    'track-map'             = @{ Sources = @('trackmap-corner-labels-353') }
    'track-map-transparent' = @{ Sources = @('trackmap-transparent-353') }
    'track-map-circle'      = @{ Sources = @('trackmap-circle') }
    'track-map-line'        = @{ Sources = @('trackmap-line') }
    'race-control'          = @{ Sources = @('race-control') }
    'race-control-fields'   = @{ Sources = @('race-control-extra-fields') }
}
# Check every source first so a missing optional cached map does not leave a partial export.
foreach ($image in $images.Values) {
    foreach ($capture in $image.Sources) {
        $source = Join-Path $captureRoot "$capture.bmp"
        if (-not (Test-Path -LiteralPath $source)) {
            throw "Missing $source. Run the GUI diagnostics first; the Map captures also need cached Lime Rock GP assets and corner labels."
        }
    }
}
New-Item -ItemType Directory -Path $destinationRoot -Force | Out-Null
$gap = 24
foreach ($name in $images.Keys) {
    $image = $images[$name]
    $bitmaps = @($image.Sources | ForEach-Object { [System.Drawing.Bitmap]::new((Join-Path $captureRoot "$_.bmp")) })
    try {
        if ($image.Crop) { $x, $y, $width, $height = $image.Crop }
        else {
            $x = 0; $y = 0
            $width = ($bitmaps | Measure-Object -Property Width -Sum).Sum + $gap * ($bitmaps.Count - 1)
            $height = ($bitmaps | Measure-Object -Property Height -Maximum).Maximum
        }
        # A transparent canvas: the gap between side-by-side captures takes the page's own background.
        $result = [System.Drawing.Bitmap]::new($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($result)
            try {
                $graphics.Clear([System.Drawing.Color]::Transparent)
                $left = 0
                foreach ($bitmap in $bitmaps) {
                    $source = [System.Drawing.Rectangle]::new($x, $y, [Math]::Min($width, $bitmap.Width - $x), [Math]::Min($height, $bitmap.Height - $y))
                    $graphics.DrawImage($bitmap, [System.Drawing.Rectangle]::new($left, 0, $source.Width, $source.Height), $source, [System.Drawing.GraphicsUnit]::Pixel)
                    $left += $source.Width + $gap
                }
            }
            finally { $graphics.Dispose() }
            $destination = Join-Path $destinationRoot "$name.png"
            $result.Save($destination, [System.Drawing.Imaging.ImageFormat]::Png)
            Write-Output $destination
        }
        finally { $result.Dispose() }
    }
    finally { $bitmaps | ForEach-Object { $_.Dispose() } }
}
