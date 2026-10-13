# Native INI preservation and offscreen configuration coverage

Source changes prepared on 2026-10-13 and exercised through the coordinated
scan-gated inert validation. Actual shell/Startup acceptance is separate.

## Native assignment rules

The existing INI dialect stores one assignment on each physical line. It does
not decode literal `\n` into a newline. Programmatic CR, LF, or embedded NUL in
a stored value cannot round-trip and now fails with `ERROR_INVALID_PARAMETER`
before the text, including a default header, is changed. Runtime provider text
can still contain newlines; this correction concerns persistent INI assignments.

Keys cannot contain `=` because both name parsers split the assignment at the
first equals sign, even within a quoted key. Section and key names must
round-trip through their actual serialized header/assignment and legacy name
parser. Empty names and null-name pointer conventions are preserved. Supported
punctuation, Unicode, significant spaces inside quoted keys, backslashes and
quotes are tested rather than removed by a generic restriction list. Existing
section-header formatting is unchanged; structurally unreadable headers fail.

Names use a quote-only escape convention, while values escape both quotes and
backslashes. The native assignment formatter now preserves that distinction,
matching the managed whole-document formatter. Managed section-only writes had
the same doubled-key-backslash defect and now use the existing name formatter.
Neither parser changes how old name text is read. Old literal doubled
backslashes remain doubled names; no automatic rename/migration is performed.

The native pointer API takes null-terminated section/key names, so its usual C
string boundary applies. The length-aware validation helper rejects embedded
NUL names when given their complete strings. No claim is made that a pointer
API can inspect characters beyond its terminating NUL.

Regressions cover actual full-parser name/value round-trips, update without
duplicate escaped aliases, unchanged empty/existing text on invalid input,
exact original file bytes after failed `IniConfigStore::WriteRaw`, no profile
creation when a later default is invalid, and no publication of an earlier
staged assignment when a fresh transaction later fails. Native/managed shared
dialect data includes backslash/quote/trailing-backslash names. Managed tests
cover mixed section/document writes using those same name semantics.

## Caller behavior

All direct production `WriteIniValueToText` call sites inspected propagate its
boolean result. `IniWriteRaw`, `IniEnsureDefaultsRaw`, and `IniConfigStore`
preserve publication boundaries. DesktopStub's locked mutation wrapper returns
before the file/cache update when its callback fails. The old TileText tray
handlers log edited/saved status only after `IniWrite` succeeds. Discord profile
staging returns an error rather than publishing an unsuccessful candidate.

The content engine accepts multiline runtime/custom text in memory, while its
actual text input dialog is a single-line edit control. Persisting an actual
newline has never been supported by this dialect. The content candidate-save
path should explain a failed staged assignment; that UI wording is coordinated
separately from this shared-writer change.

## Continuous tests

`tools/TestConfigurationFixtures.ps1` builds the managed transaction, actual
offscreen editor, whole-document INI and fake packaged Startup fixtures using
the added `/build-only` switches. All builds finish before execution is allowed;
each selected executable receives a standard Defender custom scan before its
bounded 60-second process run. The managed fixtures retain internal 45-second
watchdogs. Fake Startup operations never query or mutate a real StartupTask.

The existing `.cmd` defaults remain compile-and-run for compatibility. The new
PowerShell entry point defaults to compilation only; `-Run` explicitly opts into
the inert checks. Windows CI invokes that scan-gated path. No visible window,
notification icon, real Startup entry, package, service, utility job, account or
device operation is part of these fixtures.

## Validation results

Coordinated root validation reports 588 passing native runtime checks, 277
shared source checks, 33 actual offscreen managed UI checks, 29 managed
transaction checks, 76 whole-document INI checks, 13 fake packaged Startup
checks, and 21 native/managed lock-interoperability checks. These fixtures used
isolated data and did not launch products or real Startup integrations.

The managed document executable is freshly compiled even in `/build-only` mode:
the configuration batch unconditionally calls `BuildManagedIniDocumentTests.cmd`
before its build-only exit. That recipe compiles the current test, named-object
and INI sources into the exact `build/review-validation` path scanned/run by the
PowerShell wrapper. Compiler failures abort before execution; a stale previous
executable is never accepted as a successful build.

The root coordinates normal product builds and scans separately. Those builds,
broader utility fixture runs, and actual product/shell acceptance are not implied
by the narrow results above. No pixel tolerance or renderer behavior was changed
as part of this work.
