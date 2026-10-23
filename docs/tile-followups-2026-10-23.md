# Follow-up order from October 23 feedback

The user explicitly put the suggestions below after the existing findings. Finish and validate the current source/notification/performance work, restore the primary Desktop icon, provide a usable wallpaper-only fresh setup, restore confirmed standalone Discord defaults, and handle Gallery activation accurately first.

Confirmed priority requirements:

- Primary Wallpaper uses the original Desktop icon, not the Gallery photo glyph. Existing per-tile icon/branding switches remain effective.
- Fresh profiles start with one main Desktop wallpaper tile, native Live Tile delivery and ordinary Desktop branding. Do not automatically enable the extra source types or launch at sign-in. A fresh VM needs an explicit, discoverable one-time package setup path; Windows 11's normal Start has no Live Tiles.
- Discord defaults follow verified standalone source, rather than the recalled reversed field order: Details is foreground window title; State is CPU and RAM used/total/percentage. Existing explicit values remain unchanged.
- Gallery image activation uses the Windows-reported clicked notification only. No "last submitted image" guess. When this metadata is unavailable, including static Registration tiles, explain and use the folder/settings fallback.

Lower-priority suggestions, to begin after that work is finished:

1. Per-tile icon font selection for Quick Actions: Auto detects Windows 10/11; explicit Segoe MDL2/Segoe Fluent choices; preserve vector fallback when a glyph is missing. This concerns icon fonts, with ordinary text font customization considered separately.
2. Lyrics tile: configurable providers; timed lyrics aligned to the playback timeline, a configurable delivery lead/line skip policy, and low CPU. Research LRCLIB and explicit local LRC first; verify supported authenticated APIs before offering Google/YouTube Music, Musixmatch or Genius integrations. Do not promise precise Windows display timing.
3. Calendar weekly timetable: **the user confirmed rows are days and columns are hours, like the screenshot**. Occupied cells are highlighted; empty cells outlined. Keep the date/agenda tile available as another layout. Respect timezone and event intervals.
4. Fitness/heart-rate tiles only where a usable supported API is available. Google Fit no longer accepts new API clients and is being retired; verify current Google Health options and Mi Fitness/Zepp Life availability before claiming support. No unsupported credential scraping.
5. Battery tile: large charge percentage, charging/plugged-in state, wear percentage when hardware reports design/full capacities, and a configurable right-hand value such as current Wh. Research Battery Mode as a reference and use documented Windows battery interfaces; do not copy custom-licensed implementation code. Unknown hardware values must remain unavailable rather than invented.

The settings reset remains authorized, but must happen only with the final validated priority build. Preserve a complete backup of current settings/accounts/gallery history before installing/resetting and relaunch the existing main package.
