# PhotoCollage

PowerShell script that creates a simple image grid/collage from images in a folder.

The script reads supported images recursively, uses the first image's dimensions as the cell size, draws each image into a fixed grid, and saves the result using the requested output extension.

## Requirements

- Windows PowerShell with `System.Drawing` available.
- Input images in `.jpg`, `.jpeg`, `.png`, or `.bmp` format.

## Run

```powershell
powershell -ExecutionPolicy Bypass -File .\PhotoCollage.ps1 -InputFolder "C:\Photos" -OutputFile "C:\Photos\collage.jpg"
```

Optional settings:

```powershell
powershell -ExecutionPolicy Bypass -File .\PhotoCollage.ps1 -InputFolder "C:\Photos" -OutputFile "C:\Photos\collage.jpg" -Cols 6 -MaxImages 36 -JpegQuality 85
```

## Parameters

- `-InputFolder`: folder to scan recursively. Required.
- `-OutputFile`: output path ending in `.jpg`, `.jpeg`, `.png`, or `.bmp`. Required.
- `-Cols`: number of columns. Default: `5`.
- `-MaxImages`: maximum number of images to include. Default: `25`.
- `-JpegQuality`: JPEG quality from 1 to 100 when writing `.jpg` or `.jpeg`. Default: `80`.
- `-LogFile`: optional log path. Default: `PhotoCollage.log` beside `PhotoCollage.ps1`. Relative paths resolve from the script directory.

## Behavior And Limitations

- Image order follows the recursive file enumeration order returned by PowerShell.
- The first image defines the cell size for the whole collage.
- Source images are scaled into cells but not cropped individually.
- Mixed aspect ratios/sizes can produce uneven-looking output.
- Existing output files are overwritten by `System.Drawing` save behavior if the path can be written.

## Generated Files

The script creates the requested `-OutputFile` and writes `PhotoCollage.log` beside the script by default.
