# Calendar and Mail provider parity feasibility

The current implemented sources are **local ICS and Google Calendar**, plus
**Gmail/Google Workspace** for Mail. Visual similarity to UWP Mail & Calendar
does not establish equivalent provider/account support. This document identifies
supported routes for future work; no account was accessed and no provider was
implemented here.

| Provider | Calendar route | Mail route | Main constraint |
| --- | --- | --- | --- |
| Google | Existing Calendar API and explicit Google OAuth grant | Existing Gmail API and separate metadata/read-only purpose | Keep Calendar and Mail consent/scopes distinct; retain per-account/all-readable-calendar selection and bounded private caches. |
| Microsoft personal / Microsoft365 | Microsoft Graph delegated Calendar APIs | Graph delegated Mail APIs | A registered public desktop client and interactive user consent are needed; tenant policy can restrict consent. Do not promise Graph support for every on-premises Exchange server. |
| Yahoo | Documented Yahoo CalDAV/ICS service | IMAP over TLS; approved Yahoo OAuth is a separate path | Yahoo documents app-generated passwords for CalDAV/IMAP. Mail/calendar OAuth scopes are restricted and require developer approval; ordinary OpenID email/profile sign-in is insufficient. |
| Generic server | CalDAV, or an explicitly configured ICS subscription | IMAP with server-supported modern authentication | IMAP carries mail, not calendar synchronization. Calendar discovery/auth and mail discovery/auth remain separate capabilities even if the account label is shared. |

## Microsoft route

Graph's [calendarView](https://learn.microsoft.com/en-us/graph/api/calendar-list-calendarview?view=graph-rest-1.0)
returns occurrences, exceptions and single events over a bounded interval for
personal and work/school accounts. `Calendars.ReadBasic` is its documented least
privilege; choose additional read-only permission only for fields/features that
need it. Fetch calendar lists, selected views, pagination and explicit time-zone
offsets; do not treat the default calendar as all calendars.

[Message listing](https://learn.microsoft.com/en-us/graph/api/user-list-messages?view=graph-rest-1.0)
supports delegated `Mail.ReadBasic` for headers/basic metadata and broader
`Mail.Read` when previews/body access is actually requested. Filter the intended
folder, select only needed fields and bound pagination. No sending/deletion or
write permission is needed for a read-only tile. Shared/delegated mailbox and
calendar access needs its own capability/permission acceptance.

Use a purpose-bound desktop sign-in design with the provider's documented
[authorization-code flow](https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-auth-code-flow),
PKCE/public-client handling and protected per-profile token storage. An existing
Google client or Microsoft Mail's first-party application identity cannot serve
as DesktopStub's Microsoft registration.

## Yahoo constraints

Yahoo's [Calendar support](https://en-global.help.yahoo.com/kb/account/apple-calendar-sln3744.html)
documents `https://caldav.calendar.yahoo.com` with the Yahoo ID and an
app-generated password. Its [IMAP settings](https://help.yahoo.com/kb/sln4075.html)
document `imap.mail.yahoo.com`, TLS/993 and an app password. These are real
supported connection routes, not a promise that every account can currently
generate a password or that every provider feature is exposed.

Yahoo's [developer sign-in guide](https://developer.yahoo.com/sign-in-with-yahoo/)
marks mail, contacts and calendar scopes as restricted and requiring Developer
Access. Therefore a Yahoo button/OIDC email grant alone cannot authorize
calendar/mail access. Before promising Yahoo OAuth, establish approval, permitted
scopes, native-client/redirect handling and the actual mail protocol grant. Do
not borrow another application's client ID, cookies or saved token.

An app-password option is distinct from asking for the primary account password.
Use masked local entry and protected storage; explain provider-side revocation.
Yahoo documents [generation eligibility and deletion](https://help.yahoo.com/kb/yahoo-finance-plus/learn-generate-third-party-passwords-sln15241.html).

## Generic protocol route and incremental plan

[CalDAV, RFC4791](https://www.rfc-editor.org/rfc/rfc4791) defines calendar
collections and calendar data queries. Implement principal/calendar-home
discovery, collection selection, bounded time-range REPORTs, ETags and supported
incremental synchronization, then normalize recurrence, exceptions, floating
times, TZIDs and all-day dates into the existing event model. ICS file import
and a remote subscription are separate source modes; a private subscription URL
also needs secret treatment.

[IMAP, RFC9051](https://www.rfc-editor.org/rfc/rfc9051) defines mail access;
[OAuth SASL mechanisms, RFC7628](https://www.rfc-editor.org/rfc/rfc7628) do not
give an arbitrary server or client automatic OAuth access. Negotiate advertised
authentication, use verified TLS, and support documented app-password/server
credentials only where appropriate. Prefer EXAMINE and PEEK/header retrieval
for read-only tile previews so reading a tile does not silently mark messages
as seen. SMTP is unnecessary for this scope.

Recommended increments:

1. Define shared account/provider capabilities and purpose-bound credentials;
   keep Calendar and Mail presentation, permissions and selection independent.
2. Add Microsoft Graph read-only Calendar/Mail, with personal and work/school
   fixtures before explicit real-account acceptance.
3. Add a reusable CalDAV reader and validate against a controlled server, then
   Yahoo's documented connection route. Treat Yahoo OAuth approval separately.
4. Add bounded IMAP metadata/unread support, server capability/auth discovery and
   provider-specific OAuth only where supported. Retain the existing Gmail API.
5. Expose all connection/selection/refresh/privacy/error controls through the
   same validated CLI/INI/GUI surfaces. Reuse last-good/age/retry policy, stop
   unused workers, clear account caches on disconnection, and never auto-load
   message HTML or remote images for a tile preview.

Unit fixtures should cover pagination/limits, recurrence/time zones, duplicate
UIDs across calendars/accounts, token cancellation/expiry/revocation, auth
purpose mismatch, stale data and read-only mail flags. Native consent, tenant
policy and real server behavior remain explicit acceptance work. This plan is
not an implementation or a complete 1:1 parity claim.
