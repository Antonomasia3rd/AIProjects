# Google Calendar account source

This source uses a Google account authorization flow and the Calendar REST API. A local ICS export remains a separate optional source; it is not the Google sign-in implementation. Account access is never inherited from Codex, another application, browser cookies or the retired Windows Calendar app.

## Google client setup

The app needs an OAuth client belonging to its publisher or the user. Google requires a Cloud project with the Calendar API enabled and an OAuth **Desktop app** client. There is no borrowed client ID or credential bundled in this implementation. The project owner configures the consent screen and, while the project is in testing, its allowed test accounts. Download that Desktop client's credentials JSON and explicitly import it in the Calendar tile's Google settings. The importer accepts the `installed` client shape, rejects web/service-account files, and reads only `client_id` and the optional `client_secret`. Endpoint URLs from the file are ignored.

Choose **Connect** to open Google in the system browser, approve read-only Calendar events and read-only calendar-list access, and return to DesktopStub. The calendar picker then selects a calendar for this tile. The requested scopes cannot create, edit or delete events. Google may require publisher verification before a public client can be widely distributed. These requirements come from Google's [desktop OAuth documentation](https://developers.google.com/identity/protocols/oauth2/native-app).

An External consent screen left in Testing issues refresh grants that expire
after seven days for these Calendar scopes. Reconnect if such a test grant
expires; that is a Google project policy rather than a tile refresh failure.
See Google's [refresh-token expiration rules](https://developers.google.com/identity/protocols/oauth2#expiration).

Settings use `Calendar.Source=Google` and `GoogleCalendar.ClientId`, `ClientSecret`, `CalendarId`, `RefreshSeconds` (300), `TimeoutSeconds` (15), and `MaximumBytes` (2097152). `RefreshTokenProtected` is written by Connect as a DPAPI-protected, Windows-user-bound value. Plaintext refresh-token settings are rejected. The protected envelope also binds the profile, tile ID and OAuth client. The client secret identifies a public Desktop client and is not an account password. No account password is collected.

## Runtime contract

Sign-in binds an exclusive listener to `127.0.0.1` on an OS-selected ephemeral port before opening the browser. Each attempt has independent random state and an S256 PKCE verifier. The HTTP callback requires the exact local Host, path and state; duplicate parameters and duplicate Host headers are rejected. The listener closes on cancellation or timeout. The host commits a returned protected credential only if its originating tile/profile/configuration is still current.

Refresh and Calendar requests use fixed Google HTTPS endpoints, no redirects, bounded responses and cancellation. Credential values are confined to POST bodies or the Authorization header, never a query URL or diagnostic. Per-tile memory-only access tokens expire before Google's returned lifetime. Disconnect removes the local saved grant; optional server revocation is a separate action because Google documents that revocation affects the Cloud project's other OAuth grants too.

The API queries a bounded window with `singleEvents=true`, `orderBy=startTime`, `timeMin` and `timeMax`. Google expands recurring events and exceptions. Results retain event IDs and explicit offsets; all-day end dates remain exclusive. Calendar-list metadata supplies the original all-day timezone, and the operating system's ICU mapping converts IANA identifiers to Windows identifiers. Unknown mappings fail explicitly. Calendar lists are bounded to eight pages of 100 and event windows to eight pages of 250, at most 32 days. A repeated page token fails instead of looping. Failed/incomplete fetches are not published as a successful partial snapshot. These contracts follow [Events.list](https://developers.google.com/workspace/calendar/api/v3/reference/events/list), [CalendarList.list](https://developers.google.com/workspace/calendar/api/v3/reference/calendarList/list), [Windows ICU](https://learn.microsoft.com/en-us/windows/win32/intl/international-components-for-unicode--icu-) and [ICU timezone mapping](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/ucal_8h.html).

## Validation boundary

The provider fixture injects all HTTP, browser authorization and credential storage. It checks endpoint/scopes, callback rejection, profile binding, import boundaries, request limits, pagination, timed/all-day events, cancellation, instance-token caching and OS timezone mapping. It does not open Google sign-in or access an account. Actual sign-in, calendar selection and account refresh remain native acceptance steps after the user supplies a Desktop client and completes Google consent.
