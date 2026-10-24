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

## Calendar event-filled builders and queue, 24 October

The installed Calendar DLL above was read again, and its SHA-256 still matches `59A1E8FCC46108B37AA46FFE9DB2A56B0EB9C4390A191C58696AA52FDCEDB1A1`. This pass is limited to static builder/control-flow inspection. No native Calendar process, account, event store or notification database was accessed. The local witnesses under `build/review-validation/` are `calendar-HxOutlookBackground-nobytes-oct24.txt`, `calendar-event-builders-annotated-oct24.txt`, `calendar-event-record-helpers-oct24.txt` and `calendar-event-style-literals-oct24.json`. All VAs below assume image base `0x180000000`.

The native helpers matter for interpreting the builder calls correctly:

| VA | Observed operation |
| --- | --- |
| `0x1800733C0` | Create a text node with the supplied text, with no explicit style. |
| `0x1800730FC` | Create text, then set `hint-style="caption"`. |
| `0x180073258` | Create text, then set `hint-style="captionsubtle"`. |
| `0x1800734E8` | Call `0x180073594` with literal `2`; that helper sets `hint-wrap="true"` and `hint-maxLines="2"`. This is not an append/finalize operation. |

The text-style helpers do not add wrapping themselves. Where a builder calls `0x1800734E8` separately, the wrapping is part of that specific text-node contract.

`0x18008BC98` sets the event-present byte at record offset `+0x1F0` from whether the primary event pointer is non-null. In the same preparation path, the primary subject obtained through event virtual slot `+0x38` is processed by `0x1801AC4E8`, saved at `+0x278` and copied to `+0xC0` (`0x18008BD0C`–`0x18008BD9F`). The optional string at `+0xE0` comes through event virtual slot `+0x40`, conditional on another event flag and a nonblank-string check (`0x18008BDC0`–`0x18008BE2A`); its placement and the supplied screenshots are consistent with location. These are semantic inferences from the call sites, not names obtained from private symbols.

The record deliberately has distinct formatted-when fields. `+0xA0`, `+0x100`, `+0x1F8` and `+0x218` are filled using the shared event-range formatter `0x1801ABE40`, with different Boolean arguments and a date-comparison branch for `+0x1F8`. Substituting one universal formatted-when string for all four fields is not the native contract. The same-day branch at `0x1801AB5D0` proves one difference: when its first Boolean is true, the check at `0x1801AB61F` goes straight to returning the formatted start text at `0x1801AB79B`; with that flag false and unequal start/end values, it formats the end and combines the two through a resource at `0x1801AB636`–`0x1801AB6B5`. The constructor sets this Boolean true for `+0xA0` (Medium) and false for `+0x100` (Large), establishing start-only versus a range in this case. Agenda `+0x218` also sets the first Boolean true, but sets a second Boolean true as well. The Wide `+0x1F8` path either copies `+0xA0` or calls the formatter with first Boolean false and second Boolean true after a date comparison. This bounded pass does not reconstruct every localized range-format resource or all-day/multiday case.

The Medium function starts at `0x180090560` (correcting the earlier `0x180090548` function-start estimate). With the event byte set, it emits `TileMedium`, branding `name`, and binding `displayName` from record `+0x80`. Its subject at `+0xC0` uses `caption`, followed by `captionsubtle` when text at `+0xA0`. Both text nodes explicitly wrap and have a maximum of two lines: calls at `0x1800907F9` and `0x18009083F`. Without an event, it calls the date-only helper `0x180092CEC` with size enum 1 at `0x1800908D0`.

The Wide next-event function is `0x18009093C`. Its event path emits `TileWide` with branding `nameAndLogo`, then a group with two subgroups:

```xml
<group>
  <subgroup hint-weight="7">
    <text hint-wrap="true" hint-maxLines="2">SUBJECT</text>
    <!-- Only when the location-like string is nonempty: -->
    <text hint-style="captionsubtle">LOCATION</text>
    <text hint-style="captionsubtle">WIDE_WHEN</text>
  </subgroup>
  <subgroup hint-weight="3">
    <text hint-align="right">SHORT_WEEKDAY</text>
    <text hint-style="headernumeral" hint-align="right">DAY</text>
  </subgroup>
</group>
```

The explanatory placeholders and comments above are not literal payload text. The primary node has no explicit style; it is not `title` or `subheader`. The left/right weight setters are `0x180090CB0` and `0x1800910CC`. The title wrap and two-line cap are set at `0x180090DC1` and `0x180090EB1`. The optional location and when fields come from record `+0xE0` and `+0x1F8`; the right weekday/day come from `+0x60` and `+0x20`. The numeric literals were checked as complete UTF-16 strings, not inferred from constructor character counts: RVA `0x1EF20C` is `37-00-00-00` (`7`), and RVA `0x1EE8D8` is `33-00-00-00` (`3`).

The separate Wide agenda function starts at `0x180091630`. It emits branding `nameAndLogo`. Its first group/subgroup contains a plain, unstyled header from record `+0x2D8`. The next group has **two** subgroups, one for times and one for subjects, rather than independently formatted full event rows. Its loop at `0x180091B55` creates up to three paired rows from `+0x218 + i*0x20` and `+0x278 + i*0x20`, using count `+0x318`. The row text starts without explicit style. For rows whose index exceeds the prepared cutoff at `+0x31C`, both nodes are assigned `captionsubtle` (`0x180091BCE`, `0x180091C30`, `0x180091D25`).

The agenda time subgroup has weight `2` normally, or `3` if any prepared time string is longer than eight UTF-16 code units; the subject subgroup has weight `5` in both cases. The weight setters are `0x180092040`, `0x180091E63` and `0x180091F68`. The compact branch rejoins at `0x180091F14` after temporary-string cleanup, so it still reaches the subject-weight setter. Literal RVAs `0x1EE8D4`, `0x1EE8D8` and `0x1EF008` decode to `2`, `3` and `5`, each followed by a NUL. When the prepared row count is exactly one, the builder adds a footer group with an empty text spacer and a `captionsubtle` line from `+0x2F8` (`0x180092132`, `0x180092298`, `0x180092331`, `0x180092392`). The preparation routine obtains the header/footer through resource IDs; this pass has not independently decoded those localized resources, so the screenshot's English `Today`/`No other events today` strings should not be generalized to every record or language.

The Large event function is `0x1800925C0`. Its event path uses branding `nameAndLogo` and sets binding `displayName` from record `+0x80` at `0x1800927E1`. It does **not** add the date-only `headernumeral` block in this path. The text sequence is:

1. Primary subject `+0xC0`, explicitly `body`, wrapping with a two-line maximum (`0x1800928A8`, `0x180092945`).
2. Optional nonempty location-like text `+0xE0`, `captionsubtle`, then the distinct primary when field `+0x100`, `captionsubtle`.
3. Empty text spacer, second subject `+0x140` with `caption`, and second when `+0x160` with `captionsubtle`.
4. Empty text spacer, third subject `+0x1A0` with `caption`, and third when `+0x1C0` with `captionsubtle`.

The later subjects use the caption helper and do not automatically inherit the primary subject's wrapping. In the record preparation loop, two additional event pointers can populate those later fields (`0x18008C09A`–`0x18008C2EF`), which is consistent with three events total. The screenshot's footer date is consistent with native branding receiving the date text through `displayName`, rather than a big date header being placed above the events.

The queue assembly also explains the two Wide appearances. The loop near `0x18008F650` creates one document containing Small, Medium, Wide next-event and Large by calling `0x18008FEC8`, `0x180090560`, `0x18009093C` and `0x1800925C0` at `0x18008F7A8`–`0x18008F80E`. After finalization/publication, it checks the same event byte and calls `0x18008F9D4` at `0x18008F889` when an event is present. That helper creates a **second document**, adds Small, then calls the Wide agenda builder at `0x18008FA2E`, and finalizes/publishes it separately. Thus the native next-event and agenda states are separate queued payload variants; this is not evidence of one combined Wide layout or an exact shell rotation interval.

These builder facts establish the size-specific event presentation and queue shape for this installed version. They do not establish every appointment-selection window, localized event-range string, recurrence rule, shell queue timing, raster font metric or pixel position. Those remaining limits should be stated explicitly instead of describing the entire application as a verified 1:1 clone.

### Agenda inclusion and subdued-row cutoff follow-up

A bounded re-read of the existing disassembly resolves more of the preparation predicate, but does not establish the event getter's UTC/local domain. At `0x18008B981`, the reference timestamp saved at `[rsp+0x48]` is produced by `0x1800BAE80` from the incoming record's first timestamp. For ordinary nonsentinel values that helper calls `0x1800BACA0`, which calls `FileTimeToSystemTime` through IAT `0x1801D4828`, then `SystemTimeToTzSpecificLocalTime` with a null timezone argument through IAT `0x1801D4810`, and reassembles the resulting civil fields. The timestamp retained in RBX is therefore a local civil representation; treating this reference as an unchanged raw UTC instant is unsupported. Import names were verified read-only from this same DLL's timezone import table starting at `0x1801D4800`.

Helper `0x1800A4C78` extracts year/month/day through `0x1800258C0` and reconstructs a midnight representation through `0x180002D30`; it performs no timezone conversion itself. Helper `0x1800A4B40` compares year/month/day, using field selectors 0, 2 and 3. The flag set at `0x18008BFB9` means that the primary event's start civil date equals the civil date of the reference timestamp plus 24 hours. This is relative to the prepared reference, and should not automatically be described as "tomorrow from now" for every scheduled record.

For each of the two supplied additional event pointers, the agenda-count increment at `0x18008C117` is reached when that flag is true, **or** the additional start's civil date equals the reference date, **or** its numeric start value is less than the reference timestamp (`0x18008C0EA`–`0x18008C111`). Thus "always include only events on the primary event's date" is not the native predicate. A maximum of three display rows is a capacity, not a promise that three rows are always populated: the upstream record must supply those pointers and the preparation predicate must admit them.

The initialization at `0x18008BD01` writes a full QWORD value 1 to record `+0x318`. That simultaneously initializes the low DWORD agenda count to 1 and the high DWORD cutoff at `+0x31C` to 0. At `0x18008C2A2`, the additional event's first QWORD returned through virtual slot `+0x90` is compared directly against the primary event's first QWORD from the **same** virtual slot, saved at `[rsp+0x68]`. If the additional value is less than or equal to the primary value, the cutoff increments at `0x18008C2A9`. This comparison has no intervening timezone conversion, date truncation, or all-day-flag check; it also occurs after the agenda-inclusion branch rejoins. The renderer uses a zero-based row index and applies `captionsubtle` only when that index is **greater** than the cutoff (`0x180091BCE`–`0x180091BD5`). There is no demonstrated off-by-one in that initialization/comparison pair.

The bounded witness does not identify the implementation of the event object's virtual slot `+0x90`. Consequently it does not prove that comparing raw Google UTC starts reproduces native behavior for date-only/all-day events originating in different calendar timezones. Two items sharing a displayed all-day date need not have equal raw UTC starts; whether the native getter has already normalized those values remains unverified. The user's reported dimming and row-count cases should retain that distinction until native comparison screenshots or narrower getter evidence resolve it.
