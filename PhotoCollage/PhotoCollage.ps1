param(
    [Parameter(Mandatory = $true)]
    [string]$InputFolder,

    [Parameter(Mandatory = $true)]
    [string]$OutputFile,

    [int]$Cols = 5,
    [int]$MaxImages = 25,
    [ValidateRange(1, 100)]
    [long]$JpegQuality = 80
)

Add-Type -AssemblyName System.Drawing

# --- LOAD FILES ---
$files = Get-ChildItem $InputFolder -Include *.jpg,*.jpeg,*.png,*.bmp -Recurse | Select-Object -First $MaxImages
if ($files.Count -eq 0) {
    throw "No supported images found in $InputFolder"
}
$rows  = [math]::Ceiling($files.Count / $Cols)

# --- READ DIMENSIONS FROM FIRST IMAGE ---
$probe  = [System.Drawing.Image]::FromFile($files[0].FullName)
$photoW = $probe.Width
$photoH = $probe.Height
$probe.Dispose()
Write-Host "Detected photo size: ${photoW}x${photoH}, grid: ${Cols}x${rows}"

# --- BUILD CANVAS ---
$canvas = New-Object System.Drawing.Bitmap -ArgumentList ($Cols * $photoW), ($rows * $photoH)
$g = [System.Drawing.Graphics]::FromImage($canvas)
$g.Clear([System.Drawing.Color]::White)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic

for ($i = 0; $i -lt $files.Count; $i++) {
    $img = [System.Drawing.Image]::FromFile($files[$i].FullName)
    $x = ($i % $Cols) * $photoW
    $y = [math]::Floor($i / $Cols) * $photoH
    $g.DrawImage($img, $x, $y, $photoW, $photoH)
    $img.Dispose()
    Write-Host "[$($i+1)/$($files.Count)] $($files[$i].Name)"
}

$g.Dispose()

# --- SAVE ---
$encoder   = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq "image/jpeg" }
$encParams = New-Object System.Drawing.Imaging.EncoderParameters -ArgumentList 1
$encParams.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter -ArgumentList ([System.Drawing.Imaging.Encoder]::Quality), $JpegQuality
$canvas.Save($OutputFile, $encoder, $encParams)
$canvas.Dispose()

Write-Host "Done! Saved to $OutputFile"
