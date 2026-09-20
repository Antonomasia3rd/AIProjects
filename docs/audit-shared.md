# Shared dependencies and non-DesktopStub audit

Updated 2026-09-08. This records code observations and changes during the current repository consolidation. The large pre-existing dirty working tree is preserved. Findings here are not proof that every app, device, or Windows shell variant has been exercised.

## Current requested rules

The user's current rules replace older generated documentation: reusable implementations belong in `dependencies`; project folders compose them with product declarations; INI, CLI, and tray settings should agree; resident startup uses the per-user Startup folder, with the Windows packaged startup API permitted for packaged apps. Setup convenience comes first. Optional security work follows functional repair. Old mandatory policy prose is retired, while remaining code restrictions are documented honestly rather than silently claimed fixed.

## Fixed in this audit

| Finding | Change | Verification |
| --- | --- | --- |
| Native Startup writes staged `.tmp.lnk` files directly into the launch folder. A crash or simultaneous logon can launch an unfinished/duplicate entry. The managed helper already avoided this. | `dependencies/startup_shortcut.inc` stages in sibling `.AIProjects-StartupStaging`, then atomically renames into the per-user Startup folder. Staging is outside the launch folder on the same parent volume. | Real ShellLink install/query/replacement/removal and staging-location regression in `tools/SharedBaselineTests.cpp`. |
| Managed INI reads strict Unicode but writes with replacement encoding. An unmatched UTF-16 surrogate is silently replaced while save reports success. | `dependencies/managed_ini.cs` uses strict UTF-8 for default creation and atomic batch saves. | Invalid Unicode update leaves the original file intact; invalid default creates no INI and cleans staging files, in `tools/LegacyUtilitiesTests.cs`. |
| Tooltip text can include an embedded NUL, hiding status text from Win32. Length truncation can split an emoji into an invalid UTF-16 half. The managed fallback also retained surrounding whitespace. | Shared native and managed tooltip normalization removes invisible NUL termination, trims fallbacks, enforces shell/WinForms limits, and preserves surrogate pairs. | Native and managed behavior tests exercise NUL-only values, fallback, limits, and a pair at the truncation boundary. |
| DiscordRPC tray does not respond to keyboard Enter/Space activation (`NIN_KEYSELECT`). | Routes keyboard activation to its tray menu in `dependencies/DiscordRPC/drpc_tray.inc`. | 176 DiscordRPC source checks pass; the independent validation agent built the product. Mouse double-click refresh remains unchanged. |
| Managed apps reject or misread native quoted INI settings, and trim significant whitespace when saving. | Managed parsing and writing now use the valid DesktopStub quote/comment/path dialect; legacy bare assignments remain accepted. | Both languages read one 16-case fixture; managed save/reload covers comments, escaped paths, significant spaces, deduplication across repeated sections, Unicode, unrelated sections, and UTF-16-to-UTF-8 migration. |
| SMTC data retrieval was embedded in NowPlayingTile's resident/cache/logging globals with indefinite asynchronous waits. | New `dependencies/content_sources/smtc.inc` exposes a read-only `ReadSmtcContent(Text&, error, timeout)` provider with MTA scope and a common async deadline. | `tools/TestSmtcSource.cmd`: 13 contract/runtime checks, including 20 concurrent real system reads and handle-growth checks. No playback manipulation, registration, or media metadata logging. |
| RSS synchronous HTTP had per-stage timeouts, allowing a trickle response to occupy the provider worker indefinitely and delaying shutdown. URL splitting also relied on fixed host/path buffers. | New shared `dependencies/http_fetch.inc` uses asynchronous WinHTTP with one total deadline, worker-side cancellation checks, bounded response bytes, explicit query/fragment parsing, and callback/buffer lifetime through `HANDLE_CLOSING`. Only `RssHttpGetText` was migrated. | `tools/TestHttpFetch.cmd` passes 15 localhost checks, including delayed headers, trickling body, cancellation before/after send, exact/oversized lengths, chunking, status/empty responses, query fidelity, long paths, 30 timeout/late-callback cycles, and complete callback-state/read-buffer release. |

The pre-existing shared native tray registration already sets `NIF_SHOWTIP` for version-4 icons. That specifically addresses the silent-hover state described by the user; this audit did not introduce that existing fix.

## Verification completed

- Before these changes, `tools/TestSharedBaseline.cmd` passed its runtime suite and 297 source checks; `tools/TestLegacyUtilities.cmd` passed.
- After these changes, both runtime suites and all 297 native source checks pass, including the new regressions. An old source-token check expected direct `Trim(tooltip)` calls; it was updated for the NUL-sanitizing helper.
- `DiscordRPC/TestDiscordRPCSource.cmd` passes 176 checks. `tools/TestSmtcSource.cmd` passes its Windows-only read-only harness; the final run returned 21 successful/idle snapshots and zero unavailable/error results.
- `tools/TestHttpFetch.cmd` passes all 15 checks with only a `127.0.0.1` fixture server. Windows CI now runs it, and DesktopStub's project-map entry names `http_fetch.inc` and `content_engine.h` explicitly. RepoTools project-map validation passes for all 14 projects.
- These tests use test-scoped temporary INI/shortcut identities and clean their own launch entries. No resident application was installed or started to test hardware features.

## Remaining findings and integration work

1. Physical relocation alone is not an overlay architecture. `dependencies/<Product>` still contains application globals and entire app bodies. Source providers must expose snapshots and declared options through shared interfaces, rather than include another app's entry point or tray engine.
2. Valid native and managed INI value syntax now agrees. Managed malformed-target-line and unsafe programmatic-key rejection remains stricter than native. A compatibility change to observe: bare `#`/`;` now starts a comment, matching DesktopStub; literal comment characters belong inside quotes. Standard legacy bare assignments remain readable, and writes preserve significant quoted whitespace.
3. Native and managed Startup helpers both use the current user's Startup folder. Native mutations use a session-local mutex; managed mutations are user-scoped across sessions. Multi-session writes to the same native INI/shortcut remain an audit target.
4. ADBController is currently a foreground UI with no tray or startup configuration. Its old README claimed that absence was intentional policy; it is now tracked as missing functionality under the current rules.
5. RealTimeNotesDeskband is an Explorer COM deskband and currently saves DPAPI-protected credentials by default. `KeepLegacyPlaintextSecrets` is rollback compatibility, not a complete opt-in-security mode. Existing token persistence needs an explicit migration before it meets the user's requested default.
6. SecureDesktopLauncher, AllowContentAboveLock, and YourPhoneHideBanner retain mandatory protected-path checks in code. They are privileged/service integrations whose present behavior cannot be described as opt-in. Security-default migration remains open while functional consolidation is completed.
   DiscordRPC also automatically migrates plaintext Gateway tokens to DPAPI and currently has no persistent portable plaintext mode. Its README now identifies this gap rather than presenting forced encryption as the current requested rule.
7. SMTC text is now available through an independent provider. NowPlayingTile still owns artwork/widget/native delivery features; full feature parity is not complete, so the standalone app is retained. The provider timeout bounds asynchronous waits and requests cooperative cancellation; synchronous Windows COM calls are not a hard process deadline. Windows 8/8.1 have no supported current-session SMTC manager, so this source reports unavailability there.
8. Several project suites mostly check source tokens. Passing them is useful for wiring regressions but does not demonstrate actual keyboard navigation, shell tooltip display, high-DPI rendering, hardware behavior, or Explorer restart recovery.

## Retired generated rules

The following old statements were identified and the user confirmed that current rules override them:

- `legacy/ADBController/README.md`: intentionally no tray/startup.
- `legacy/RealTimeNotesDeskband/README.md`: mandatory DPAPI-only new saves.
- `legacy/SecureDesktopLauncher/README.md`: protected-path policy as an immutable project rule.
- `legacy/AllowContentAboveLock/README.md` and `legacy/YourPhoneHideBanner/README.md`: protected Program Files deployment as a repository-wide design rule.
- `dependencies/README.md`: relocation without sharing, and a blanket prohibition on any cross-product provider reuse. Build ownership remains relevant for CI selection; shared providers are the intended integration boundary.

Their current runtime restrictions are still described where applicable. Documentation changes do not imply the corresponding code defaults have been changed.

## SMTC references

The provider reads system media session properties through Microsoft's [SMTC session manager](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessionmanager). Its async wait uses one completion handler, followed by `GetResults` only after terminal status; Microsoft's [C++/WinRT timeout documentation](https://learn.microsoft.com/en-us/windows/apps/develop/cpp-winrt/concurrency-2#asynchronous-timeouts-made-easy) explains why repeated `wait_for` calls or following it with `get` are invalid.

## HTTP references and scope

The completed HTTP implementation follows Microsoft's [WinHttpCloseHandle lifetime and cancellation guidance](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle) and [callback status contract](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nc-winhttp-winhttp_status_callback). It never frees callback context just because close returned, and never cancels a synchronous request by closing it concurrently. Callback code only signals the worker; all request APIs are sequenced by that worker. The timeout/cancel harness specifically exercises late callbacks after the public fetch returns.

RSS XML parsing and tile delivery remain host concerns. Empty HTTP bodies are valid at the transport layer and are rejected or interpreted by the RSS parser as appropriate. Proxy/TLS defaults are preserved; Windows 8 falls back to its default-proxy mode if automatic-proxy mode is unavailable. Live external proxy/TLS behavior and network failures across every Windows version were not tested here.
