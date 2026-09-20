# Real-Time Notes shared data provider

Updated 2026-09-10. The existing deskband's data engine is now reusable by DesktopStub without loading a COM deskband or relying on Explorer/application globals. The standalone deskband remains because its UI, registration, and account-management behavior are not fully consolidated.

## Implemented boundary

`dependencies/content_sources/notes.inc`, namespace `aip::notes`:

- `ResourceKind`: `Resin`, `Stamina`, `Charge`; `ParseResourceName` and `ResourceName` map configuration labels.
- `Configuration`: resource; explicit UTF-8 `uid`, `ltoken`, `ltuid`; `refreshSeconds` (default 300); `timeout` (15 seconds); `maximumBytes` (1 MiB); and `canceled` callback.
- `Snapshot`: resource and `StateKind`, numeric current/maximum/recoverySeconds, line1/line2/tooltip, game detail menuLines, and normalized refreshSeconds.
- `ParseResponse(resource, json, refreshSeconds)` can run directly on fixtures.
- `Fetch(configuration, transport)` supports an injected transport; normal operation uses shared `aip::FetchHttpBytes` with one total deadline, cancellation, and response bound.

The provider reads no files, account stores, registry, or host settings; performs no logging, DPAPI, UI, package registration, or playback actions. Credential storage and decryption belong to the host. Shared `HttpFetchOptions` now accepts named header pairs, validated without putting header contents in errors.

The deskband aliases shared resource/state/account/snapshot models. Its existing account/cookie loader remains separate, applies its refresh override, and invokes the shared fetcher. A window-generation cancellation predicate aborts work for a closed/replaced deskband. Private signature, HTTP, response parsing, and data-formatting bodies were removed rather than left duplicated. DesktopStub source/schema/menu integration is owned by the parent task.

## Bugs repaired while extracting

- Required response status is read from the root and resource fields from `data`; unrelated nested fields no longer satisfy those lookups.
- JSON grammar, UTF-8, required counts, overflow, negative values, and missing partial-resource recovery are checked before producing a successful snapshot.
- A partial resource with zero recovery displays `Recovery unavailable`, rather than falsely saying `Full`. Full resources may omit the countdown.
- Expedition completion counts use each item's status field, rather than counting every string that happens to say Finished.
- Error text is bounded without splitting UTF-16 surrogate pairs or retaining embedded NUL termination.
- Existing per-phase synchronous HTTP was replaced by the shared total-deadline/cancellation helper. Asynchronous buffers/context remain alive through WinHTTP handle closure.

## Verification completed

- `tools/TestNotesSource.cmd`: **40 checks passed**, including **2,000 concurrent fixture parses**. Tests cover all three game snapshots and detail lines, malformed/nested/missing/negative/overflow values, API errors, recovery truthfulness, deterministic DS signature compatibility, existing URL/header/region semantics, injected transport, cancellation, response bounds, header size/syntax, and exception diagnostics that do not expose credential text.
- `legacy/RealTimeNotesDeskband/BuildDeskband.cmd check`: MSVC `/W4 /WX` compilation passed and **67 source checks passed**.
- `legacy/RealTimeNotesDeskband/BuildDeskband.cmd new`: a side-by-side DLL linked successfully with MSVC before the final small header-limit/diagnostic-boundary refinements. The final compile and fixture run cover those refinements. The DLL was never loaded or registered.

Every test used synthetic values and injected transport. No real account/cookie files were read, no credentials were decrypted or printed, and no network request, deskband registration, or Explorer instance was exercised. The final pre-test usage check reported 87% five-hour and 66% weekly consumed. Test processes completed.

## Compatibility limits and retained observations

The DS salt, client-version headers, UID-derived server mappings, and endpoint paths were extracted from existing code and retained. Tests establish that those rules were preserved; they do not establish current live HoYoLAB compatibility. The existing mappings include Chinese server names with overseas API hosts, which may require separate service-specific work. Endpoints/auth assumptions may be obsolete and were not tested with an account.

Existing deskband account persistence still performs its older DPAPI migration. The new provider neither requires nor implements that storage policy; DesktopStub can supply explicit values according to the user's current opt-in-security rule. There is no automatic account import or profile discovery in this provider.

Live response/proxy/TLS behavior, region/account compatibility, and the deskband's visual/COM lifecycle remain outside this fixture-only validation. No new trust/location/security enforcement was added.
