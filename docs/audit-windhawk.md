# Windhawk and LockScreenWin10 audit

Reviewed 2026-09-30 from source-only inspection. No Windhawk module was compiled, installed, reloaded, or attached to LockApp.

## Source topology

`legacy/WindhawkMods/LockScreenWin10/sources/` intentionally preserves original upstream or user-provided inputs. `builds/` contains the generated candidate files intended for Windhawk; `tools/build.ps1` compiles files from `builds/`, and the project README instructs users to paste those files into Windhawk. Same-named files in the two directories differ by design, so they must not be deduplicated or moved without regenerating and reviewing the candidate.

The ignored `build/`, `analysis/crashes/`, `analysis/logs/`, `dumps/`, `reports/`, and `screenshots/` paths contain local generated evidence or binaries. They were observed but not deleted, staged, uploaded, or treated as source.

## Findings

- `tools/dumper_writer.inc` waits indefinitely for its file-writer thread during `DumpWriter::Close()`. If the writer blocks in a file operation, LockApp module unload can hang. Replacing this with a timeout requires a lifecycle redesign: the worker currently references `this`, so closing handles or destroying state after a timeout would create a use-after-free risk.
- `analysis/MainDlg.cpp` documents a deliberate 32-bit allocation leak used to transport 64-bit values through 32-bit UI fields. This is analysis tooling rather than a Windhawk module, but it must not be copied into a release candidate without ownership tracking.
- `legacy/WindhawkMods/local@always-uiaccess.wh.cpp` has similar infinite joins for its pipe and auto-topmost threads. It is a separate host module and needs its own shutdown-state redesign before a finite timeout can be introduced safely.
- Windhawk controls module loading and startup. These modules cannot directly inherit the resident app tray/startup contract; an interactive companion or a DesktopStub source adapter would be required to expose the same user-facing controls.

## Next safe work

1. Keep `builds/` as the only candidate build input and regenerate it from `sources/` plus the tracked preparation scripts.
2. Before changing shutdown waits, isolate writer state from the module object so a timed-out worker can retain ownership until it exits.
3. Validate candidate behavior only in a disposable LockApp/Windhawk environment after the DesktopStub Defender incident is separately resolved.

