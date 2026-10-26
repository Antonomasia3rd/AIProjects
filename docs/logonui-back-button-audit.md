# LogonUI Back restoration: access, source audit, and native evidence

Reviewed on 2026-10-26. This is an offline investigation and implementation brief. The installed mod source remains unchanged and no mod was installed, reloaded, or injected. A separate source candidate was subsequently compiled and tested inertly, as recorded below. No logon/session operation was triggered.

This checkpoint contains source, tests, build scripts, and the durable summary in
this document. Links into `analysis/` refer to private, untracked local evidence
and generated build artifacts; those files are preserved in the shared checkout
but are intentionally excluded from the source checkpoint. A fresh clone will
need those inputs restored or the relevant checks regenerated before following
those local evidence links. This checkpoint is not production acceptance.

## Confirmed request

The requested compatibility target is **Windows 8 and later, x86/x64, plus ARM64 on Windows releases that support it**. This is a target, not a completed compatibility claim. Windows 7 is a historical reference rather than an implementation target.

The purpose is to restore the historical Back logic and host its control in LogonUI Tweaks alongside the added system controls. Native behavior is context dependent: returning from Change Password, returning to account tiles, and cancelling a newly created unauthenticated remote credential request must retain their different Windows transitions. Existing native Back behavior should be preserved where present. The historical button's rendering does not itself establish the correct backend.

## Sources recovered

- ChatGPT: [Reversing LogonUI Back Button (1)](https://chatgpt.com/c/6ad382e3-94f8-83ec-b317-3b51f8b15ae8).
- ChatGPT: [Reversing LogonUI Back Button (2)](https://chatgpt.com/c/6ad8e453-6860-83ec-bf47-62996c0b7cb4).
- Authoritative installed source: `C:\ProgramData\Windhawk\ModsSource\local@logonui-tweaks.wh.cpp`.
- Metadata: LogonUI Tweaks `0.1.0-alpha5f`, 961,548 bytes, 27,091 lines.
- Source SHA-256: `3F8F6E78E9EFFD2F3E058BDF8E291115A18FA59F0FAEB6BBD25D336913377915`.
- Prior reports, logs, exported conversation, native binaries and symbols: `D:\Users\Amiya\Desktop\techstuff`.
- Mounted Windows references: E: 10 22H2, F: 10 1809, K: 11 25H2, L: 8.1, M: 10 1511.

The app reader exposes recent chat messages and attachment paths, but several assistant messages resolve only to saved-content references. The browser view of chat (2) was also inspected; several saved responses were still unavailable there. The local PDF preserves the user's messages and explicitly omits assistant replies. Consequently this investigation does not claim to have recovered every assistant conclusion in either chat. Local binaries, exact symbols, and actual log contents take precedence over unsupported prior interpretations.

## Current mod and review findings

The exact installed source contains **no Back command implementation**. Its native/system-control strip has Battery, Volume, Clock, IME, Input, Network sign-in, Disconnect, Network, Accessibility, and Power.

The current implementation is amd64-only. Its stated system-control floor is Windows 10 1809. User-list and transient-status features have a separate private-ABI gate for one `Windows.UI.Logon.dll` timestamp/image-size pair (`0xF49AD882`, `0x00402000`). A claim that the whole mod currently supports Windows 8+ or all Windows builds would be incorrect. README runtime-test statements were not rerun in this investigation.

The focused review identified three source-visible issues. Line numbers refer to the source hash above:

| Priority | Finding | Source evidence | Required correction |
|---|---|---|---|
| P1 | Hot unload may continue after XAML cleanup fails, leaving external delegates pointing into the unloaded DLL. | UserStatus cleanup requests a 1200 ms rendezvous and only logs its result at 26579-26586; event detachment occurs on the owning UI thread at 24224-24234. Later controls cleanup at 13789-13815 does not detach UserStatus registrations. | Track actual retained registrations and their owning threads, unify cleanup, and prove detachment plus callback/dispatcher drain before DLL removal. A timeout alone cannot make retained callbacks safe. |
| P2 | Known-build hook selection trusts OS build/UBR and a function boundary over the loaded DLL's own structural evidence. | Fast-path selection/acceptance at 26693-26748; structural disagreement still keeps the hardcoded target at 26773-26781. | Require validation against the actual module. Use the existing unique structural resolver independently of logging; reject a conflicting fast-path hint. |
| P2 | Hook-worker creation failure omits shutdown of an already-started battery notification worker. | Watcher starts at 26893; failure rollback at 26896-26915 closes the stop event without signalling/joining that worker. Its event wait is at 5774-5776; normal cleanup at 5793-5804. | Signal the stop event, join and close the watcher, cover its registered window class, and keep shared handles alive until all users exit. |

No timeout, wrong-image collision, or resource-exhaustion failure was induced. These findings are a source audit, not reproduced crashes. Detailed helper/call-path analysis is saved in [the local mod audit](../analysis/logonui-back-audit-20261026/mod-audit.md).

## Correction to the earlier Winlogon interpretation

The prior `RemoteBack-Producer-Static-Findings-20261026.md` described K: Winlogon RVA `0x130F4` as an internal credential-request routine. The newly available public PDB and disassembly identify it as:

```cpp
unsigned long HandlePowerRequestFromLogonUI(
    _WLSM_GLOBAL_CONTEXT*,
    unsigned int,
    LogonUIPowerButtonSource,
    int*);
```

The F: equivalent is at RVA `0x707CC`. The observed branch can manufacture `ERROR_CANCELLED (0x4C7)` **after processing a power request**. It does not establish a general Back/cancel producer and does not justify interpreting the earlier controller `numericOut=1` observation as native Back.

The actual K: `RequestCredentials` function starts at `0xDC80`, with `WluiRequestCredentials` at `0x11C40`; the corresponding F: starts are `0x17BCC` and `0x2A860`. The exact-symbol evidence records distinct output pointer types and control-flow paths. The actual K: request reaches `RPCRT4!NdrClientCall3` with procedure index `0x11`; the LogonController RPC server/output propagation remains to be traced. Signal-table inspection also confirms that `LogonUI_Cancel` is index 38 on K: and 37 on F:, so a universal numeric signal is unsuitable.

K: PDB and PE CodeView GUIDs agree, but the public PDB reports age 2 while the PE reports age 1. F: GUID and age agree. The newly named functions align with disassembly and runtime-function starts; the K: age difference remains disclosed and must not be represented as an exact GUID-and-age match or permission to invoke private code.

Recomputed PE/PDB hashes, GUIDs, ages, symbol RVAs and signal indices are in [exact-winlogon-identities-and-symbols.json](../analysis/logonui-back-2026-10-26/exact-winlogon-identities-and-symbols.json). The [native call-boundary report](../analysis/logonui-back-2026-10-26/native-call-boundary.md) explains the output propagation, cancellation consumers, and RPC boundary. Associated text files in that directory contain the inert disassembly. These RVAs describe the exact inspected images only.

## What the existing runtime evidence establishes

`D:\Users\Amiya\Desktop\techstuff\Producer-20261025-103818-9224.log`, lines 10-39, records RPC/logoncontroller upstream cancellation reaching `Windows.UI.Logon.dll`'s async `OnCancel` for the same operation. `OnCancel` returns void and upstream cancellation returns `S_OK`; the observer explicitly labels that HRESULT as **not the credential result**.

Earlier reports assert a Windows 8.1 native `RequestCredentials -> 0x4C7 -> LogonUI_Cancel` runtime chain. The original trace was not located in the six loose Winlogon logs or a bounded scan of text entries in 18 relevant local ZIPs. That result remains a prior reported finding rather than a freshly verified runtime trace. Other observed normal completions through ShutdownUX or CredProvDataModel are separate contexts.

The remaining implementation boundary is the legitimate UI-side action and its owning request object: availability, enabled state, interface/ABI, lifetime, threading, and the resulting native state transition. A matching method name, prefix, or successful callback HRESULT is insufficient. The prior corpus reports nonunique `OnCancel` prefixes, making a short byte-pattern resolver unsuitable.

Detailed recovered intent and evidence confidence are saved in [prior-research.md](../analysis/logonui-back-audit-20261026/prior-research.md).

## Compatibility requirements

| Family | Requested architectures | Evidence available in this review | Implementation implication |
|---|---|---|---|
| Windows 8 / 8.1 | x86, x64 | Mounted 8.1 x64 and older AuthUI analyses; no Windows 8 or x86 binary coverage verified here. | Preserve the existing native action and its state-dependent behavior; use a separate AuthUI-era integration. |
| Windows 10 1507 / 1511 and other releases before 1809 | x86, x64; ARM64 where applicable | Mounted 1511 x64 and older corpus reports. | Current Tweaks XAML resolver does not claim this floor. Validate UI/backend families independently. |
| Windows 10 1809 and newer | x86, x64, ARM64 where applicable | Mounted 1809/22H2 x64; exact 1809 Winlogon evidence. | Existing controls support does not prove Back support or an architecture port. |
| Windows 11 | x64, ARM64 | Host/mounted x64 references and exact K: evidence. | No ARM64 backend or resolver was validated in this review. |

All mounted/host binary samples inspected by the inventory subtask were AMD64. Architecture support requires actual instruction decoding, pointer/layout and calling-convention validation, compiler/runtime compatibility, and matching evidence; changing Windhawk metadata alone is insufficient.

The installed Windhawk executable is 1.7.3. Windhawk's author documents native ARM64 engine support starting in 1.6 ([release announcement](https://ramensoftware.com/windhawk-v1-6-arm64-support-better-compatibility)). The 2.0 alpha release notes explicitly drop pre-Windows-10 and 32-bit OS support ([2.0 alpha 1](https://github.com/ramensoftware/windhawk/releases/tag/2.0.0-alpha.1)). The requested legacy/x86 range therefore also needs a compatible Windhawk 1.x toolchain; modern-engine support alone cannot establish this full range.

## Implementation order and acceptance rules

1. Correct mod lifecycle/rollback and resolution issues in a reviewable candidate before adding further retained UI/request callbacks.
2. Trace actual RequestCredentials/WluiRequestCredentials and LogonController interfaces offline with matched PE/PDB evidence. Preserve the distinction between Win32 credential results, HRESULT invocation results, async status, and power-action output.
3. Resolve Back independently of the unrelated user-list ABI gate. Provide per-family and per-architecture implementations; unknown identities or unproven contexts leave the feature unavailable.
4. Host the control in Tweaks' UI lifecycle once the producer contract is established. Bind visibility/enabled state to the active native context and release all request references and event registrations on context retirement/unload.
5. Compare native and restored behavior on disposable, checkpointed VMs for newly created unauthenticated RDP/Enhanced sessions, existing remote sessions, account selection, Change Password, Security Options, and cancellation during pending work. Test repeated load/unload and context replacement. Static coverage and runtime parity must be reported separately.

Power Disconnect, Fast User Switching, synthetic Escape, WTS logoff, forced HRESULT/output values, and direct Winlogon signal injection are not accepted substitutes for the native Back contract. The restoration is not yet integrated or runtime validated.

## Continuation: source candidate and native return-to-lock intent

The user authorized continuation and supplied the installed PDB downloader path. An existing absolute image path was verified before the tool was used to retrieve the missing Windows 10 1511 LogonUX public PDB. Its GUID agrees with the image; the public-PDB age difference remains recorded rather than hidden.

A separate [alpha5g source candidate](../legacy/WindhawkMods/local@logonui-tweaks.wh.cpp) now contains the SystemProvider resolution and battery initialization-rollback corrections. It also checks AMD64 before interpreting the shared PE view and releases the battery window class if window creation fails. The old OS-build/UBR fast-path table is removed; every SystemProvider hook target comes from actual structural validation or the existing symbol fallback.

The candidate's [companion notes](../legacy/WindhawkMods/LogonUiTweaksCandidate.md) explicitly leave **P1 open** and label it **not ready to install or hot-reload**. No Back control or Windows 8/x86/ARM64 port is claimed. The [lifecycle design and independent review](../analysis/logonui-back-2026-10-26/lifecycle/design-and-candidate-review.md) explain why reliable revocation accounting, actual owner-thread cleanup and per-attempt dispatcher-fence lifetime are needed; a timeout, empty bookkeeping list, unloading flag or module pin alone is insufficient.

Validation completed:

- Whole-candidate x64 syntax check and an optimized x64 DLL build using the installed Windhawk 1.7.3 engine import library.
- 47 assertions in an inert harness that executes the actual extracted initialization function with mocked Windows/feature effects, including signal/join/close ordering and startup failure/success cases.
- The actual extracted structural resolver agrees with independently named F:/E:/K: `_OnLoaded` targets and rejects a wrong machine type, changed handler prefix, absent unwind directory and ambiguous matching candidates in private data buffers.
- Installed alpha5f source SHA-256 remains `3F8F6E78E9EFFD2F3E058BDF8E291115A18FA59F0FAEB6BBD25D336913377915`.

The [build receipt](../analysis/logonui-back-2026-10-26/candidate-validation/candidate-build.json), [inert-test receipt](../analysis/logonui-back-2026-10-26/candidate-validation/candidate-tests.json), and [test log](../analysis/logonui-back-2026-10-26/candidate-validation/candidate-host-tests.log) record paths, hashes and limits. The [baseline-to-candidate patch](../analysis/logonui-back-2026-10-26/candidate-validation/alpha5f-to-alpha5g.patch) provides a compact review against the unchanged installed source. The final inert-test receipt uses the already-validated syntax stage's skip switch; the optimized build also compiled the complete final source. The compiled candidate was never loaded.

### Controller and RPC boundary is now linked

The [exact controller report](../analysis/logonui-back-2026-10-26/controller/controller-rpc-and-return-to-lock.md) links Winlogon's RPC procedure `0x11`, through the matching interface UUID and server routine table, to `CLogonController::RequestCredentials` and its checked vtable slot.

It establishes three distinct output/result channels:

| Controller result | Final operation-aborted output | RPC result |
|---|---:|---|
| `S_OK` | zero | success |
| `S_OK` | nonzero | `ERROR_OPERATION_ABORTED` **995**, through HRESULT `0x800703E3` |
| `0x800704C7` | either | `ERROR_CANCELLED` **1223** |

The unsigned shutdown/power mask is separate from that final operation-aborted `int*`. The old `S_OK/numericOut=1` observation cannot establish the historical Back outcome.

The previously unnamed controller gate is now identified as **Return To Lock intent**, written by the native `IReturnToLockListener::OnReturnToLockRequested`. For an active credential request the listener marks the intent and then invokes ordinary native Abort. The request's final classification can then return the cancellation HRESULT that the RPC adapter maps to 1223. The exact K: native chain is:

```text
WNF_LOGN_RETURN_TO_LOCK subscriber
  -> CProcessStateManager::_OnReturnToLockRequested
  -> IReturnToLockListener::OnReturnToLockRequested
  -> mark active request's Return To Lock intent
  -> CLogonController::Abort
  -> RequestCredentials / RPC -> ERROR_CANCELLED (1223)
```

The event value and subscriber are named and the pointer/call relationships are checked against the exact hashed x64 image. The legitimate publisher, event scope/access constraints, originating UI context and restored runtime outcome still need proof. No WNF event was published, and the constant is not supplied as an unconditional backend.

### UI cancellation and lifetime are also distinguished

The [exact LogonUX producer report](../analysis/logonui-back-2026-10-26/producer/ui-native-producer.md) compares K:/F:/M: native methods and records differing private object layouts. Ordinary async Cancel enters Canceled state 2 and then fires `OnCancel`; direct `OnCancel` invocation would omit the state transition. Native `IAbortableCallback::OnError` enters Error state 3 and retains its HRESULT. The verified Change Password Cancel producer uses that error path and performs its own request/UI cleanup. That command must stay confined to its native context.

The RootViewModel retains the outstanding request callback, registers its continuation event, removes that subscription during retirement, and releases the callback. A restored control must follow the active native context and its retirement rather than retain an arbitrary private pointer.

The new evidence identifies the native intent/cancellation mechanism more precisely. It does not yet establish a portable, valid UI-side invocation contract or a finished restoration.

### Historical Back is now independently checked in native code

The [Windows 8.1 comparison](../analysis/logonui-back-2026-10-26/producer/win81-native-back-comparison.md) uses the original archived AuthUI binary/PDB pair, verifies matching GUID and age, and applies the PDB's OMAP source-to-optimized address mapping before disassembly. Its PE hash also matches the mounted L: AuthUI file. Displayed file-version strings differ by path despite identical hashes; the report records that discrepancy and uses exact PE/PDB identity as authority.

The native mode-32 Back action dispatches `_OnCancel(false)`, which still evaluates the actual frame/list/dialog context. In the credential-request cancellation path, its callback queue receives `ERROR_CANCELLED (1223)`. The separate generic-abort operation writes `ERROR_OPERATION_ABORTED (995)`, and normal completion does not overwrite a previously aborted item. This establishes the distinction statically without relying on the previously reported but unavailable signal-33 runtime log.

That historical request-completion distinction agrees with the modern controller's separate Return To Lock intent. It strengthens the candidate semantic route while leaving legitimate modern invocation, context gating, lifecycle and restored runtime parity unresolved.

### Bounded publisher search did not establish an invocation contract

The [publisher findings](../analysis/logonui-back-2026-10-26/publisher/publisher-findings.md) record a 64-image K:/F:/M:/L: search using full/split constants, state names and WNF imports, followed by disassembly of candidate publish call sites. Only the inspected K:/F: controller images contained the complete Return To Lock state value. The directly verified K: references subscribe/query the state.

The AuthUI publisher found in K:/F: is `_InitiateShutdown@CSlideToShutDownScreen` and uses the different `WNF_LOGN_SLIDE_TO_SHUTDOWN` state. It must not be mistaken for Back. M: AuthUI still contains a context-dispatching native `OnBackButton`, but its presence alone does not prove that a modern LogonUX page uses it. Untranslated L: PDB labels from an earlier attempted query were rejected; the separate historical comparison above uses the required OMAP translation.

The search is a bounded negative result, not proof that Windows has no publisher. A different component, dynamic interface or derived state value can still originate the notification. The next backend decision needs the legitimate originating interface/context or a separately validated instance of the native Return To Lock listener; no generic WNF sender was added to the candidate.
