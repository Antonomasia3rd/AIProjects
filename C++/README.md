### Contents:
* DesktopStub

# DesktopStub
## Why was this made?
* simulating the "Desktop" tile behavior on Windows 8 and 8.1
## Disclaimer
* this might NOT be the original implementation on Windows 8/8.1 itself
* multimon wallpaper span is broken, since this program acquires from the wallpaper file, not from how it actually shows on your desktop (i'll make this an Issue later)
## Feature(s)
* Console (for real-time monitoring)
* Logging to file (as well as changing the log path)
* Tray icon (enabled by default, can be disabled if you want seamless transformation)
* Specific asset(s) generation only (e.g. you might want to enable only SmallTile, MediumTile, WideTile, and LargeTile only for accurate setup), other icons use the placeholder logo from Windows 8/8.1's Desktop icon
### all config(s) are stored on %exename%.ini, on the same folder as the .exe itself
## Planned feature(s)
* AppxRegister methods (currently it's using Powershell, which is very heavy on lowend systems)
* High-DPI support (currenty it's fixed to a specific resolution, the largest at 310x310, which makes it blurry on higher DPI monitors)
* Multi-monitor support (see Disclaimer #2)
  - Detect used wallpaper method (Fill / Tile / Span / etc.)
  - Detect connected monitor resolution
  - Detect if Start Screen is opened on which monitor, if yes then the DesktopStub will be re-registered with a suitable wallpaper on that monitor, methods:
    - look for the class window responsible for the start screen, or
    - detect which monitor the cursor is currently in (NOTE: causes CPU spike for heavy multimon users)
## NOTE: this code was unmaintained
colleg 😭
