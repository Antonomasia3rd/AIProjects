### Contents:
* DesktopStub

# DesktopStub
## Why was this made?
* simulating the "Desktop" tile behavior on Windows 8 and 8.1
## Disclaimer
* this might NOT be the original implementation on Windows 8/8.1 itself
* multimon wallpaper span is broken, since this program acquires from the wallpaper file, not from how it actually shows on your desktop (i'll make this an Issue later)
## Planned feature(s)
* AppxRegister methods (currently it's using Powershell, which is very heavy on lowend systems)
* Multi-monitor support (see Disclaimer #2)
  - Detect used wallpaper method (Fill / Tile / Span / etc.)
  - Detect connected monitor resolution
  - Detect if Start Screen is opened on which monitor, if yes then the DesktopStub will be re-registered with a suitable wallpaper on that monitor, methods:
    - look for the class window responsible for the start screen, or
    - detect which monitor the cursor is currently in (NOTE: causes CPU spike for heavy multimon users)
## NOTE: this code was unmaintained
colleg 😭
