# Separate Registration delivery repair, 2026-10-23

## Confirmed cause

The resident's log recorded successful image/manifest writes followed by
deployment errors `0x80073CF0` and `0x80070003` for a Registration tile under the
logical `%LOCALAPPDATA%\DesktopStub\RegistrationTiles` directory. That logical
directory was absent when inspected from an unpackaged PowerShell process.
Read-only inspection found the manifest, launcher, images and authorization
records under the main package's physical
`LocalCache\Local\DesktopStub\RegistrationTiles` directory instead.

This matches the documented [AppData virtualization of packaged desktop apps](https://learn.microsoft.com/en-us/windows/uwp/porting/desktop-to-uwp-behind-the-scenes).
The application's own successful read/write did not establish that the Windows
deployment service could open the same logical path. The per-tile delivery
choice could already be saved as Registration while its first publication
continued to fail.

## Change

The separate Registration backend now opens its owned manifest and containing
directory, rejects directories/reparse files where ordinary files are expected,
and obtains their final physical paths using
[`GetFinalPathNameByHandleW`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew).
It checks that the resolved manifest remains `AppxManifest.xml` inside the
resolved owned package directory, then supplies that physical manifest path to
the existing deployment backend. The manifest handle stays open without write
or delete sharing until deployment completes.

The user/profile/tile identity, logical cache location, ownership authorization
and publication receipt remain unchanged. Existing virtualized staging files
can therefore be used by the corrected backend. This does not disable file
virtualization or replace the main host's package identity.

Ordinary tile migration now requests the shared actionable confirmation. A
pending request changes no delivery setting or package file. Its approval
rechecks the active profile and the selected tile's complete section/policy,
and a second check under the INI mutation lock prevents an intervening edit
from being overwritten. No/cancel, expired requests and stale approvals cannot
commit a switch. After a successful save, the tile explicitly requests a first
refresh. Setup status distinguishes authorization from a successful native
publication and explains when the separate app can be pinned from Start.

## Validation boundary

The inert Registration fixture resolves real private file handles, injects a
logical-to-physical shadow-folder mapping, verifies that deployment receives
the physical manifest path, and refuses a manifest whose resolved parent does
not match its owned root. Deployment is injected; no real package is installed.

The focused delivery fixture drives the actual registered tray command and
shared confirmation coordinator. It checks pending/No/stale-Yes behavior,
successful per-tile mode persistence, explicit refresh demand and the first
queued Registration frame. The fake deployment and notification hooks keep
these checks offline and preserve the existing resident and real pins.

These checks establish host behavior and path selection. They do not replace
native Windows package-deployment acceptance after the corrected build is
installed. Results for other native tile identities and their host routing are
tracked separately.
