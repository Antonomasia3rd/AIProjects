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

`PhotoCollage.cs` is intentionally only the product assembly-metadata overlay.
The product-owned implementation is compiled from
`dependencies\PhotoCollage\photo_collage_app.cs`, which consumes the root-level
managed named-object, logging, and INI dependencies.

## Run

```cmd
PhotoCollage.cmd -InputFolder "C:\Photos" -OutputFile "C:\Photos\collage.jpg"
```

Optional settings:

```cmd
PhotoCollage.cmd -InputFolder "C:\Photos" -OutputFile "C:\Photos\collage.jpg" -Cols 6 -MaxImages 36 -JpegQuality 85
```

## Persistent Profile

`--configure-only` creates `PhotoCollage.ini` beside the executable or at the
selected `--ini` path. Normal runs and `--show-config` read an existing profile
or use defaults without creating one. The profile stores defaults in its
`[Settings]` section. Relative profile paths resolve from the profile's folder,
which makes a copied profile self-contained.

Use `--set` with `--configure-only` to change it without starting an image job:

```cmd
PhotoCollage.cmd --set Settings.InputFolder="C:\Photos" --set Settings.OutputFile="collage.jpg" --set Settings.Cols=6 --configure-only
```

Use a separate profile when needed:

```cmd
PhotoCollage.cmd --ini "D:\Profiles\weekend.ini" --set Settings.MaxImages=36 --configure-only
PhotoCollage.cmd --ini "D:\Profiles\weekend.ini" --show-config
```

`--set` is intentionally rejected without `--configure-only`. A configuration
command therefore cannot also create or replace a collage. The existing image
options remain one-run overrides and do not modify the profile.

## Parameters

- `-InputFolder`: folder to scan recursively. Supply it here or in the INI for image jobs.
- `-OutputFile`: output path ending in `.jpg`, `.jpeg`, `.png`, or `.bmp`. Supply it here or in the INI for image jobs.
- `-Cols`: number of columns. Default: `5`.
- `-MaxImages`: maximum number of images to include. Default: `25`.
- `-JpegQuality`: JPEG quality from 1 to 100 when writing `.jpg` or `.jpeg`. Default: `80`.
- `-MaxCanvasMegapixels`: hard limit for the calculated canvas size. Default: `100`; range: `1` through `1024`.
- `-LogFile`: optional log path. Default: `PhotoCollage.log` beside the compiled helper executable. Relative paths resolve from the helper directory; an empty value restores that default.
- `--ini`: optional profile path. Explicit relative paths resolve from the working directory, like DesktopStub; the default is `PhotoCollage.ini` beside the executable.
- `--set Settings.Key=Value`: persist a `[Settings]` value. Supported keys are `InputFolder`, `OutputFile`, `Cols`, `MaxImages`, `JpegQuality`, `MaxCanvasMegapixels`, and `LogFile`. Requires `--configure-only`.
- `--configure-only`: create, validate, and optionally update the profile without reading images or creating output.
- `--show-config`: print the effective profile values without reading images or creating output.
- `--help`: show usage and exit before strict argument parsing or filesystem access.
- `--version`: show the binary version and exit before strict argument parsing or filesystem access.

## Behavior And Limitations

- Image paths are ordered case-insensitively for deterministic output.
- Inaccessible subdirectories, directory reparse points, and unreadable/corrupt supported-extension files are reported and skipped. `-MaxImages` counts readable images, so one bad file does not prevent later valid files from being included.
- The first image defines the cell size for the whole collage.
- Source images are scaled into cells but not cropped individually.
- Mixed aspect ratios/sizes can produce uneven-looking output.
- The requested output file is excluded from input discovery.
- Canvas dimension arithmetic is checked and the megapixel limit is enforced before allocation.
- Output is written to a temporary file and atomically replaces an existing destination; failed writes do not destroy the prior collage.
- A source that becomes unreadable after the initial probe is skipped during rendering; the run fails without replacing the output if no source can be rendered.

## Generated Files

Image jobs create the requested output and write the configured log. The default
log is beside the selected INI; `-LogFile` keeps its executable-relative behavior.
`--configure-only` and the tray editor's Save button create or update the INI. Log appends
use the shared cross-process UTF-8 dependency and report persistence failures.

Both this tool and TaskSchedulerMigration consume `managed_profile.cs` for
assignments, paths and final-value validation. `--set` can repair an invalid
saved value. An empty `--ini` is rejected, and output/log/INI file paths must be
distinct to avoid corrupting one another. Read-only inspection never starts an
image job.

## Configuration tray

Run `PhotoCollage.exe --tray --ini "PhotoCollage.ini"` to open the optional
configuration tray. Double-click the icon or select **Settings...** to edit the
same `[Settings]` values accepted by `--set`. Save validates the complete profile
and can repair several invalid fields together; Cancel leaves the file alone.
Only edited fields are saved, preserving unrelated external edits and comments.
Relative paths use the selected INI's directory, like persistent CLI settings.

The tray contains no image-job action and does not create an INI until Save.
Existing one-run CLI options remain available separately. Menu/editor logic is
shared with TaskSchedulerMigration in `dependencies/managed_configuration_tray.cs`;
each executable/profile pair has one tray instance and a nonempty hover label.
No custom runtime DLL was introduced. The editor's actual controls and Save/Cancel
handlers have offscreen tests; visible notification-area acceptance remains open.

The **Start tray at sign-in (Startup folder)** checkbox, `--startup` /
`--no-startup`, and `[Settings] RunAtStartup=0|1` share the same preference.
For example, `PhotoCollage.exe --startup --ini "PhotoCollage.ini"` saves and
applies it without running an image job. The shortcut is placed only in the
current user's Startup folder and launches `--tray --ini` with the exact profile.
Manual INI changes apply on tray launch, Reload configuration, or menu refresh.
`--configure-only` rejects Startup changes; use the explicit management switches.
Startup management can include a validated `--set` batch, but cannot include
one-run image arguments. Real Startup-folder integration remains an opt-in test.
