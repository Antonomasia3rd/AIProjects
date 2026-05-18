Add-Type -AssemblyName System.Drawing

# --- CONFIG ---
$inputFolder = "D:\Users\Amiya\OneDrive\College\VI - Asistensi Praktikum Rangkaian Listrik\27 April\12"
$outputFile  = "D:\Users\Amiya\Desktop\12.jpg"
$cols = 5

# --- LOAD FILES ---
$files = Get-ChildItem $inputFolder -Include *.jpg,*.jpeg,*.png,*.bmp -Recurse | Select-Object -First 25
$rows  = [math]::Ceiling($files.Count / $cols)

# --- READ DIMENSIONS FROM FIRST IMAGE ---
$probe  = [System.Drawing.Image]::FromFile($files[0].FullName)
$photoW = $probe.Width
$photoH = $probe.Height
$probe.Dispose()
Write-Host "Detected photo size: ${photoW}x${photoH}, grid: ${cols}x${rows}"

# --- BUILD CANVAS ---
$canvas = New-Object System.Drawing.Bitmap -ArgumentList ($cols * $photoW), ($rows * $photoH)
$g = [System.Drawing.Graphics]::FromImage($canvas)
$g.Clear([System.Drawing.Color]::White)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic

for ($i = 0; $i -lt $files.Count; $i++) {
    $img = [System.Drawing.Image]::FromFile($files[$i].FullName)
    $x = ($i % $cols) * $photoW
    $y = [math]::Floor($i / $cols) * $photoH
    $g.DrawImage($img, $x, $y, $photoW, $photoH)
    $img.Dispose()
    Write-Host "[$($i+1)/$($files.Count)] $($files[$i].Name)"
}

$g.Dispose()

# --- SAVE ---
$encoder   = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq "image/jpeg" }
$encParams = New-Object System.Drawing.Imaging.EncoderParameters -ArgumentList 1
$encParams.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter -ArgumentList ([System.Drawing.Imaging.Encoder]::Quality), 80L
$canvas.Save($outputFile, $encoder, $encParams)
$canvas.Dispose()

Write-Host "Done! Saved to $outputFile"