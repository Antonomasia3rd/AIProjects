# Notes integration continuation, 2026-10-11

The scoped Notes data/action/import slice is implemented and validated. No donor
retirement or release/map change is included.

The slice adds per-entry `NotesDetail=Summary` (default) or an available metric
from the shared Notes detail catalog. Existing summary count/recovery behavior is
preserved; separate cycle entries can show commissions/expeditions/realm/bosses,
training/reserve/Echo of War, engagement/scratch card/store without stacking all
details into clipped text.

New protected account fields are `LTokenProtected` and `LTUidProtected`; donor
`LTokenV2Protected`/`LTuidV2Protected` aliases remain readable. Per-account
`RefreshSeconds=0` inherits `[Notes] RefreshSeconds`. Import copies protected
hex blobs unchanged. Only the configured fetch worker resolves credentials;
inert fixtures will inject credential decoding and HTTP transport. No automatic
encryption, registry discovery, browser/login or clipboard operations are added.

CLI: `--notes-import-profile INI`, `--notes-import-cookie GAME=JSON`, and
`--notes-clear GAME` are configuration-only actions. Imports expand into one
typed native INI batch after `--ini` resolution; explicit `--set` overrides
imported values. `--notes-refresh GAME|All` uses bounded existing-resident IPC
and fails if no resident exists; it never starts one. Tray uses matching actions.
Import sources remain unchanged. The helper is
`dependencies/content_sources/notes_profile.inc`; the portable catalog is
`dependencies/content_sources/notes_model.h`.

INI import uses the repository's native effective-value parser: repeated account
sections merge and the last assignment of a key wins. Legacy primary credential
names retain their established precedence over fallback aliases. A structural
pass reuses the shared line/header/name helpers and rejects malformed account
assignments (including missing `=`) before publication. All values are staged
locally until the entire import validates; errors do not echo credential values.
The source profile is never written. No separate INI value/quoting parser was
introduced; quoted value semantics remain those of the native baseline.

Fresh binaries were compiled, custom-scanned by Microsoft Defender, then run
through the existing review driver with 60-second fixture deadlines. Results:

| Gate | Result |
|---|---|
| Notes source/import/decryption fixtures | 58 checks, 0 failures, including the final repeated-section/malformed-assignment review |
| Portable content engine | 282,185 assertions; 100,000 randomized schedule iterations |
| Inert DesktopStub runtime/menu | 149 checks, 0 failures |
| Passive native CLI | 6 cases: protected import/explicit override, cookie JSON, clear, malformed source with existing/missing target, refresh with no resident |
| DesktopStub source contracts | 1,747 checks |

The full host/CLI gates preceded the final file-only parser review adjustment;
the 58-check source/import fixture was rebuilt/scanned and passed afterwards.
Host fragments were then transferred to the hardware-source worker for its
separate integration. Root should rebuild the combined host before committing
that later slice.

The source/import and portable fixtures compile with warnings as errors. The
normal native DesktopStub review product also compiled and scanned clean;
execution was limited to the documented passive CLI paths. Existing SDK GDI+
warnings remain in host builds. No real account, cookie file, DPAPI encryption or
decryption, HTTP endpoint, browser, clipboard, driver, registry fallback, Startup
or package operation was invoked. The credential/transport workers used injected
synthetic implementations. Fixtures proved late old-account results cannot
publish after clear/reconfiguration and showed summary/detail views use the same
cached source snapshot.

Evidence lives in ignored `build/review-validation/`: `notes-source-final-oct11.log`,
`notes-integration-oct11.log`, `notes-content-engine-oct11.log`,
`notes-content-runtime-oct11.log`, `desktop-notes-source-oct11.log`, the six
`notes-cli-*-oct11.log` files, build logs, matching Defender logs and scan hashes.
`test-notes-cli-oct11.ps1` and the Notes build/run scripts retain reproducible
bounded commands. A first passive CLI assertion expected unquoted INI output;
the script was corrected to accept the existing native quoted serialization.
No product behavior changed for that fixture correction.

Live HoYoLAB endpoint compatibility, real-user protected blobs, visible tray
acceptance and older target Windows remain unverified. Registry-only legacy
account discovery is intentionally absent; explicit INI/JSON import is the
supported migration route in this slice. The existing security-default conversion
remains deferred. The retained deskband UI/registration host is not recreated.
