$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$assetDir = Join-Path $root 'Assets'
New-Item -ItemType Directory -Force -Path $assetDir | Out-Null

function New-TileAsset {
    param(
        [string]$Name,
        [int]$Width,
        [int]$Height,
        [float]$FontSize
    )

    $path = Join-Path $assetDir $Name
    $bitmap = New-Object System.Drawing.Bitmap $Width, $Height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear([System.Drawing.Color]::FromArgb(27, 31, 36))

    $rect = New-Object System.Drawing.RectangleF 0, 0, $Width, $Height
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect,
        ([System.Drawing.Color]::FromArgb(43, 130, 116)),
        ([System.Drawing.Color]::FromArgb(122, 82, 150)),
        35
    $graphics.FillRectangle($brush, $rect)
    $brush.Dispose()

    $font = New-Object System.Drawing.Font 'Segoe UI Symbol', $FontSize, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
    $textBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)
    $format = New-Object System.Drawing.StringFormat
    $format.Alignment = [System.Drawing.StringAlignment]::Center
    $format.LineAlignment = [System.Drawing.StringAlignment]::Center
    $graphics.DrawString([char]0x266A, $font, $textBrush, $rect, $format)

    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)

    $format.Dispose()
    $textBrush.Dispose()
    $font.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
}

New-TileAsset -Name 'Square44x44Logo.png' -Width 44 -Height 44 -FontSize 24
New-TileAsset -Name 'StoreLogo.png' -Width 50 -Height 50 -FontSize 28
New-TileAsset -Name 'Square150x150Logo.png' -Width 150 -Height 150 -FontSize 82
New-TileAsset -Name 'Wide310x150Logo.png' -Width 310 -Height 150 -FontSize 78
New-TileAsset -Name 'Square310x310Logo.png' -Width 310 -Height 310 -FontSize 160

Write-Host "Wrote tile assets to $assetDir"
