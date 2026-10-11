# Discord advanced host/profile validation, 2026-10-11

Initial host/editor gates passed after fresh compilation against the explicitly
frozen native/managed INI lock baseline and clean Microsoft Defender custom scans:
44 shared editor/credential checks, 228 content runtime/menu checks, 3 no-hardware
host checks, 1,750 DesktopStub source checks, and 17 passive CLI cases. Every
fixture/process was limited to 60 seconds through the existing review driver.
Usage was checked before tests (61% used; ordinary usage allowed). No real account,
Discord/session transport, process context, credential decryption/encryption,
Startup, package, device, browser or Windhawk operation was exercised.

## Shared APIs

`dependencies/DiscordRPC/profile_edit.h/.cpp` exposes typed functional descriptors
and the real default catalog through `ProfileOptions`, read-only
`ValidateProfileFieldGrammar`, `ReadProfileDocument`, `ParseProfileDocument`,
`ResolveProfileReference`, `ReadProfileOptionForEditor`, and
`NormalizeProfileSetting`. `ApplyProfileEditsToText` stages a complete decoded
option batch; `CommitProfileEdits` mutates only an existing profile under native
fresh INI locking. `MakeDefaultProfileDocument(text,result,error)` is pure setup
staging; `CreateProfileDocument` is separate explicit atomic no-overwrite setup.
The catalog comes from the actual functional source defaults, with explicit
bool/int/activity type/enum/client-ID/text/private-secret/opaque-secret metadata.
The real censor rule-order/replacement/regex grammar is reused without collecting
context or starting a service. Activity types 0/1/2/3/5 remain integers.

`ProfileEditPolicy` requires explicit private input for credential changes and
acknowledged host cadence mapping for `general.update_interval`.
`ProfileEditResult.requiresHostCadenceApply/requestedRefreshSeconds` reports that
mapping. `Options.refreshMs` is the actual service scheduler. Untouched comments,
unrelated sections, and plaintext/protected credential representations remain
unchanged. Changed text preserves the separate Discord and quoted native INI
escape layers, including literal backslashes, newlines and Windows paths. Changed
known fields are validated; untouched legacy/unknown rows are preserved.

`service.h` additionally exports
`ResolveConfiguredToken(plaintext,protectedValue,decoder,token,error)`. This pure
seam uses plaintext only when no protected value is configured. Protected input
and failed/empty decoding remain authoritative. The actual Gateway construction
supplies the existing DPAPI UTF-8 legacy decoder; editors, preview and injected
transports never call it. Six injected decoder tests prove selection and failure
behavior without any actual decryption or token source. Decoder exceptions cannot
echo their private text. No automatic migration or encryption was added.

## Host behavior

`--discord-set Section.Key=Value` is configuration-only by default and edits the
INI referenced by active `Discord.ProfilePath`; relative references resolve
against the host INI. A normal edit cannot create a missing file.
`--discord-create-profile INI` explicitly creates and binds the real functional
catalog defaults. Explicit normal `--set` host overrides are staged in the same
transaction. A contradictory creation binding or inconsistent explicit
`Discord.RefreshSeconds`/`general.update_interval` pair is rejected. A source
interval maps back to the host refresh; a normal host refresh supplied in this
advanced batch updates the source preference too. An unrelated edit preserves
legacy profile intervals, and the host scheduler remains authoritative.

Credential editing requires `--discord-private-input`; output never repeats the
value. The tray uses the same descriptor catalog, opens secret replacement fields
blank and masked, and detects stale host/profile text before saving. Its menu
identifies the active advanced profile and actual host cadence; inactive inline
Name/Details/State/ClientId controls remain editable and labeled for use without an advanced profile. The advanced interval dialog
starts from the actual host value. Menu reads do not decrypt, migrate, create a
provider or repair source preferences. Explicit plaintext input remains plaintext
in the INI and may appear in shell history/process arguments; the masked tray
avoids command-line entry, and `general.token_protected` remains available for an
explicit protected blob. No mandatory security migration was introduced.

`ga_discord_profile.inc` locks both paths in invariant normalized identity order,
rejects identical identities/physical files, validates both complete batches, and
stages strict UTF-8 byte vectors for both before publishing either one. Limits
include the BOM (1 MiB for profiles, existing native maximum for the host). Invalid
UTF-16 in an unrelated host comment cannot serialize to an empty/BOM-only file.
Each file is atomically published through native helpers. Reported host save
failure restores both original raw byte sequences, including encoding and opaque
credentials; failed setup removes only the profile created by that transaction.

This is ordinary write-failure recovery, not crash/power-loss atomicity across two
file publications. A noncooperating reader can observe the interim pair. No
journal or read-time writes were added. CLI rejects resident/helper/render/Startup
combinations and mixing the advanced transaction with other donor imports. The
configuration path neither starts a resident/provider nor reconciles Startup.

## Evidence and boundaries

The fixture covers explicit types, full-batch validation, escaping, exact untouched
credentials, optimistic conflicts, four concurrent complete writers, two injected
preview services with zero transports, pure setup, no-overwrite/missing-file
behavior, cadence matching/conflicts, contradictory bindings, raw UTF-16 rollback,
invalid-surrogate rejection before either write, private input/no echo, and passive
host command dispatch. Visible dialog acceptance and real context/Discord/network
acceptance remain untested. Actual DPAPI compatibility was retained in code but
was not exercised against an account or real credential in these fixtures.

Edited source/fixture files: `dependencies/DesktopStub/ga_discord_profile.inc`,
`ga_command_line.inc`, `ga_content_tray.inc`, `ga_render_only.inc`,
`DesktopStub/DesktopStub.cpp`, `DesktopStub/BuildDesktopStub.cmd`,
`DesktopStub/tools/ContentRuntimeTests.cpp`,
`DesktopStub/tools/DesktopStubSourceCheck.cpp`,
`dependencies/DiscordRPC/profile_edit.h/.cpp`, `service.h/.cpp`, `drpc_core.inc`,
and `tools/DiscordProfileEditTests.cpp`. Canonical maps/docs/test wiring and shared
INI lock implementation are independently owned. No donor was retired in these
host gates. Shared/editor fixture compilation uses /W4; editor and fixture use
/WX. The pre-retirement shared service still has historical unused app warnings.

Ignored logs and reproducible recipes: `build/review-validation/driver.ps1`,
`build-discord-host-oct11.cmd`, `test-discord-host-cli-oct11.ps1`,
`discord-host-build-oct11.log`, `discord-host-profile-oct11.log`,
`discord-host-runtime-oct11.log`, `discord-host-unavailable-oct11.log`,
`discord-host-source-oct11.log`, `discord-host-cli-cases-oct11.log`; each matching
Defender log and `*-scan.sha256` records the submitted executable. Exact scanned
binaries are preserved in that ignored directory:

| Binary | SHA-256 |
| --- | --- |
| DiscordHostProfileTestsOct11.exe | B77723BDA06A9A1F00A3B7838037D2B1D7FD28D3E2AA916259CCCC483455BCBA |
| DiscordContentRuntimeTestsOct11.exe | 2364EB5010C75A0375EC61077A457676C51293ECD92F68E02457F2CDBA9A4A99 |
| DiscordNoHardwareTestsOct11.exe | 48BA486BBBD9F77C90AE90DB50C4BDCF474755EB3AF61C56E5C29B1948516589 |
| DiscordDesktopSourceCheckOct11.exe | 6E2F41B01F4D3372E709C58A883DA5D303891BC4FC55A4779A95F7C3C71E380B |
| DiscordHostCommandsOct11.exe | BC51C66AF0824C25C8CEA4DE168E7E50E2CF833B69B48298499A61994DC818BA |
## Final scoped donor retirement

The tracked standalone donor is now retired: `DiscordRPC/DiscordRPC.cpp`,
`DiscordRPC.rc`, `README.md`, `build.cmd`, `TestDiscordRPCSource.cmd`, and
`tools/DiscordRPCSourceCheck.cpp` were removed, together with the shared-directory
`drpc_app.inc`, `drpc_command_line.inc`, and `drpc_tray.inc`. Only those explicit
tracked source files were deleted; ignored/local binaries, INIs, accounts, installed
state and Startup registrations were untouched. Canonical CI/maps/shared source
checker and user-facing migration docs are separately owned by the root agent.

The retained `drpc_environment.inc`, `drpc_core.inc`, `drpc_types.inc`,
`drpc_config_defaults.inc`, and `drpc_presence.inc` no longer contain standalone
resident globals, AppOptions/runtime tray structures, CLI/help/UI defaults,
automatic default-file writes, Startup reconciliation, path/instance lifecycle,
console/log sink configuration, automatic token protection/migration, or donor
dry-run routines. The functional default catalog, stateless rules, actual context
and presence construction, editor, service/cancellation, both transports, and
actual-send protected-token UTF-8 reading remain. Existing library diagnostic
hooks remain inert as its formerly disabled logger was; no app logger or file
sink remains. Public host/editor/service signatures are unchanged by pruning.

Fresh post-retirement build/gates passed: shared service /W4 /WX clean, 140 injected
service checks with 40,000 snapshot and 80,000 relay reads, 44 editor/credential
checks, 228 runtime/menu checks, 3 no-hardware checks, 1,750 DesktopStub source
checks, and 17 passive CLI cases. All six exact executable files were individually
custom-scanned clean before bounded runs (60-second deadlines). Usage was checked
again (64% used; ordinary usage allowed). This confirms the retained source after
pruning, including protected/plaintext selection with fake decoders; it does not
claim real-account, visible-dialog or live Discord/DPAPI acceptance.

New ignored evidence is `build-discord-retired-oct11.cmd`,
`discord-retired-build-oct11.log`, `discord-retired-service-oct11.log`,
`discord-retired-profile-oct11.log`, `discord-retired-runtime-oct11.log`,
`discord-retired-unavailable-oct11.log`, `discord-retired-source-oct11.log`,
`test-discord-retired-cli-oct11.ps1`, `discord-retired-cli-cases-oct11.log`, and
matching Defender/scan SHA-256 records, all under `build/review-validation`.
Earlier submitted host binaries and logs remain preserved too.

| Post-retirement binary | SHA-256 |
| --- | --- |
| DiscordRetiredServiceTestsOct11.exe | C648AE6415B0C7BC7FFB795F421641225D02125565F6D0EBDFDAE3EAEB8E3269 |
| DiscordRetiredProfileTestsOct11.exe | 83E9C465EC64E62185A3CA4F853F753555AD3F1360074C33E2E560425A68456D |
| DiscordRetiredContentRuntimeTestsOct11.exe | C59222FDCF991E4D9F1E0137C057080FD8ACC0D188A2D92D0C9794E241B00BB6 |
| DiscordRetiredNoHardwareTestsOct11.exe | 065DEAA8E6B0E8AF2A2BADEA91F15F15F8F28DFFDEA93AEAE83F53249AB2C3F6 |
| DiscordRetiredDesktopSourceCheckOct11.exe | B1236E96FB75A289810378588FEB7D78C2E005E54BD1C9893EE826814E1B0A1E |
| DiscordRetiredCommandsOct11.exe | 9A92B5B312199971E3DD43BA88FA6BCD50316F5C4FD2CB07CC58177C910755BE |

## Final review corrections and freeze

Independent review identified and fixed three material gaps before the final
product smoke. Explicit private credential edits now replace the effective
credential family: one nonempty plaintext/protected choice clears its opposite;
two nonempty choices reject the full batch; a blank-only edit clears both so it
cannot reactivate stale plaintext. Unrelated edits still preserve both opaque
representations exactly. Shared and host fixtures assert the actual
`ResolveConfiguredToken` selection using fake decoders, including no decode for
new plaintext and authoritative decode for a protected replacement. No encryption,
migration, DPAPI account access or transport is needed to edit or test it.

`content::ReadConfiguration` reads and validates ProfilePath before interpreting
the inline Discord representation. A trimmed nonempty advanced path preserves
raw ClientId/Name/Details/State without applying their inactive semantics. Path,
SendPresence, RefreshSeconds, OperationTimeoutMs and ClearOnStop still validate.
Empty/whitespace path restores inline validation; a rejected switch leaves the
previous complete configuration unchanged. The tray allows explicit stored inline
repair while advanced mode is active, labels those fields for use without the
advanced profile, and can switch through its common save route after repair. No
untouched values are reset.

`CommitProfileEdits` now holds the existing native IniWriteMutexGuard before its
1 MiB bounded capture and through the recursively locked native MutateFresh. A
cooperating replacement cannot enter the generic 16 MiB reread between those
steps. The fixture uses an isolated approximately 1.2 MiB UTF-16 comment profile
whose decoded/output bytes would fit, and proves rejection with exact unchanged
bytes after a cooperating lock owner replaces the original. Same-thread recursive
ownership is checked too. No parser/writer was duplicated and no API was added.

The first corrected monolithic host run stopped at its internal watchdog (124).
A fresh scanned diagnostic build with coarse group timings completed 236 checks;
its new Discord group took 734 ms, while repeated sparse default expansion in the
existing Notes/hardware/NowPlaying import CLI groups consumed approximately 9/8/15
seconds. An independent source trace found no held-lock or typed-dialog path in
the new repair fixture. The nominal 450 sleep ticks had also drifted beyond 45 wall
seconds. The final fixture seeds the real BuildInitialIniTemplate plus native
effective overrides only before those three import-focused configure-only cases;
stale entry/import/source-byte/rejection assertions remain, and dedicated sparse
initialization/default-write failures still run. Production default expansion,
flush/persistence behavior and timeouts are unchanged. The fixture watchdog now
uses an actual steady_clock 45-second deadline, with no retries or extension.
Migrating sparse profiles still repeats default expansion in production; normal
initial templates avoid that cost. Production optimization is deferred.

All final corrected gates passed after fresh scans: 282,209 model assertions with
100,000 randomized schedule iterations; 55 shared editor/credential checks; 239 host
runtime/menu checks; 140 injected service checks with 40,000 snapshot / 80,000 relay
reads; 3 no-hardware checks; 1,750 DesktopStub source checks; 24 passive CLI cases.
The final host fixture completed in 9.82 seconds (Discord group 0.29 seconds), leaving
headroom under its unchanged 45-second limit and external 60-second process bound.
Shared service/editor/fixture and model compilation use /W4; shared service/editor/
fixture/model use /WX where already supported. Native host SDK shadow warnings
remain outside /WX. Usage was checked before corrected gates (68% used, ordinary
usage allowed). No real accounts, transport, DPAPI credentials, context, hardware,
Startup/package, browser or visible dialog were exercised. Final owned diff
whitespace check passed. All owned code/fixtures are now frozen again.

Additional source files in this correction are `dependencies/content_engine.h`
and `tools/ContentEngineTests.cpp`; shared editor, tray and host/editor fixtures
listed above were extended. No signature or donor-retirement change followed.
Corrected evidence in `build/review-validation`: `build-discord-corrected-oct11.cmd`,
`discord-corrected-build-oct11.log`, `discord-corrected-model-oct11.log`,
`discord-corrected-profile-oct11.log`, `discord-corrected-runtime-oct11.log`
(the stopped initial run), `discord-runtime-diagnostic-oct11.log`,
`discord-runtime-final-build-oct11.log`, `discord-runtime-final-oct11.log`,
`discord-corrected-service-oct11.log`, `discord-corrected-unavailable-oct11.log`,
`discord-corrected-source-oct11.log`, `test-discord-corrected-cli-oct11.ps1`,
`discord-corrected-cli-cases-oct11.log`, and matching Defender/scan hash records.
Earlier submitted binaries, including the failed and diagnostic builds, remain
preserved. Exact final submitted binaries:

| Final corrected binary | SHA-256 |
| --- | --- |
| DiscordCorrectedContentEngineTestsOct11.exe | AF0762BEC8EB2566B3C878C25C8AA5BC39D710CC8C6F8435AACEAE948DA34CEF |
| DiscordCorrectedProfileTestsOct11.exe | 39E6FBDCDA3FEDBD7F912C6A698600AAD5B07F0C4CB0601B187B9AAFEB89482E |
| DiscordFinalRuntimeTestsOct11.exe | 8759A7B7DA072B0B76D61E2642A39789FF4AD7BD36F94A38DB176FABC6253B22 |
| DiscordCorrectedServiceTestsOct11.exe | CBAF8B2AFA9557332C9BC795CEDD727196AFC78186A838AC88384DC16CD8D758 |
| DiscordCorrectedNoHardwareTestsOct11.exe | F59D69DBCF6707D3187840A30E59779B9765003615E81AD7372C24329316F9B3 |
| DiscordCorrectedDesktopSourceCheckOct11.exe | 79982E16A433EFAD9E50333FA5E75475C8B28501C38B5FC7A3D66F1C726097B7 |
| DiscordCorrectedCommandsOct11.exe | 20841D5165B1CC2ABF0A5347188EF625728007E9328856E62FD3EE339F052E60 |
