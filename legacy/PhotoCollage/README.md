# PhotoCollage

C# console app that creates a simple image grid/collage from images in a folder.

The app reads supported images recursively, uses the first image's dimensions as the cell size, draws each image into a fixed grid, and saves the result using the requested output extension.

## Requirements

- Windows with .NET Framework and `System.Drawing` available.
- Input images in `.jpg`, `.jpeg`, `.png`, or `.bmp` format.

## Build

From this folder:

```cmd
BuildPhotoCollage.cmd
```

Output:

```text
build\PhotoCollage.exe
```

## Run

```cmd
PhotoCollage.cmd -InputFolder "C:\Photos" -OutputFile "C:\Photos\collage.jpg"
```

Optional settings:

```cmd
PhotoCollage.cmd -InputFolder "C:\Photos" -OutputFile "C:\Photos\collage.jpg" -Cols 6 -MaxImages 36 -JpegQuality 85
```

## Parameters

- `-InputFolder`: folder to scan recursively. Required.
- `-OutputFile`: output path ending in `.jpg`, `.jpeg`, `.png`, or `.bmp`. Required.
- `-Cols`: number of columns. Default: `5`.
- `-MaxImages`: maximum number of images to include. Default: `25`.
- `-JpegQuality`: JPEG quality from 1 to 100 when writing `.jpg` or `.jpeg`. Default: `80`.
- `-MaxCanvasMegapixels`: hard limit for the calculated canvas size. Default: `100`; range: `1` through `1024`.
- `-LogFile`: optional log path. Default: `PhotoCollage.log` beside the compiled helper executable. Relative paths resolve from the helper directory.

## Behavior And Limitations

- Image paths are ordered case-insensitively for deterministic output.
- The first image defines the cell size for the whole collage.
- Source images are scaled into cells but not cropped individually.
- Mixed aspect ratios/sizes can produce uneven-looking output.
- The requested output file is excluded from input discovery.
- Canvas dimension arithmetic is checked and the megapixel limit is enforced before allocation.
- Output is written to a temporary file and atomically replaces an existing destination; failed writes do not destroy the prior collage.

## Generated Files

The app creates the requested `-OutputFile` and writes `PhotoCollage.log` beside the compiled helper executable by default.
