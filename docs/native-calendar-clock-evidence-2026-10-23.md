# Installed Calendar and Clock tile code evidence

Read-only inspection of installed binaries goes beyond the earlier manifest inspection. No app, calendar/account store, package permission or operating-system notification database was modified. Offsets below are specific to these exact versions; do not reuse them for another build.

Calendar package microsoft.windowscommunicationsapps 16005.14326.22342.0: HxOutlookBackground.dll SHA-256 59A1E8FCC46108B37AA46FFE9DB2A56B0EB9C4390A191C58696AA52FDCEDB1A1. The Small builder at VA 0x18008FEC8 references TileSmall at 0x18008FF47 and selects no branding, centered stacking and centered text. It creates two text nodes, with titlenumeral for the day. TileSmall and style literals occur around raw offsets 0x1EE2E0–0x1EE3C8.

The date-only Medium builder at 0x180090548 selects TileMedium and calls helper 0x180092CEC with size enum 1. That helper compares the enum at 0x180092D4F and selects no branding, centered alignment and centered stacking for Medium. Weekday uses caption around 0x180092FE3; day uses headernumeral around 0x180093190. Wide/Large select right alignment/nameAndLogo rather than the centered Medium rules. The local disassembly witness is build/review-validation/calendar-HxOutlookBackground-disasm-oct23.txt.

Clock package Microsoft.WindowsAlarms 11.2607.6.0: Time.dll SHA-256 CE10935AF06A6358FBD51D96F92353FDB4DF00DC99ABD11DF36B62C303265C3F. Actual UTF-16 XML fragments at raw 0x6BBA64–0x6BBD4C contain TileMedium branding=name/displayName and base text, TileWide branding=nameAndLogo and title text, and TileLarge branding=nameAndLogo and subheader text. Adjacent markers include ClockLiveTileTimerTask - GetDescriptor and ClockTileUpdated. Nearby pinning literals name WorldClockSmallTile.png and the Medium/Wide/Large assets. This clock path does not contain a live Small text binding.

The user's screenshots independently show abbreviated weekday/date on Calendar Small, centered date-only Calendar Medium, and weekday rather than a long full-date block on the native world-clock tile. At the initial inspection, the full assembled Clock payload and event-filled Calendar branches had not been traced. The additional Clock inspection below addresses the former. These findings do not establish pixel-for-pixel identity for every shell, DPI, locale, agenda state or custom typography setting.

## Additional Clock control-flow inspection, 24 October

The same installed `Time.dll` was disassembled read-only with the installed Visual Studio `dumpbin /disasm:nobytes`. No app process was launched, instrumented or injected; no personal Clock data or notification database was read. The local witnesses are `build/review-validation/clock-native-audit-oct23/Time-disasm.txt`, `ClockLiveTile-builder.txt`, `ClockLiveTile-callers.txt` and `ClockLiveTile-weekday-formatter.txt`. Image addresses in this section assume the preferred image base `0x180000000` and are specific to the SHA-256 above.

The XML builder at VA `0x180C6B240` assembles nineteen pieces (allocation count `0x13` at `0x180C6B316`) rather than selecting only an isolated template literal. The concatenation stores at `0x180C6B328` through `0x180C6B450` establish this complete structure, with the dynamic strings named descriptively below:

```xml
<tile version="3"><visual>
  <binding template="TileMedium" branding="name" displayName="LOCATION">
    <text hint-style="base">DIRECTION_MARK_AND_TIME</text>
    <text hint-style="captionsubtle">WEEKDAY</text>
  </binding>
  <binding template="TileWide" branding="nameAndLogo" displayName="LOCATION">
    <text hint-style="title">DIRECTION_MARK_AND_TIME</text>
    <text hint-style="captionsubtle">WEEKDAY</text>
  </binding>
  <binding template="TileLarge" branding="nameAndLogo" displayName="LOCATION">
    <text hint-style="subheader">DIRECTION_MARK_AND_TIME</text>
    <text hint-style="captionsubtle">WEEKDAY</text>
  </binding>
</visual></tile>
```

Whitespace and the descriptive uppercase placeholders above are explanatory, not copied literal payload. The builder contains no third text node for a full date or location: location is supplied through `displayName`. All three bindings reuse the same time and weekday values.

The weekday is supported by the call chain, not inferred solely from the screenshot. Constructor `0x180ABFBD0` creates a DateTimeFormatter from the object at RVA `0x699BC8`, whose text is `dayofweek.full`, then saves it at instance offset `+0x10` (`0x180ABFC1E`–`0x180ABFC34`). In the update routine, both the initial builder call at `0x180C6AD6D` and the later call at `0x180C6AF07` receive that formatter's output as the second content argument. The time formatter factory at `0x180C6B4A0` creates a DateTimeFormatter using `shorttime` (string object RVA `0x69A678`); the builder prepends U+200E or U+200F according to the resource `LayoutDirection`/`LTR` check at `0x180C6B299`–`0x180C6B2CF`. This delegates the native time representation to Windows formatting preferences rather than hard-coding 12-hour time globally.

The visible label comes through `0x180AC82B0`, called by the builder at `0x180C6B30E`. That helper reads the source clock's label at `+0x28`, checks for `LocalTime`, optionally appends `_SHORT`, and passes the resulting resource key through its lookup call. This is the label injected into `displayName`; it is not another text node.

The Small pin path is also directly reachable: URI creation at `0x180C69418` uses `ms-appx:///Assets/WorldClockSmallTile.png`; the value is passed into the secondary-tile visual properties at `0x180C69557`–`0x180C6955D` before the request ending at `0x180C695C9`. Inspection of that installed 497-byte PNG confirms a white outlined clock over a globe. It is a distinct world-clock symbol, rather than the ordinary circular clock used by the main app. The live builder above intentionally has no `TileSmall` binding.

This extends the earlier literal evidence to the fully assembled Clock XML contract. It still does not constitute capture of a live OS notification or pixel equivalence across shell/DPI/locale combinations. The Calendar event-filled branch remains outside this bounded inspection.

DesktopStub derives the default direction marker from the Windows UI language (`GetUserDefaultUILanguage` → `LCIDToLocaleName` → `GetLocaleInfoEx(LOCALE_IREADINGLAYOUT)`), cached with its minute header. Time and weekday formatting continue to follow regional preferences. Native Clock can use a different language through its own application-language/resource override; DesktopStub cannot reproduce that app-specific override without a separate explicit setting. It does not inspect or alter the native app's private language configuration.
