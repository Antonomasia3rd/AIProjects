# Calendar and Clock layout correction, October 24

The user's screenshots showed an actual implementation gap: Calendar Small
stayed an icon, and date-only Medium reused the Wide/Large date-column layout.
The earlier manifest inspection did not satisfy the requested reverse engineering.
This pass inspected the installed generation code and XML and implemented the
verified size-specific contracts. Full module hashes, addresses, reconstructed
Clock XML and local disassembly witnesses are in [the evidence record](native-calendar-clock-evidence-2026-10-23.md).

| Source and size | Native default now implemented |
| --- | --- |
| Calendar Small | Centered abbreviated weekday and title-sized date numeral, no branding |
| Calendar date-only Medium | Centered full weekday in caption style and larger header numeral, no branding |
| Calendar Wide/Large | Existing right-hand date area; local ICS agenda remains available |
| Clock Medium | Base-sized time, subdued full weekday, location as name branding |
| Clock Wide | Title-sized time, subdued full weekday, name-and-logo branding |
| Clock Large | Subheader-sized time, subdued full weekday, name-and-logo branding |
| Clock Small/fallback | Static clock-over-globe symbol, no invented live time binding |

The Calendar contracts were traced in HxOutlookBackground.dll from installed
Mail/Calendar 16005.14326.22342.0. Clock's complete three-binding assembler and
dayofweek.full / shorttime formatter callers were traced in Time.dll from
Clock 11.2607.6.0. No native application or account/calendar store was started,
injected into, or modified. No Microsoft bitmap or font outline is bundled:
the world-clock symbol is drawn by DesktopStub's own vector implementation.

## Existing settings and delivery methods

Absent Clock/Calendar branding now reads as Auto so native per-size defaults
can be applied without overriding explicit None, Logo, Name or NameAndLogo.
Existing explicit values and custom typography remain effective. Calendar Small
semantic text is sent only under Auto text delivery. Off remains hidden; Overlay
continues using bitmap delivery for the existing larger bindings.

The Calendar Small Registration image uses the real source-rendering path even
when its generic Assets.SmallTile wallpaper switch is off. Date-only Calendar
Registration suppresses the Medium name footer under Auto branding, while
explicit branding remains effective. Registration Clock uses the source location
as its manifest display name so the label is retained without a duplicate body
line; the existing 128-character manifest bound is preserved without splitting
a UTF-16 pair. Native Overlay image bindings retain equivalent branding metadata.

Clock's absent time-format setting now uses Windows regional short time;
explicit 12/24-hour choices remain unchanged. The full weekday is subdued and
location is not repeated in the body. UI-language direction chooses the leading
LRM/RLM mark; regional formatting remains independent. The header is still cached
per minute. Native Clock may have a separate app-language override, which
DesktopStub does not inspect. Locale-aware abbreviated weekday and layout/direction
versioning invalidate old date/time image caches. No additional resident or timer
was introduced. Status uses the corresponding visible fields rather than the
removed long-date/location/no-events body fields.

## Verification

The final combined source/confirmation/layout fixture passed **281 checks**,
including the real XML builder with only the existing three background assets,
one Calendar Small binding, Off/Overlay controls, every explicit branding choice,
custom styles/alignment/stacking, current Windows weekday formatting, escaping,
centered offscreen ink at 100%/200%, and real private Registration generation.
That last test stages an owned package with native deployment injected, verifies
Small date ink when Assets.SmallTile is disabled, and changes the day to prove
the PNG is regenerated rather than retaining the previous image.

The extended regression fixture passed **489 checks**, the Clock/Calendar model
and native formatting fixture passed **64,255**, artwork metadata passed **713**,
offscreen artwork passed **295,628**, and source guards passed **1,937**. Product
and fixture binaries were scanned before execution. Evidence is under
build/review-validation/datetime-parity-oct24-*.

The historical four-pixel legacy static-caption discrepancy remains open;
no assertion or tolerance was relaxed. This correction does not claim that the
bitmap fallback is pixel-identical to Windows' renderer on every DPI/theme, nor
that Calendar's event-filled native branch or every other UWP tile is completely
reverse-engineered. The default Small/date-only Medium Calendar contracts and
the Clock text/branding contract above are supported by actual installed code.

## Installed result

The corrected build is installed. The guarded binary-only update retained the
current profile/accounts/history and Start layout; it did not repeat the earlier
reset or request new pins. Seven pre-update files and their hashes are retained
under DesktopStub/build/SettingsResetBackup-DateTime-20261024-012922-8b15c1.
The existing main package activated canonical PID 36780 and passed 75 seconds
with the same packaged process and successful native publication. A four-frame
batch completed with all four frames successful, followed by normal successful
Clock/Prayer minute batches. No failed batch or deployment-path failure appeared
in that health window. Acceptance of the publication is verified; native visible
Start pixels have not been captured by the automated checks.

Installed SHA-256 values:

| Artifact | SHA-256 |
| --- | --- |
| DesktopStub.exe | B40FA2BE1C1D6BEFD8DD266870286726870BEEC3C7342CE03C143879A91C9C31 |
| DesktopStubLiveTileBroker.exe | 88CF7B2ED393ECEAF140BCF2A28101BA44AA0C61198E9D6755C9E0486FED1FA6 |
| DesktopStubTileLauncher.exe | A208114A89DD9FC7219DE0B23DE51AA325E95342222B8106C9B85768769AFF1F |

The first installer preflight could not import Appx into PowerShell 7 because of
an assembly-name collision. It failed before stopping the resident or changing
files. The read-only package query was moved to the Windows PowerShell Appx
cmdlet, after which the guarded retry passed. Product runtime uses its existing
native activation/update APIs; this workaround changes only the local operation
script. Final evidence: datetime-parity-oct24-install-retry.log, the approved
artifact hash receipt, source/layout test logs and the verified backup receipt.
