# Embedded Notes sign-in source checkpoint, 2026-10-14

Historical checkpoint: the broader managed-source lookup on 2026-10-15 found
the original embedded WebView2 login and its cookie clearing/UID behavior. The
native-only historical conclusion below was incomplete. Current UX, session
policy, discovery protocol and remaining acceptance checks are recorded in
[notes-signin-ux-2026-10-15.md](notes-signin-ux-2026-10-15.md).

`ga_notes_signin.inc` provides `OpenEmbeddedNotesSignIn(HWND,int resource)` and
`CloseEmbeddedNotesSignIns()` (UI-thread shutdown). The dedicated modeless window
owns its controller/environment; its private WM_SAVE_ACCOUNT requires no ga_app
message forwarding. Callback state is weak, closed windows cannot publish, and
saving rejects an active INI profile change. Root owns activation routing and the
shutdown call. The source popup now invokes embedding rather than ShellExecute.

The visible official HoYoLAB view uses the supported WebView2 APIs. Only an
explicit Use signed-in account click requests GetCookies for the nonempty fixed
https://www.hoyolab.com/ URI. Domain matching accepts only hoyolab.com or proper
subdomains, and only ltoken/ltuid or complete v2 pairs are selected. Duplicate
conflicting cookies and oversize collection/fields are rejected. No ExecuteScript,
DOM scraping, other browser profile, browser harness, clipboard or credential log
is used. Popup HTTPS navigation remains in the embedded view. User-specified game
UID, optional DPAPI protection and captured cookies are staged through the existing
Notes JSON import validator; the native shared save publishes the whole batch
atomically and then requests source refresh. DPAPI is unchecked/opt-in; no existing
credential is decrypted for the dialog. Per-account cadence is retained.

WebView2 user data is app/profile-scoped under LocalAppData/DesktopStub/NotesSignIn
using the shared normalized INI identity hash, not the installed browser profile.
The embedding itself uses WebView2 Runtime subprocesses; tile delivery remains the
single resident architecture. Missing/incompatible Runtime and initialization
failure have friendly guidance; no installer is invoked. Cancelling the window
publishes no pending credentials, while the embedded browser profile can preserve
its own signed-in session for this app/profile.

Recovered native history (8c7b3e6 shared donor and 89d26e5^ native source) contains
manual/JSON account editing and external Open HoYoLAB, not an embedded WebView2
login. The -SWebView2 history hits came from a bundled reference subtree. This is a
new supported embedding, with pinned official SDK 1.0.4258.31 header/license and
x64/x86 static loader subset in dependencies/third_party/WebView2. DesktopStub's
product build statically links x64 loader plus version.lib; no loader DLL needs
copying. Microsoft license/version/package+file hashes are recorded there.

No build, executable, installer, account, sign-in, cookies, provider or deployment
was exercised in this slice. Source-only checks verify fixed cookie URI/domain
filter, absent script/harness/log paths, weak callback lifetime and atomic parser/
save usage; owned whitespace checks passed. Native compilation, real Runtime
availability, login redirects, cookie issuance and user sign-in acceptance remain
for root's next gates. Inert host compilation stubs embedding entirely.

Primary API reference:
https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2cookiemanager
Official package:
https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.4258.31
