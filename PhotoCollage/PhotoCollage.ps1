param(
    [Parameter(Mandatory = $true)]
    [string]$InputFolder,

    [Parameter(Mandatory = $true)]
    [string]$OutputFile,

    [ValidateRange(1, 1000)]
    [int]$Cols = 5,

    [ValidateRange(1, 10000)]
    [int]$MaxImages = 25,

    [ValidateRange(1, 100)]
    [long]$JpegQuality = 80,

    [ValidateRange(1, 1024)]
    [long]$MaxCanvasMegapixels = 100
)

Add-Type -AssemblyName System.Drawing

$outputExtension = [System.IO.Path]::GetExtension($OutputFile).ToLowerInvariant()
if ($outputExtension -notin @(".jpg", ".jpeg", ".png", ".bmp")) {
    throw "Unsupported output extension '$outputExtension'. Use .jpg, .jpeg, .png, or .bmp."
}

# --- LOAD FILES ---
$files = @(Get-ChildItem -LiteralPath $InputFolder -File -Include *.jpg,*.jpeg,*.png,*.bmp -Recurse | Select-Object -First $MaxImages)
if ($files.Count -eq 0) {
    throw "No supported images found in $InputFolder"
}
$rows  = [math]::Ceiling($files.Count / $Cols)

# --- READ DIMENSIONS FROM FIRST IMAGE ---
$probe = $null
try {
    $probe  = [System.Drawing.Image]::FromFile($files[0].FullName)
    $photoW = $probe.Width
    $photoH = $probe.Height
}
finally {
    if ($probe -ne $null) {
        $probe.Dispose()
    }
}
Write-Host "Detected photo size: ${photoW}x${photoH}, grid: ${Cols}x${rows}"

# --- BUILD CANVAS ---
$canvasWidth = [int64]$Cols * [int64]$photoW
$canvasHeight = [int64]$rows * [int64]$photoH
$canvasPixels = $canvasWidth * $canvasHeight
$maxPixels = [int64]$MaxCanvasMegapixels * 1000000
if ($canvasWidth -gt [int]::MaxValue -or $canvasHeight -gt [int]::MaxValue -or $canvasPixels -gt $maxPixels) {
    throw "Canvas would be ${canvasWidth}x${canvasHeight} (${canvasPixels} pixels), above the limit of $MaxCanvasMegapixels megapixels"
}

$canvas = $null
$g = $null
$encParams = $null
try {
    $canvas = New-Object System.Drawing.Bitmap -ArgumentList ([int]$canvasWidth), ([int]$canvasHeight)
    $g = [System.Drawing.Graphics]::FromImage($canvas)
    $g.Clear([System.Drawing.Color]::White)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic

    for ($i = 0; $i -lt $files.Count; $i++) {
        $img = $null
        try {
            $img = [System.Drawing.Image]::FromFile($files[$i].FullName)
            $x = ($i % $Cols) * $photoW
            $y = [math]::Floor($i / $Cols) * $photoH
            $g.DrawImage($img, $x, $y, $photoW, $photoH)
            Write-Host "[$($i+1)/$($files.Count)] $($files[$i].Name)"
        }
        finally {
            if ($img -ne $null) {
                $img.Dispose()
            }
        }
    }

    # --- SAVE ---
    if ($outputExtension -in @(".jpg", ".jpeg")) {
        $encoder = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq "image/jpeg" }
        if ($null -eq $encoder) {
            throw "JPEG encoder was not found."
        }

        $encParams = New-Object System.Drawing.Imaging.EncoderParameters -ArgumentList 1
        $encParams.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter -ArgumentList ([System.Drawing.Imaging.Encoder]::Quality), $JpegQuality
        $canvas.Save($OutputFile, $encoder, $encParams)
    } elseif ($outputExtension -eq ".png") {
        $canvas.Save($OutputFile, [System.Drawing.Imaging.ImageFormat]::Png)
    } else {
        $canvas.Save($OutputFile, [System.Drawing.Imaging.ImageFormat]::Bmp)
    }
}
finally {
    if ($encParams -ne $null) {
        $encParams.Dispose()
    }
    if ($g -ne $null) {
        $g.Dispose()
    }
    if ($canvas -ne $null) {
        $canvas.Dispose()
    }
}

Write-Host "Done! Saved to $OutputFile"
