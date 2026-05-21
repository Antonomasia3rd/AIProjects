# PhotoCollage

PowerShell script that creates a simple image grid/collage from images in a folder.

The script reads supported images recursively, uses the first image's dimensions as the cell size, draws each image into a fixed grid, and saves the result as a JPEG.

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
- `-OutputFile`: JPEG output path. Required.
- `-Cols`: number of columns. Default: `5`.
- `-MaxImages`: maximum number of images to include. Default: `25`.
- `-JpegQuality`: JPEG quality from 1 to 100. Default: `80`.

## Behavior And Limitations

- Image order follows the recursive file enumeration order returned by PowerShell.
- The first image defines the cell size for the whole collage.
- Source images are scaled into cells but not cropped individually.
- Mixed aspect ratios/sizes can produce uneven-looking output.
- Existing output files are overwritten by `System.Drawing` save behavior if the path can be written.

## Generated Files

The script creates only the requested `-OutputFile`.
