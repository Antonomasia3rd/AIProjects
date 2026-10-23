# Clock and Calendar tiles, 23 October 2026

Clock and Calendar are independent tile types. They start no network, account
reader, Microsoft Calendar store, hardware provider or notification worker.
Create several Clock instances with different Windows timezone IDs for world
clocks. As of the October 24 parity correction, an absent/new Clock time format
defaults to `System`, matching the installed Clock app's short-time formatter.
Explicit `12Hour` (AM/PM), `24Hour` and `System` settings remain effective.
Weekday text follows Windows regional settings. `Clock.ShowDate` controls the
weekday line; the native layout does not contain a separate long-date block.

Example instance configuration (add these IDs to `[Tiles] Instances`):

```ini
[Tile.clock-surabaya]
Type=Clock
Name=Surabaya
Clock.TimeZone=SE Asia Standard Time
Clock.Location=Surabaya
Clock.TimeFormat=12Hour
Clock.ShowDate=1

[Tile.clock-london]
Type=Clock
Name=London
Clock.TimeZone=GMT Standard Time
Clock.Location=London

[Tile.calendar]
Type=Calendar
Name=Calendar
Calendar.TimeZone=Local
Calendar.FilePath=
Calendar.DaysAhead=7
Calendar.MaxEvents=3
Calendar.RefreshSeconds=60
Calendar.ShowLocation=1
Calendar.TimeFormat=12Hour
```

Empty `Calendar.FilePath` is a date-only tile. The optional source is an
explicit absolute local-drive `.ics` file; network paths and URL subscriptions
are rejected. It reads at most 2 MiB, accepts UTF-8/BOM or UTF-16LE/BOM, and never
executes embedded VALARM instructions or opens event URLs. File metadata is
checked at the configured interval; unchanged files are not reread/reparsed.
Mapped drives, directory/file reparse points, offline files and cloud recall
placeholders are refused before reading. Save an ordinary local copy instead.
Date-only mode creates no worker. A configured calendar uses one demand-owned
asynchronous worker and the existing content-poll wake callback; filesystem
reads, parsing and recurrence expansion never run on the shared content poll.
Pending work keeps the date header available, and old source revisions cannot
replace a changed tile's events. Unpinning/type changes cancel the worker.

The parser supports one-off timed events, floating local dates/times, UTC
timestamps, all-day events with exclusive end dates, and DAILY/WEEKLY recurrence
with INTERVAL, COUNT or UNTIL, plain BYDAY weekdays, Monday week start and EXDATE.
Local occurrences are converted separately so their wall times survive daylight
saving changes. A nonexistent or repeated local time is skipped with a warning;
it is never guessed as a different hour. Nonexistent occurrences do not consume
COUNT. Zoned DTSTART requires a UTC UNTIL, as specified by iCalendar.

Monthly/yearly rules, ordinal weekdays, RDATE, DURATION and RECURRENCE-ID overrides
are not implemented. Unsupported events produce actionable warnings; an
unsupported series override suppresses that whole UID so a stale master does
not display the wrong appointment. Exporting expanded occurrences is the
fallback. Custom VTIMEZONE rules are not imported: supported timezone names use
Windows' installed rules. Native Windows timezone IDs work, along with a small
explicit IANA alias list including Asia/Jakarta, Asia/Bangkok, Asia/Tokyo,
Europe/London, America/New_York and America/Los_Angeles. Unknown zones are
rejected visibly. This is an offline calendar preview, not a complete iCalendar
client or a Microsoft account replacement.

The reader bounds supported events at 5,000 and expanded occurrences at 2,000,
event spans at 366 days, and non-UTC resolution work at 50,000 conversions per
expansion. Arithmetic recurrence seeking avoids replaying years for unbounded
rules. COUNT with local time replays the bounded occurrence set to count only
valid civil dates. Expanded occurrences cache for the day or until source/
settings changes; the visible list filters already ended events on each read.
Clock/date formatting is cached per minute, and the provider reports the exact
next minute boundary without a per-second worker.

The initial read-only reference inspection found the installed Windows Clock package
`Microsoft.WindowsAlarms` 11.2607.6.0 and Mail/Calendar package
`microsoft.windowscommunicationsapps` 16005.14326.22342.0. Their manifests declare
small/medium/wide/large tile assets; Clock declares
`TimeBackground.ClockLiveTileTimerTask`. No package was started, patched,
injected, disassembled or copied into the product. These manifests do not expose
the generated live XML. Typography uses documented adaptive roles (`title` =
24 effective pixels; `caption` = 12), and the user's screenshots guide layout.
No claim of binary-exact reproduction follows from that manifest inspection.
The later [binary/code evidence](native-calendar-clock-evidence-2026-10-23.md)
traces the actual Calendar Small/Medium builders and the complete Clock XML
assembler and formatter callers. The [October 24 correction](calendar-clock-parity-2026-10-24.md)
documents the implemented size-specific contracts, explicit settings, tests and
remaining native visual acceptance. It supersedes the initial single-layout
approximation above.

Focused fixture: `tools/BuildClockCalendarTests.cmd`, then scan and run
`build/content-integration/ClockCalendarTests.exe` through ReviewValidation.
64,236 checks passed with `/W4 /WX`; Defender scan clean. Checks include Gregorian
century boundaries, AM/PM, minute deadlines, recurrence arithmetic against a
day-by-day reference, folded/escaped text, ignored alarms, supported-sibling
preservation, unsupported series overrides, and read-only Windows DST conversion
for summer/winter, spring gaps and autumn repetition. No real calendar/account
file, pin, package, notification or resident application was changed.
The asynchronous checks use blocked synthetic readers to verify prompt header
return, replacement cancellation, immutable cache reuse across 100 polls,
expired-event filtering without file work, manual refresh and date-only
inactivity. Attribute/path refusal tests cover offline, reparse, recall and UNC
inputs without mounting a network drive or hydrating a cloud file.

Primary references:

- [Microsoft adaptive tile typography](https://learn.microsoft.com/en-us/windows/uwp/launch-resume/create-adaptive-tiles)
- [Microsoft time APIs](https://learn.microsoft.com/en-us/windows/win32/sysinfo/time-functions)
- [Microsoft local-to-UTC DST conversion](https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-tzspecificlocaltimetosystemtimeex)
- [IETF iCalendar RFC 5545](https://www.rfc-editor.org/rfc/rfc5545)
