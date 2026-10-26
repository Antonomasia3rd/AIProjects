# Properties, source health and host preparation — October 26

This follow-up implements the latest Properties/Kuro feedback on the v27 beta
baseline. The independent [1809/lock-screen investigation](desktopstub-1809-history-and-lockscreen-2026-10-26.md)
and [provider feasibility report](desktopstub-provider-parity-feasibility-2026-10-26.md)
record platform findings without treating research as implemented providers.

## Properties appearance

The unwanted filled hover/pressed rectangle is removed from setting rows,
navigation and Restart. Text color, pressed weight and the existing underlined
navigation selection carry interaction feedback. The old background fill is
not an ExplorerPatcher requirement.

The default Windows color source is now a theme-appropriate accent shade:
`AccentLight1` on dark surfaces and `AccentDark1` on light surfaces. Available
sources are `Auto`, `Accent`, `AccentLight1/2/3`, `AccentDark1/2/3`, `Dwm`,
`SystemHighlight`, and `SystemHotlight`. These names are exact INI/CLI enums;
the slash notation above abbreviates three independent choices.

`Properties.HighlightContrast=Readable` adjusts unsuitable colors toward
black/white until normal text reaches a 4.5:1 contrast target against the
theme surface. It checks a conservative Mica surface as well as the opaque
fallback. Mica is composited by Windows, so this does not measure every final
pixel of an arbitrary customized compositor. `Exact` is an explicit opt-out
for users who want their raw chosen color. High Contrast keeps a readable
system-derived link color even if Exact was selected.

All choices are exposed through Application > Interface > Properties window
in the shared tray/Properties model, the INI and the normal CLI:

```ini
[Properties]
HighlightMode=Accent
HighlightSource=Auto
HighlightContrast=Readable
HighlightColor=#0078D4
CaptionMode=Properties
```

```powershell
.\DesktopStub.exe --configure-only --set Properties.HighlightSource=Auto --set Properties.HighlightContrast=Readable
```

Custom mode continues to use `HighlightColor`; selecting a Windows source does
not overwrite that saved custom color. UISettings palette reads fall back to
DWM/system colors when unavailable. No observer or polling worker is added.

The requested [Accent Color Synchronizer's own documentation](https://www.lematheuxparesseux.altervista.org/app/accentcolorsynchronizer.html)
distinguishes the Windows accent from the title-bar color and offers brightness
adjustments. DesktopStub uses supported read-only APIs rather than changing
the system color table. Microsoft documents [UISettings accent retrieval in
classic desktop programs](https://devblogs.microsoft.com/oldnewthing/20170405-00/?p=95905)
and the [light/dark accent palette](https://learn.microsoft.com/en-us/windows/apps/develop/ui/theming).

## Profiles

Profiles now use the same Properties window, sidebar hierarchy and setting
rows as the other categories. The tray's Profiles action and
`--profile-manager` open this section in the existing singleton window.
Each profile has a stable-ID subpage with its path/status and existing actions.
The backend retains ownership, captured-revision and migration checks; a stale
page cannot silently target a changed profile. Separate tray icons remain the
default. See the [Profiles integration evidence](desktopstub-profiles-properties-2026-10-26.md).

## Live Tile host preparation

The setup action is under Advanced > Profile package identity and setup, named
“Prepare Live Tile support (keep tile delivery)...”. The ordinary Tiles menu
does not display it as a first-run prerequisite. Packaged processes show a
ready status only when package name, publisher, root and application ID match
this profile; unpackaged processes can explicitly prepare/retry the host.

```powershell
.\DesktopStub.exe --prepare-live-tile-host
```

`--setup-desktop` remains an alias. Both actions prepare the main package and
restart with package identity after successful completion. They keep every
saved tile delivery choice. The frozen setup snapshot routes to the main
package even if the Desktop tile otherwise uses independent Registration.
Its in-memory receipt verifies the unchanged original policy and permits only
the main-host routing override; an unrelated policy change is still rejected.
Ordinary Registration/Main first run still needs no command. Choosing LiveTile
on a particular tile remains an explicit per-tile preference.

Failure, interrupted recovery and Exit retain the previous admission rules;
an interrupted marker does not authorize a new background deployment. Setup
uses the configured primary Wallpaper image and requires it to be available.

## Kuro refresh state

The new reusable SourceHealth policy is active for WuWa. It counts actual
completed fetch attempts, keeps useful last-good resources, formats last-success
age and gates tile errors and popup notices separately. The default waits for
five failed attempts, with an optional elapsed-time threshold. A success clears
failure state. See [the source-health configuration and evidence](wuwa-source-health-2026-10-26.md).

No new network timer is added for the age label: normal demanded composition
updates its bucket. Five retries do not imply fifty minutes unless the user's
configured cadence makes that true. Source settings, accounts and credentials
remain independent per tile/profile.

## Validation and delivery

Final portable/inert checks passed after clean Defender scans:

- Properties model/color policy: **1,313 checks**.
- Profiles page model: **39 checks**.
- Shared source-health policy: **59 checks**.
- Kuro provider: **99 checks**.
- Combined real-host fixture: **2,157 checks**, including the actual CLI and
  shared GUI writers, WuWa semantic XML/cache keys, Profiles backend revision
  checks, setup mode preservation and narrow Main-routing admission.
- Source regression guard: **2,285 checks**.

The first combined run exposed a test's assumption about localized menu
captions; the structural check now locates categories by their real commands.
A source-guard run timed out under concurrent compilation; its complete rerun
passed without weakening any checks. The four-pixel legacy raster witness is
unchanged and remains open.

Portable/host checks do not establish every real account, Windows 1809 or
lock-screen behavior. Final native appearance/reset receipts accompany the
beta delivery record.

Native follow-up passed on the current machine: shared Profiles/Main pages,
unfilled rows, black custom color corrected for the dark surface after F5,
and ordinary Registration/Main wallpaper generation. The trial was reset
afterward. [Beta 2 delivery and recovery evidence](desktopstub-beta2-2026-10-26.md)
records the precise scope and remaining native acceptance.
