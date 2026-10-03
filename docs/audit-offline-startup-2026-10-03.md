# Source audit of the blocked offline startup path

This audit follows the production code represented by the recorded October 3
validation build. It is source inspection, not a malware determination or proof
of complete runtime behavior. See the [submission brief](defender-submission-2026-10-03.md)
and [test results](VALIDATION_2026-09-30.md).

## Path exercised before the block

`ga_app.inc::wWinMain` configures process DPI, gets the executable path, parses
arguments, resolves the selected sidecar INI path, and computes the instance
identity strings. Help/version exit early. `--configure-only` and `--render-only`
return through `RunRenderOnlyCommand` before normal instance registration,
tray/poll worker setup, package helpers, startup reconciliation, and providers.

`ga_render_only.inc` rejects startup mutations, resident control, activation
and helper options. It validates the prospective content settings. A
configure-only request creates/fills the selected INI and writes its requested
setting batch, then exits before manifest creation, rendering or publication.
The INI writer uses a profile-scoped mutex, a temporary file, and a replace move.
Defaults may contain descriptions of optional functionality; writing those
strings does not invoke that functionality.

The global RSS, SMTC and Notes provider objects hold lazy `AsyncSource` caches;
their default constructors do not start a worker. Workers start in `Request`.
Discord and hardware host wrappers hold initially null service/controller
pointers, with construction and startup in their refresh paths. Those refresh
paths are not called by the inspected configure-only branch. No automatic
account migration or driver access was found in these provider constructors.

## Capabilities compiled into the host

The full build links package APIs/PowerShell helpers, WinHTTP providers,
Discord, optional ASUS ACPI/PDH and raw keyboard indicator code. These are
intended resident features elsewhere in the program. Their presence alone
does not establish why Defender classified the build. The event log identifies
the runner process and quarantined file, not a particular function or instruction.

## Findings and limits

- The cached-INI test failures were fixture synchronization errors, fixed
  separately without changing production cache behavior.
- The smoke process helper treated a missing expected executable as a skipped
  success. It now throws with the missing path; a pure missing-file regression
  is also part of the existing interruption-test command. This prevents an
  executable removed after selection from silently skipping requested cases.
- Help/version in the recorded build encounter the optional parent-process wait before
  their early return when a wait PID is supplied. That can unnecessarily delay
  informational commands for up to 30 seconds. It was not used in the failed
  smoke sequence and is not attributed as the detection cause. The continuation
  moves that wait after informational/offline returns and adds bounded CLI
  regressions using the live runner PID. Runtime verification of this source
  change is deferred with the affected host; it is not a detection workaround.
- `EnsureInitialIniTemplate`, `EnsureIniDefaults`, and `EnsureIniStringDefaults`
  do not expose one combined success result to configure-only mode. In
  particular, a configure-only invocation with no setting batch needs a
  focused failed-write regression so it cannot report successful persistence
  when initialization fails. This remains an open functional audit item.
- The custom scan reported no threats; later normal file access is blocked.
  This evidence does not distinguish delayed/cloud/static classification from
  runtime behavioral detection. Do not claim one of those causes without
  further evidence from an isolated reproduction or Microsoft analysis.

The remaining original validation file was not executed during this review.
No quarantine restoration, protection changes, external submission, account
access or hardware operation occurred.
