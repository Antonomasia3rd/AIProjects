# Repository rework audit

Started 2026-09-07. This file is the durable handoff record. Read it and the linked audit files after any context reset. Findings describe the current working tree, including extensive pre-existing uncommitted changes. **Latest user clarification: prior uncommitted implementations have no exemption from the rules and must be edited when they conflict.** Preserve useful work/data, not incorrect behavior or obsolete policy.

## User requirements (confirmed in this conversation)

- Shared implementation belongs in `dependencies`; app folders provide includes, resources, and configuration values. Prefer DesktopStub implementations after checking them for defects.
- INI, CLI, and tray settings must agree. Every resident app needs startup controls; ordinary startup uses only the per-user `shell:startup` folder. Packaged startup is a separately named Windows API option.
- Ease of setup takes priority. Additional security must be opt-in and deferred until functional fixes; explain actual risks and existing opt-in protection in user documentation.
- Treat existing AI-generated documentation as unverified. Report conflicting old rules to the user before adopting them.
- DesktopStub should become a source/plugin engine: independent data refresh and content cycling, tile text versus text baked into the image, multiple content entries with background and text layers. Live wallpaper detection is a source too.
- Candidate integrations: RSS, Discord rich presence preview/status, ASUS/caps indicators, SMTC, notes, custom image/text.
- Warn before enabling registration-mode cycling about CPU/registration cost. Warn about potentially skipped fast live-tile cycles; do not present an unverified 10-second threshold as an OS guarantee.
- Expand portable logic/stress tests and actual Windows rendering/UI smoke. Do not claim portable tests validate the Windows shell.
- Confirmed clarification: remove standalone apps **once integrated**, not before. Overlay means **inside the tile image**, not a separate desktop widget.
- Prefer DesktopStub's native C++ toolchain where practical. Do not silently remove features to achieve language uniformity.
- Check account usage frequently and immediately before tests that can leave persistent or interactive processes/system changes. Tests must not depend on this agent returning to restore laptop usability after interruption. Prefer isolated rendering/menus and inert fixture processes; default smoke must not register real packages or manipulate Explorer/hardware/services.

## Baseline and ownership

- Branch: `main`; origin is configured as `https://github.com/Antonomasia3rd/AIProjects` despite the reported disconnection. No fetch, push, reset or commit requested.
- Initial tree has many modified/untracked files, including prior shared tray/startup and tile-layout work. Git diff is not a clean measure of changes made in this session.
- Root: DesktopStub composition/source architecture, overall documentation.
- Shared audit: shared/non-DesktopStub configuration/startup/tray correctness; [details](audit-shared.md).
- Tile audit: layout/rendering and smoke tests; [details](audit-tile-layout.md).
- Repository audit: build/test portability, language/architecture inventory, documentation conflicts; [details](audit-repository.md).

## Confirmed initial findings

1. DesktopStub's `Settings.ContentSource` chooses one worker at startup. RSS skips wallpaper startup generation and runs `RssPollThread` instead of `PollThread`. It is an exclusive mode, not layered sources; changing it requires restart.
2. RSS directly sends notification XML, bypassing the existing image/text generation path. Its comment explicitly limits it to Windows 10+, making it a separate implementation inside the same executable.
3. Moving implementation into app-named `.inc` files has reduced app entry files but does not by itself unify behavior or establish a plugin contract.
4. Current shared tray already includes an earlier hover fix. Its behavior and all consumers still need verification; do not attribute that pre-existing change to this session.

## Work status

In progress. Detailed findings, fixes, validation output, remaining gaps, and conflicting old documentation rules will be recorded here and in the linked files. Full integration is not complete merely because a portable model or a build succeeds.

### Resumed 2026-09-08

- User explicitly retired older conflicting documentation rules: use this conversation's rules. User may be AFK; continue routine implementation without reasking.
- Multiple usage-limit interruptions occurred. User explicitly requested resuming all agents each time; no reset credits were redeemed.
- Shared fixes/tests and renderer fixes/tests are recorded in the linked audit files. Renderer found disappearing high-scale badges/wide secondary text and unenforced line limits; 3,588 native checks now pass before the latest overlay addition.
- Root implementation on disk (integration validation pending): `dependencies/content_engine.h`, `DesktopStub/ga_content_state.inc`, `ga_content_runtime.inc`, `ga_content_tray.inc`. New `[Content]` + numbered `[Content.N]` configuration composes Wallpaper/LiveWallpaper/Image/None backgrounds and CustomText/RssFeed/CapsLock/SMTC text. Provider data is captured once per generation, independent of INI text. RSS uses an async worker; SMTC has a bounded native provider. Default legacy wallpaper path is retained; legacy RSS is mapped into composed headline entries and uses the existing rendering/delivery path.
- Current work: compile/test this integration, prevent stale/mixed snapshots and lifecycle races, enforce cancellation/bounds, update old RSS source-check assumptions and user docs, extend portable and binary smoke. Standalone apps have not been removed because action/widget feature parity is not yet complete.

### Verified integration progress

- First full DesktopStub host/broker build with composition succeeded. Final rebuild and binary smoke after the subsequent fixes are still pending.
- `tools/TestContentRuntime.cmd`: 23 real host/menu checks passed, including 20,000 immutable snapshot publications with four concurrent readers, direct INI `true` routing, tray toggle persistence, INI-relative image resolution and no duplicate native text in Overlay mode. This harness includes production code but never calls the normal app entry point or registers a package.
- `tools/TestContentEngine.cmd`: 282,068 behavioral assertions / 100,000 randomized schedule iterations passed under both MSVC and Clang. GDI+ renderer now passes 18,778 checks with explicit background retention in Overlay mode, plus 600 changing-text stress cycles with unchanged GDI/USER handles.
- Fixed integration review findings: INI boolean aliases now use the same parser for routing as validation; decreasing refresh interval resets old deadlines; leaving composition clears its cached frame and restores wallpaper; SMTC runs in an MTA worker even for `--once`; snapshots include RSS activation metadata; obsolete RSS XML publisher/poller removed.
- Shared native/managed INI dialect fixtures now exercise the same quoted/unquoted keys, paths and comments. Native/managed startup, Unicode and tooltip regression suites pass. See shared audit for legacy-file migration caveats.
- New shared async WinHTTP downloader has total request deadline, bounded response, query preservation/fragment removal and cancellation; localhost tests passed before final extra deadline case. No cross-thread close of synchronous WinHTTP handles is used.
- Root and DesktopStub READMEs have been updated to retire old conflicting policies and explain actual composition. Configuration examples and remaining feature gaps: [content engine guide](content-engine.md).

### Final validation of this integration pass

- DesktopStub host and packaged broker build: passed after HTTP/provider integration. Remaining compiler warnings originate in Windows SDK 19041 GDI+ headers.
- DesktopStub source contracts: 1,716 passed against the final sources. Portable scheduler/configuration: 282,068 assertions; geometry: 236; preset XML: 494. Native renderer: 18,778 checks and 600 stress cycles. Final actual host/menu harness: 23 checks and 20,000 concurrent snapshot updates passed.
- DesktopStub end-to-end Windows smoke: passed, including legacy static text geometry, new composed text rendering on medium/wide/large, changed-text cache invalidation, TextMode=Off image restoration, leaving composition, second-launch behavior and concurrent INI writes. The smoke uses a copied binary and a unique test package/profile and cleans them up.
- 13 other product builds and selected binary smoke passed (repository audit). Shared native and managed runtime/INI/Startup suites passed. SMTC: 13 checks. HTTP: 15 localhost checks, including total deadline, delayed headers, exact/oversized responses, cancellation and late-callback state cleanup.
- Portable cases were executed using native MSVC and Windows Clang. CMake and an actual Linux/macOS runtime were unavailable locally; CI now defines Ubuntu portable tests. No WSL distribution was installed. Native Windows 8/8.1/10 shell visual parity, live tile animation timing, hardware LED control and Explorer deskband behavior remain target-host tests, not claimed passes.

### Still open (do not mark the full repository rework complete)

1. Integrate Discord transport and preview/status, ASUS controls, physical Caps LED blinking, notes/account retrieval and full NowPlayingTile artwork/widget behavior into the host. Current SMTC/CapsLock providers are read-only subsets; retain standalone apps until parity.
2. Replace remaining product-global `.inc` applications with actual reusable services/providers where appropriate. Manifest registration still has product-specific variants; no claim that every app is now only variables/includes.
3. Reconcile remaining hosted services/deskband/mod configuration surfaces. ADBController tray/startup and DesktopStub's separate packaged StartupTask option have since been implemented; see the continuation results below.
4. Resolve existing mandatory DPAPI/path enforcement defaults only after functional consolidation, with compatibility migration. Today’s opt-in-security direction is documented; the old mandatory code has not silently been removed.
5. Review full native language migration for managed apps without losing functionality. This pass standardizes the native deskband compiler and shared INI behavior; it does not rewrite every C# program.
6. Verify target Windows presentation/branding/RTL behavior with the maintainer's OS setups. Choice menus, source checkboxes, entry reordering and per-content feed item selection have since been implemented.

No commit, push, standalone-app removal, reset-credit redemption or user configuration migration was performed by the root task. Existing uncommitted work was preserved. This record distinguishes the verified integration from the remaining requested consolidation.

### Continuation after user smoke-safety clarification

- User explicitly authorized changing pre-conversation uncommitted work to meet the rules. The earlier preservation wording does not freeze implementations.
- Checked account usage at continuation start: 0% five-hour used; 31% weekly used. Do not redeem reset credits without explicit authorization.
- Smoke safety agent: default no-system-integration smoke, legitimate render-only workflow, autonomous child cleanup; [progress](audit-smoke-safety.md).
- Shared agent: actual ADBController tray/startup/config parity using shared dependencies, with inert tests; [progress](audit-adb-controller.md).
- Tile agent: separate packaged StartupTask option and bounded shared API calls, no real package/startup mutation; [progress](audit-packaged-startup.md).
- Root: content configuration usability and source scheduling, integration and remaining consolidation. Avoid overlapping common ga_* edits without coordinating. Read-only checks/builds are safe; real package smoke is paused until it can complete/recover without the agent.

### 2026-09-09 continuation checkpoint

- User authorized closing DesktopStub and other applications if needed. Normal instance PID34424 was observed running the real build executable without arguments; no agent launched it or stopped it. Validation should use separate binary names unless closing it becomes necessary.
- Smoke runner job-object interruption tests passed with inert child/grandchild fixtures. Default render-only routing/host early entry are now being compiled, not yet declared fully verified; consult smoke safety audit.
- ADBController actual tray/startup integration and DesktopStub packaged startup adapter/hooks are in progress under their agents; consult individual ledgers before resuming edits.
- Root added portable choice descriptors, per-entry `RssItem=1..20`, and warning-on-risk-change logic in `dependencies/content_engine.h`. Runtime reads the selected item independent of entry index and reports unavailable indices rather than silently choosing a different story.
- Reworked `ga_content_tray.inc`: background/text-mode submenus, source checkboxes with optional layer-order editing, file chooser with atomic image/path save, add-content and move-earlier/later actions. Entry command stride is now64. Moves rename full INI sections to preserve unknown fields/comments. A menu edit commits from one file snapshot and refuses to overwrite concurrent edits. Routine text edits do not repeat unchanged cycle warnings.
- Added UI harness assertions for source checkbox round trips, enum persistence, reordering and RSS selection. Content message boxes are intercepted by the inert harness so a regression cannot leave a modal dialog on the user's desktop. These new harness changes are not yet compiled as of this checkpoint.
- Latest usage check:89% five-hour /61%weekly used. All agents instructed to save state and avoid new system/resident/hardware tests. No credits redeemed.

### 2026-09-09 resumed validation

- Root's updated portable content suite passed:282,074 assertions /100,000 randomized iterations. The revised actual host/menu harness passed33 checks, including source checkboxes, delivery choices, whole-section reordering and typed RSS item persistence. New dialogs are intercepted by tests; no user-facing prompt can strand this harness.
- Root added separate legacy preset labels/checkmarks and a pre-enable cost/timing warning for the RSS preset. Updated content guide describes the new menus and per-entry RSS headline selection.
- Smoke agent built DesktopStubValidation.exe plus separate broker successfully. The bounded default smoke is executing only help/version, configure-only, render-only and local manifest generation against a copied temporary validation executable. It does not call the normal DesktopStub entry path. Wait for agent's completion record; no package-integration opt-in was invoked.
- Current usage checkpoint65%five-hour75%weekly; agent subsequently reported73%/77%. Current work is limited to completing already-written features/tests and recording results before another possible reset.

### Completed smoke-safety and configuration work

- Default DesktopStub smoke now passed entirely offline, against freshly built DesktopStubValidation.exe/broker copied into its temp directory. Modes `--render-only` and `--configure-only` enter before resident signaling, startup reconciliation, provider access and package operations. Normal package/resident tests require an explicit separate opt-in that was not used. The prior concern that a quota interruption could leave a package behind is removed from the default suite.
- RepoTools establishes a kill-on-close Windows Job Object before child creation, bounds waits and enforces a15-minute test deadline. Inert normal-exit and forced-termination regressions confirmed child/grandchild termination. Default Discord resident IPC testing is also gated out because it could contact a running Discord. Diagnostic temp files may remain after interruption; no agent-dependent cleanup is required for default smoke.
- Final combined DesktopStub source checker passed1,726 checks after updating obsolete source contracts. Fresh validation host/broker compiled. The 33-check real host/menu harness and282,074-assertion portable model suite passed independently. Project map validation passed for14 projects and `git diff --check` is clean.
- Packaged startup now has independent `RunAtStartupPackaged`, CLI aliases, clearly named tray control, cached actual-state/error status and an asynchronous worker. New Windows10 manifests declare it disabled; Windows8/81 remain unchanged. Custom INI profiles cannot accidentally mutate a package-wide task that launches the default profile. Twelve fake API/manifest tests and host compilation passed; actual OS StartupTask mutation was deliberately not tested.
- ADBController now has shared tray/startup/config behavior:20 inert checks,46 source checks and a warning-as-error executable build passed. Default builds compile only; the optional old binary-smoke runner no longer runs automatically. CI runs the inert harness and build-only target, and the project map includes its shared startup dependency. Hardware/Explorer presentation remain untested.
- Quota was checked throughout; latest root checkpoint54%five-hour90%weekly consumed. No reset credits used. No normal DesktopStub instance, real Startup entry, package, ADB device or hardware state was modified by this continuation's tests.

### Next integration stage

- The smoke-isolation, ADBController and packaged-startup subtasks are complete at their documented verified checkpoints. No new code in those areas is planned except concrete integration defects.
- Latest account read showed31%five-hour5%weekly used after an external account reset; root did not call credit redemption. Continue checking often and before tests.
- Remaining core objective continues: move reusable Discord/notes functionality into source providers used by DesktopStub, preserve standalone apps until feature parity, and complete shared architecture rather than treating relocated app bodies as finished consolidation. Tests remain offline/inert by default.

### Shared source lifecycle checkpoint

- Root added platform-neutral `dependencies/content_source_host.h`: one worker and latest pending request per source, immutable cached results tagged by profile key, cancellation on profile change/disable/shutdown, fetch-exception reporting, deadline-bounded caller waits and coalesced polling. Fetchers still own bounded I/O and must honor cancellation; synchronous OS calls are not force-terminated.
- DesktopStub RSS/SMTC wrappers now use this shared host. RSS propagates the cancellation predicate into `http_fetch`; disabling a source cancels its work. Invalid RSS configuration messages are included in the request key so a different invalid edit does not leave an old diagnostic cached for15minutes.
- Added `tools/ContentSourceHostTests.cpp` and standalone runner/CMake target. Tests use only synthetic bounded callbacks, no OS integration or network. They cover10,000 cache polls, force/interval refresh, stale-result suppression, failure/recovery and cancellation/join. MSVC run passed10,020 checks (`build/content-source-host-tests.log`); host integration compile remains pending.
- Notes extraction API is approved and documented in `docs/audit-notes-source.md`; agent owns native data engine + deskband reuse. Discord embedded API/refactor is under its agent; provider wiring waits for a concrete interface. AppX sharing agent owns shared typed registration script + both call sites, NPT captured runner and startup fragment reuse.
- Latest usage checkpoint75%five-hour29%weekly. All current tests are bounded/offline; no package or hardware state is involved.

### Notes/Discord integration checkpoint, 2026-09-10

- Shared Notes extraction is complete: `content_sources/notes.inc` used by the deskband and DesktopStub. Forty synthetic response/fake-transport checks (2,000 concurrent parses), deskband compile/link and67 source checks passed. HTTP header pairs were added for Cookie/DS; no actual cookies/accounts/network were used in tests. Existing endpoint/region assumptions are preserved but live compatibility unverified.
- Shared Discord service is complete as a library and consumed by standalone DiscordRPC: service API/TU compile, standalone build,131 fake-transport checks with40,000 snapshot reads and186 source checks passed. DesktopStub now links that TU and hosts preview/sending in-process; sending defaults off. Optional external profile preserves advanced config; built-in profile exposes ClientId/Name/Details/State. Host UI/link integration is being verified.
- DesktopStub Notes schemas, game/account menus, credential redaction/masked entry, per-game async requests and explicit timeout statuses are implemented. Cookies are deliberately plaintext, documented clearly; no mandatory DPAPI/location policy was added. Credential fields are not echoed in validation errors or command-line logs.
- Shared source host review fixed concurrent Request/Stop and multiple-Stop races plus unsafe fixture lifetime ordering. Final10,103 synthetic checks passed. Wrappers now expose one-shot wait expiry instead of silently showing an old cache as fresh.
- First combined host runtime harness linked but crashed with access violation before producing results. Likely cross-translation-unit static initialization: globally constructed Discord Service touched its TU's logger/path globals. Root changed the host to construct the service lazily after process initialization. The isolated harness is recompiling/running in `build/integrated-sources-runtime.log`; do not claim host integration passed until that result is checked.
- DesktopStub build scripts and host-test runner compile/link `service.cpp` separately with Crypt32. CI/map include Notes tests/dependencies. Content guide documents Notes and Discord setup/limits. Root quota checkpoint38%five-hour93%weekly before this diagnostic run; no real services/accounts/hardware/package actions have run.

### Verified combined source integration

- Lazy host construction fixed the cross-translation-unit startup crash. The final combined host/menu harness passed41 checks, including constructing and starting the separately linked Discord service with synthetic profile/context and sending off, reading its Preview snapshot and stopping it by deadline. No real foreground data, account, Discord transport or network was used.
- Updated portable model passed282,081 assertions. Shared asynchronous source host passed10,103 synthetic checks. Fresh DesktopStubValidation host/broker build passed with Notes and the separately linked Discord service.
- A direct source-check attempt initially ran an obsolete September8 scanner executable and failed an already-updated smoke guard. File timestamps confirmed the stale executable; the full source suite is rebuilding the scanner rather than changing correct source to satisfy old assertions.
- Final offline binary smoke and rebuilt source suite are running under the existing bounded/inert test design. Logs: `build/integrated-sources-offline-smoke.log`, `build/integrated-source-suite.log`. Check completion before reporting these final two results.
- Latest allowance before those tests:67%five-hour97%weekly. No agents were spawned or resumed at that low remaining weekly allowance; all delegated source implementations were complete. No reset credit was redeemed by the agent.

### 2026-09-11 active work and safety follow-up

- The rebuilt full DesktopStub source suite passed1,727 guards,236 geometry checks,494 XML checks,18,778 GDI+ checks and41 host/menu checks. The final separate-process offline smoke failed a strict no-change comparison: four pixels in unchanged primary caption glyphs differed when only unsupported medium secondary text changed. Pixel/layout assertions have not been relaxed. Renderer investigation is ongoing in [its ledger](audit-render-determinism.md).
- Native ASUS and Caps blinking engines are being extracted/ported from the existing active code, with portable pattern logic and injected device backends. Only inactive/fake hardware tests are allowed. Their C# standalone apps remain until actual parity; see [ASUS inventory](audit-asus-source.md) and [Caps inventory](audit-caps-source.md).
- Found a second test-isolation gap: shared native/managed baseline runtime tests still installed real Startup links by default. Those final integration blocks now require explicit `--allow-startup-integration`; injected transaction tests and temporary-directory ShellLinks remain covered by default. Both safe default suites passed; native source guards passed301. See [details](audit-baseline-test-safety.md).
- Latest quota checkpoint48%five-hour26%weekly. User-redeemed credits are exhausted; the agent has redeemed none. Continue frequent checks and bounded/inert validation.

### 2026-09-11 hardware-stage checkpoint

- Root added `CapsBlink` to the portable content schema using the actual portable `aip::caps::Config`/validator: `HardwareEnabled=0`, `KeyboardTargetPath=\\Device\\KeyboardClass0`, `BlinkIntervalMs=500` (50..86400000). This is pending host/backend/UI wiring and validation; do not claim that selecting this newly declared source yet drives hardware.
- Caps agent has written `dependencies/hardware/caps_blink_engine.h` plus the Windows backend file, retaining logical/current LED restoration and old target ownership identity. ASUS agent has written portable pattern/service/protocol/native-backend files and a legacy settings-map parser. Both agents still own fake tests/adapter validation; root awaits stable final APIs before wiring actions.
- Renderer investigation reproduced neither the original four-point glyph variation with the exact cached generation sequence nor with DPI/rounding/hint/contrast and full multi-size warmup probes. Assertions remain exact. Agent is retaining diagnostics and improving failure wording without claiming a cause or applying an unsupported rasterization fix; root will rerun the full offline smoke when the next host build is stable.
- Safe native/managed baseline defaults were executed successfully, including301 native source checks. A filtered read-only audit of the per-user Startup folder found no remaining baseline-test shortcuts. No Startup artifact needed removal.
- Latest allowance95%five-hour51%weekly; zero reset credits. No new persistent/system/hardware test started near the limit. All work and remaining validation are recorded on disk.

### SAVE/RESUME CHECKPOINT — 2026-09-12

User explicitly requested saving work because the weekly allowance is nearly exhausted. Latest tool read:1%five-hour used,89%weekly used,zero reset credits. Root started no new tests or hardware operations; errored agents were asked only to finalize their saved ledgers. Existing user applications are not stopped for this checkpoint.

Validated engine work now on disk:

- Caps engine/backend:25 fake checks,131,072 pattern combinations,50 lifecycle stress cycles and Windows adapter syntax compilation passed. Pending canceled I/O retains ownership on the original worker until completion; restoration follows completion, then releases the target. `cleanupPending` remains observable. Actual driver/keyboard behavior is untested.
- ASUS engine/backend:287,638 Clang checks including100,000 randomized steps; MSVC warning-as-error service/fixture and native compile passed. No native backend was linked into the fake test executable. Legacy parser behavior, pause/duration/priority/HDD mapping and source-only error handling are recorded in its ledger.
- Renderer investigation:18,826 checks passed, no GDI/USER handle growth. Four anomalous primary-caption pixels from one offline run remain unexplained/unreproduced after controlled probes. No renderer change or pixel tolerance was introduced. Full offline smoke must be rerun after host integration stabilizes, with exact comparisons retained.

**Root hardware host integration is incomplete and has NOT been built/tested yet.** Current edits include:

1. `dependencies/content_engine.h` now declares CapsBlink and AsusBlink sources, typed Caps settings, and ASUS inline/profile configuration using the native parser.
2. `ga_content_runtime.inc` has new lazy ContentCapsProvider/ContentAsusProvider wrappers and logical-Caps UI-query callback. `ga_content_state.inc` declares the message/cache; `ga_app.inc` handles the query and captures state on shutdown requests.
3. These wrappers still need wiring into ContentEngineTick's needed-source discovery, refresh/text composition, disable and shutdown paths. They are not yet called there.
4. Add corresponding INI defaults/template sections, menu controls (hardware flags default0), and inert tests. The source-command range now reaches offset31 because there are8 source choices; change the old unused-ID test from31 to an actually unused offset such as39.
5. Link ASUS `service.cpp` and `native_backend.cpp` plus `pdh.lib` in the product build. For ContentRuntimeTests, compile ASUS service with `AIP_ASUS_NO_NATIVE_BACKEND` and define `DESKTOPSTUB_INERT_HARDWARE` before including DesktopStub so no regression can instantiate a real Caps backend. Current build scripts only link Discord's service, so the new ASUS wrapper references can produce unresolved symbols until this is done.
6. Keep Caps Update(false)/Stop joins off the active UI/poll path while `Snapshot.running`/cleanupPending is true; RequestStop is nonblocking. An unresponsive driver can delay final shutdown; never release ownership while a late write could still occur. Review shutdown logical-state caching and expose pending cleanup honestly.
7. Rebuild the portable model, fake host/runtime harness, source guards and separately named DesktopStubValidation binaries. Use only default offline smoke; no real package, account, hardware or Startup integration test. The normal user's DesktopStub build may remain open; user authorized closing applications if required, but separate validation binaries avoid it.

All earlier shared/notes/Discord/startup/AppX implementations and validation records remain in this file and linked ledgers. All pre-conversation uncommitted work is editable under the user's rules. No commits/pushes or agent credit redemptions occurred. Remaining broad objectives (full standalone retirement, hosted utility parity, security opt-in migration, remaining managed-language migration) are not marked complete.

### 2026-09-19 solo continuation

- User explicitly prohibited starting/resuming subagents until they report the five-hour allowance is back to100%. Root complied; no collaboration calls were made. Initial usage73%five-hour/11%weekly; pre-test checkpoint91%/14%.
- Wired CapsBlink/AsusBlink into source discovery, refresh, tile text, disable and shutdown. Invalid configuration cancels previously enabled actuators/Discord sending; invalid ASUS profile reload stops its earlier pattern. Hardware actions still default off, and disabled sources request cancellation.
- Added corresponding INI defaults/template sections and tray controls, including explicit hardware flags, ASUS disk-read/pause/basic pattern/profile settings and atomic restart requests. The old unused menu-ID test now uses39 because the expanded eight-source catalog uses31.
- Product build now compiles ASUS service/native backend separately and links PDH. The host test defines DESKTOPSTUB_INERT_HARDWARE and compiles ASUS service with AIP_ASUS_NO_NATIVE_BACKEND: real Caps/ASUS backend access is excluded from its executable. Added linked-host preview checks for both engines.
- Current validation command is `tools/TestContentRuntime.cmd`, log `build/hardware-host-runtime.log`. Check result before claiming this hardware host integration compiles/passes. No actual hardware, Startup, package or normal application test is authorized by this validation.
- Remaining after this run: resolve compile/test failures, add focused typed-model hardware cases and user-facing setup documentation, check source guards, build separately named validation binaries, rerun the bounded offline smoke with exact raster comparisons (the earlier four-pixel discrepancy remains unresolved/unreproduced). Preserve standalone hardware apps until parity is demonstrated. Do not resume subagents without the user's explicit allowance-restored message.

### 2026-09-20 integration closeout in progress

- User subsequently explicitly allowed subagents again; current resumed allowance0%five-hour34%weekly. Root resumed only the unfinished lifecycle review and integration validation.
- Solo hardware host harness passed43 inert runtime/menu checks, excluding native Caps/ASUS backends. Portable model now passes282,126 assertions, covering hardware defaults, normalized INI/CLI parity, invalid target/interval/state and atomic failed reload. Hardware settings/setup/limits are now documented in `docs/content-engine.md`.
- Lifecycle review confirmed quick disable/reenable can leave an ASUS service stopped behind a cached desired key. Shared audit owns a small explicit pending-stop/cache reset fix and inert regressions in `ga_content_runtime.inc`/ContentRuntimeTests; Discord stop/reload races and direct invalid-Caps cancellation are included. It is not yet reported complete.
- Repo validation has already fixed the missing hardware dependency declaration and portable Caps test routing. It waits for those wrapper edits, then compiles fresh separately named binaries and runs source/offline smoke. Metadata checks passed14 projects and All. No normal app, driver, keyboard, account, Startup or package tests are planned.

### Source presentation and cancellation follow-up

- Root found that nonnumeric Preview/Error/Control/SMTC labels were placed in the badge field. The medium preset treats any badge as a large counter and omits its secondary region, hiding presence state, media metadata and hardware errors. Discord/ASUS adapters and SMTC now keep those labels in ordinary text, leaving the badge empty. Shared audit owns matching Caps/generic-error wrapper presentation and host-test assertions. Root updated pure service adapter expectations; no pixel tolerance or OS rendering rule changed.
- Review of embedded Discord ActualTransport confirmed its mutable cancellation callback could be read by Gateway background/cleanup threads while reassigned, and was destroyed before clients that referenced it. Repo validation is replacing it with an operation-scoped shared relay of immutable callbacks, with inert concurrent/lifetime/timeout regressions. Completed-operation deadlines must not poison idle/later operations.
- New hardware/model setup documentation is saved in `docs/content-engine.md`. Runtime wrapper fixes and the fresh build/smoke remain in progress; consult individual ledgers rather than treating this checkpoint as a completed repository migration.

### Defender quarantine — 2026-09-21

- Windows Defender detected `Trojan:Win32/Bearfoos.A!ml` in the fresh
  `DesktopStubValidation.exe` and its temporary offline-smoke copy, then
  quarantined both. The threat record reports no execution and no active
  remaining instance. All binary builds, smoke runs, restoration, exclusions,
  allow actions, and external submissions are paused.
- A source-only review found the host combines AppX registration/PowerShell,
  relaunch/activation, network providers, Discord, raw keyboard I/O and ASUS
  ACPI access. That can affect generic ML classification but does not establish
  a false positive or identify a single cause. Full evidence and operating
  rules are in [the Defender incident record](audit-defender-detection.md).
- The review independently hardened legacy Live Tile helper registration:
  manifest/package values now use the shared literal PowerShell builder on all
  registration paths. This is source-only verified pending a safe external or
  clean-environment binary review.

### Account-expiry handoff and stopped work — 2026-09-20

- User requested stopping tasks, cleaning temporary/permanent task artifacts and Codex memory, and a Desktop handoff. They explicitly chose to keep the edited project source. Development/validation stopped; both unfinished subagents were interrupted, and no task-created test processes remained at the final check.
- Handoff: `C:\Users\Amiya\Desktop\AIProjects-Handoff-2026-09-20.md`, with companion directory containing tracked patch, new-source snapshot, base/status, logs/evidence and precise pending work. Source remains in this workspace. Latest wrapper/presentation/shutdown-sampling edits are saved but not yet fully validated; the Desktop handoff lists the required first checks.
- Cleanup is NOT complete. Automatic approval review rejected the bulk cleanup, a narrowed single MEMORY.md deletion, and a narrowed single test-executable deletion with only "blocked by policy". None of those commands executed. Memory and temporary/build artifacts remain. No workaround was used to bypass the rejection.
- `CLEANUP-REQUIRED.md` and cleanup-plan/results files in the Desktop handoff identify manual actions. A broad untracked snapshot accidentally included the unrelated LockScreenWin10 directory; its original was untouched, and its handoff-only duplicate is explicitly listed for removal (also blocked). Codex memory contents were not copied into the handoff. App sign-out/history purging and secure storage erasure were not performed or claimed.
- Resume only on a later explicit user request; do not launch tests or source actuators automatically from this stopped checkpoint. No commit/push or reset credit redemption occurred.
