# Final hardware-source host integration validation

Scope: compile a fresh separately named DesktopStub host and broker with the native ASUS service/backend, Discord service and shared caps engine; run only source checks and the bounded default **offline** smoke. No normal application launch, package registration, ATKACPI, PDH, keyboard LED, audio or external Discord operation is part of this validation.

Ownership: repository validation owns build/test infrastructure and this evidence record. Root owns host UI/model changes. Shared audit is reviewing runtime wrappers and the inert host harness; compilation waits for that agent's ready signal.

Initial inspection (2026-09-19):

- `BuildDesktopStub.cmd` compiles Discord service, ASUS service and ASUS native backend as separate translation units and links the host with `crypt32.lib` and `pdh.lib` alongside its existing libraries.
- `TestContentRuntime.cmd` compiles ASUS with `AIP_ASUS_NO_NATIVE_BACKEND`; the shared agent owns that harness run.
- CMake already links `Threads::Threads` for ContentSourceHostTests and AsusEngineTests. Current ContentEngineTests has no thread use needing an additional thread target.
- The DesktopStub project-map entry was missing an explicit `dependencies/hardware` declaration; this will be added before validation.
- RepoTools still installs its kill-on-close job/deadline and routes default DesktopStub smoke through offline entry points. Explicit validation binary/broker overrides will prevent testing stale release-map binaries.
- Initial usage check: 23% five-hour, 20% weekly consumed. Limits will be checked again before each test stage.

Results are pending. Pixel comparisons will remain exact; any recurrence of the earlier four-pixel discrepancy will be preserved as a failure with its fixture path, without weakening the assertion or claiming an unverified cause.

Preparation results:

- Project map validation passed for 14 projects; workflow selector passed for 14 projects plus All. Logs: `build/hardware-host-project-map.log` and `build/hardware-host-workflow.log`.
- Added the existing portable CapsBlinkEngineTests to CMake with `Threads::Threads`, and the existing inert Caps test script to Windows CI. Its device backend is fake; Windows device code in that script is syntax-checked only.
- Shared audit reported a concrete pending-stop/re-enable wrapper bug and is updating `ga_content_runtime.inc` plus the inert host harness. The fresh host build is deliberately waiting for those edits to settle.
- The newly wired portable CapsBlinkEngineTests compiled and passed under Clang with fake device I/O only (`build/hardware-host-caps-portable.log`). No repeated run is needed for this infrastructure change.
- Resumed stage on 2026-09-20: usage 16% five-hour and 36% weekly consumed. Metadata validation remains complete; source checks and the fresh host/offline smoke await the shared audit ready signal.
- While awaiting wrapper readiness, root identified and authorized a concrete Discord callback race/lifetime review. It was confirmed: ActualTransport reassigned a callback read by Gateway background threads, and member destruction discarded that callback before Gateway cleanup. A narrowly scoped cancellation relay now publishes immutable callback objects through atomic shared pointers, and callbacks retain relay state instead of a raw transport pointer. Send/Clear operation scopes clear their deadline on completion so an expired completed operation cannot cancel an otherwise idle Gateway. Fixture-only concurrent/destruction/deadline cases were added; no real transport is exercised.
- Discord source guards passed **190 checks** after the relay fix (`build/hardware-host-discord-source.log`). The relay fixture batch passed **140 checks, 40,000 snapshot reads and 80,000 relay reads**, with root's updated text-adapter expectations (`build/hardware-host-discord-relay.log`); usage immediately before it was 77% five-hour, 63% weekly consumed. This fixture uses no Gateway connection or token file. Fresh host compilation still awaits shared audit readiness.
