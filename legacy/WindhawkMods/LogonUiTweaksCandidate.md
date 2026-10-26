# LogonUI Tweaks alpha5g workspace candidate

This is a reviewable source candidate based on the installed alpha5f file,
SHA-256 `3F8F6E78E9EFFD2F3E058BDF8E291115A18FA59F0FAEB6BBD25D336913377915`.
The installed file under `C:\ProgramData\Windhawk\ModsSource` has not been changed.

**Not ready to install or hot-reload.** The inherited XAML callback lifetime
finding remains open. This candidate contains two audited fixes, not the completed
Back restoration or a Windows 8/x86/ARM64 port.

The source checkpoint includes this candidate, build/test scripts, the harness,
and summary documentation. Links into `analysis/` below are private, untracked
local evidence and generated artifacts, preserved separately in this checkout.
They are not bundled with the commit. Build/test scripts regenerate their own
outputs when the required compiler and mounted reference images are available.

## Changes

- SystemProvider hook resolution always uses the actual loaded image's unique
  structural result, with the existing symbol fallback. OS build/UBR can no longer
  override contrary image evidence or bypass validation when logging is disabled.
- The shared PE view rejects non-AMD64 images, matching its x64 instruction decoder.
- If hook-worker creation fails, initialization signals the shared stop event,
  joins and closes the battery watcher, unregisters its private window class, and
  keeps the stop event alive until the started workers have exited.
- Battery-window creation failure also releases its successfully registered class.

## Inert validation

Run `TestLogonUiTweaks.ps1` with the installed Windhawk compiler. It checks the
complete x64 source and compiles a small standalone harness from excerpts of the
actual production resolver and initialization routine. The harness does not load
the complete mod or invoke Windhawk.

The startup cases replace every Windows/feature side effect with mocks and verify
the signal/join/close ordering, event lifetime, class cleanup, event-creation
failure, and successful startup. The mock rejects closing worker handles before
joining them. There are 47 assertions in the current startup suite.

The resolver cases read the mounted F:/E:/K: `Windows.UI.Logon.dll` files into
ordinary data buffers. Their expected targets were independently named by the
corresponding public PDBs:

| Input | Expected `_OnLoaded` RVA |
|---|---:|
| F: Windows 10 1809 | `0x49760` |
| E: Windows 10 22H2 | `0x472D0` |
| K: Windows 11 25H2 | `0x1B630` |

Each case also requires rejection after changing the PE architecture, altering
the handler prefix, removing unwind metadata, and introducing a second matching
candidate. Mutations occur only in private buffers. Images, VM drives, sessions,
power, audio, brightness, and the running mod remain untouched.

These three images are the verified structural-test coverage. The old host
26200.6584 entry, x86, ARM64, and other builds are not covered by this test run.
Public-PDB GUID/age qualifications are recorded in the binary evidence reports.

The source's embedded README retains prior alpha5f runtime-test and hot-load
stress-test history. Those inherited statements are provenance, not runtime
acceptance of alpha5g. This candidate's validation is the x64 compilation and
bounded inert checks listed here.

`BuildLogonUiTweaks.ps1` performs an x64 DLL build using the installed engine's
import library. It writes the candidate, log, and source/output-hash receipt to
the analysis directory. It never copies the DLL into Windhawk or loads it.

## Open work

- The P1 callback-lifetime issue spans user-list and system-control event
  revocation, actual registration-owner threads, queued delegates, dispatcher
  fences, and failed initialization. A timeout or a module pin alone is not a fix.
  See [the lifecycle design](../../analysis/logonui-back-2026-10-26/lifecycle/design-and-candidate-review.md).
- The native Back investigation distinguishes ordinary async cancellation,
  explicit cancellation errors, and Windows' return-to-lock intent. An additional
  arrow or a generic disconnect/cancel operation would not establish native parity.
- The requested Windows 8+ x86/x64 and supported-ARM64 range still requires
  per-family implementations and behavioral validation.

See [the main audit](../../docs/logonui-back-button-audit.md) and the controller,
producer, and publisher evidence under `analysis/logonui-back-2026-10-26`.
