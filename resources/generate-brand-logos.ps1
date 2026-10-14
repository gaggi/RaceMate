# Packs the white logos in brand-logos/ into brand-logos.png, one 64 px cell each, and writes
# src/BrandLogos.inc with the brand of every cell. Brand names are those CarBrand() returns.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class BrandLogoPacker {
    static byte[] Alpha(Bitmap b) {
        var data = b.LockBits(new Rectangle(0, 0, b.Width, b.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        var bytes = new byte[data.Stride * b.Height];
        Marshal.Copy(data.Scan0, bytes, 0, bytes.Length); b.UnlockBits(data);
        var alpha = new byte[b.Width * b.Height];
        for (int y = 0; y < b.Height; y++) for (int x = 0; x < b.Width; x++) alpha[y * b.Width + x] = bytes[y * data.Stride + x * 4 + 3];
        return alpha;
    }
    // The logo's visible bounds, ignoring faint stray pixels.
    static Rectangle Bounds(Bitmap b) {
        var alpha = Alpha(b); int left = b.Width, top = b.Height, right = -1, bottom = -1;
        for (int y = 0; y < b.Height; y++) for (int x = 0; x < b.Width; x++) if (alpha[y * b.Width + x] > 8) {
            left = Math.Min(left, x); top = Math.Min(top, y); right = Math.Max(right, x); bottom = Math.Max(bottom, y);
        }
        if (right < 0) throw new Exception("The logo is empty.");
        return Rectangle.FromLTRB(left, top, right + 1, bottom + 1);
    }
    // Fits each logo into its cell, keeping the aspect ratio, and returns the atlas as white with alpha.
    public static Bitmap Pack(string[] files, int cell, int padding, int columns) {
        int rows = (files.Length + columns - 1) / columns;
        var atlas = new Bitmap(columns * cell, rows * cell, PixelFormat.Format32bppArgb);
        using (var g = Graphics.FromImage(atlas)) {
            g.Clear(Color.Transparent);
            g.InterpolationMode = InterpolationMode.HighQualityBicubic; g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            g.CompositingQuality = CompositingQuality.HighQuality; g.SmoothingMode = SmoothingMode.HighQuality;
            for (int i = 0; i < files.Length; i++) using (var logo = new Bitmap(files[i])) {
                var source = Bounds(logo); float inner = cell - 2 * padding;
                float scale = Math.Min(inner / source.Width, inner / source.Height);
                float w = source.Width * scale, h = source.Height * scale;
                float x = i % columns * cell + (cell - w) / 2, y = i / columns * cell + (cell - h) / 2;
                using (var attributes = new ImageAttributes()) {
                    attributes.SetWrapMode(WrapMode.TileFlipXY); // Keeps the edges from fading into the neighbours.
                    g.DrawImage(logo, new[] { new PointF(x, y), new PointF(x + w, y), new PointF(x, y + h) }, source, GraphicsUnit.Pixel, attributes);
                }
            }
        }
        return atlas;
    }
    // An 8-bit palette of white at every alpha: a quarter of the size of 32-bit pixels.
    public static Bitmap ToIndexed(Bitmap atlas) {
        var alpha = Alpha(atlas);
        var indexed = new Bitmap(atlas.Width, atlas.Height, PixelFormat.Format8bppIndexed);
        var palette = indexed.Palette;
        for (int a = 0; a < 256; a++) palette.Entries[a] = Color.FromArgb(a, 255, 255, 255);
        indexed.Palette = palette;
        var data = indexed.LockBits(new Rectangle(0, 0, atlas.Width, atlas.Height), ImageLockMode.WriteOnly, PixelFormat.Format8bppIndexed);
        var bytes = new byte[data.Stride * atlas.Height];
        for (int y = 0; y < atlas.Height; y++) Array.Copy(alpha, y * atlas.Width, bytes, y * data.Stride, atlas.Width);
        Marshal.Copy(bytes, 0, data.Scan0, bytes.Length); indexed.UnlockBits(data);
        return indexed;
    }
}
'@

# Cell order is alphabetical by file; the brand is the name CarBrand() uses.
$brands = [ordered]@{
    'acura'='Acura'; 'aston-martin'='Aston Martin'; 'audi'='Audi'; 'bmw'='BMW'; 'buick'='Buick'; 'cadillac'='Cadillac'
    'chevrolet'='Chevrolet'; 'dallara'='Dallara'; 'dodge'='Dodge'; 'ferrari'='Ferrari'; 'ford'='Ford'; 'holden'='Holden'
    'honda'='Honda'; 'hyundai'='Hyundai'; 'iracing'='iRacing'; 'kia'='Kia'; 'lamborghini'='Lamborghini'; 'lexus'='Lexus'
    'ligier'='Ligier'; 'lotus'='Lotus'; 'mazda'='Mazda'; 'mclaren'='McLaren'; 'mercedes-amg'='Mercedes'; 'nissan'='Nissan'
    'pontiac'='Pontiac'; 'porsche'='Porsche'; 'radical'='Radical'; 'ray'='Ray'; 'renault'='Renault'; 'riley'='Riley'
    'ruf'='RUF'; 'skip-barber'='Skip Barber'; 'subaru'='Subaru'; 'tatuus'='Tatuus'; 'toyota'='Toyota'; 'volkswagen'='Volkswagen'
    'williams'='Williams'
}
$sourceDir = Join-Path $PSScriptRoot 'brand-logos'
$found = Get-ChildItem $sourceDir -Filter *.png | ForEach-Object BaseName | Sort-Object
$unknown = $found | Where-Object { -not $brands.Contains($_) }
$missing = $brands.Keys | Where-Object { $found -notcontains $_ }
if ($unknown -or $missing) { throw "brand-logos/ and the brand list differ. Unknown: $($unknown -join ', '). Missing: $($missing -join ', ')." }

$cell = 64; $padding = 4; $columns = 8
$files = [string[]]($brands.Keys | ForEach-Object { Join-Path $sourceDir "$_.png" })
$atlas = [BrandLogoPacker]::Pack($files, $cell, $padding, $columns)
$indexed = [BrandLogoPacker]::ToIndexed($atlas)
try { $indexed.Save((Join-Path $PSScriptRoot 'brand-logos.png'), [System.Drawing.Imaging.ImageFormat]::Png) }
finally { $indexed.Dispose(); $atlas.Dispose() }

$names = ($brands.Values | ForEach-Object { "`"$_`"" }) -join ','
$inc = @"
// Generated by resources/generate-brand-logos.ps1; do not edit.
// brand-logos.png holds one $cell px cell per brand, $columns per row, in this order.
constexpr int BrandLogoCell=$cell, BrandLogoColumns=$columns;
constexpr const char* BrandLogoNames[]={$names};
"@
Set-Content -Path (Join-Path $PSScriptRoot '..\src\BrandLogos.inc') -Value $inc -Encoding utf8
