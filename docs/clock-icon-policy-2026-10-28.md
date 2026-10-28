# Clock artwork audit and localized-label correction

The earlier "Clock/world clock static icon looks weird" report has two distinct
parts: icon identity and visual fidelity. The committed implementation already
has independent `clock` and `worldclock` topics. `Clock.IconStyle` exposes Auto,
Clock and WorldClock through the validated source configuration and shared
tray/Properties menu; explicit choices remain authoritative.

Local Clock uses the documented E917 Clock symbol through the selected installed
Segoe MDL2/Fluent font, with family/glyph checks and a vector fallback. Auto
prefers MDL2 below Windows build 22000 and Fluent on newer systems. Microsoft
documents the [Fluent Clock symbol and font policy](https://learn.microsoft.com/en-us/windows/apps/design/iconography/segoe-fluent-icons-font).

The retained installed Clock evidence traces the secondary tile's
`WorldClockSmallTile.png`, a white clock-over-globe image. DesktopStub deliberately
uses its own vector interpretation of that composite rather than guessing a PUA
codepoint or bundling Microsoft's bitmap. The generic E909 World symbol is a
globe, not the same composite. See [the installed binary evidence](native-calendar-clock-evidence-2026-10-23.md).
This establishes the intended subject; it is not a pixel-equivalence claim.

The audit found a concrete automatic-selection defect: a Local time zone used
the ordinary Clock icon only when Location was blank or exactly the English
string "Local time". Changing the display label to "Home", "Waktu lokal", or
even "local time" selected the world-clock image without changing the source
mode. Auto now depends only on the Local versus explicit time-zone setting.
Location remains display text. Users who want a world-clock image for a city in
their local zone can explicitly select `Clock.IconStyle=WorldClock`.

No INI migration or settings rewrite is needed. Topic-specific asset paths and
publication metadata already distinguish Clock from WorldClock, so the ordinary
metadata refresh selects the corresponding asset plan. The change does not alter
the installed-font selector or the vector geometry.

Portable artwork-policy checks passed **780 checks** after a clean Defender
scan. They and the existing actual immutable Clock/host fixture now cover
translated/custom labels, explicit named zones and both icon overrides. The
parent records the combined host execution. No native app, package,
account, icon-font installation or Start operation was exercised in this audit.
Actual Start appearance at different DPI/OS/font versions remains acceptance;
the subjective visual report should not be marked pixel-perfect from this fix.
