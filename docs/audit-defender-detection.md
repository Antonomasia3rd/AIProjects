# Defender detection on DesktopStub validation build

Observed: 2026-09-21. This record is an incident note, not a claim that the
file is safe or a request to bypass Microsoft Defender.

## Facts observed locally

- Microsoft Defender detected `Trojan:Win32/Bearfoos.A!ml` (Threat ID
  `2147731250`, severity 5) in the separately named
  `DesktopStubValidation.exe` and in the copied offline-smoke executable.
- Defender quarantined both files successfully. `Get-MpThreat` reported
  `DidThreatExecute: false` and `IsActive: false` afterward.
- The normal pre-existing `DesktopStub.exe` was not listed as a detected
  resource. That does not establish that it is safe; it is an older binary and
  must not be used as a substitute for a review.
- No exclusion, restoration, "Allow on device", sample submission, package
  registration, hardware interaction, or process launch was performed in
  response to the detection.

## Static review

DesktopStub is a broad native host that currently links several capabilities:

| Capability | Source area | Default activation |
| --- | --- | --- |
| Loose AppX registration and PowerShell fallback | `ga_registration.inc`, `ga_live_tile.inc` | User-selected registration/compatibility paths |
| AppX activation and relaunch helpers | `ga_app.inc`, `ga_live_tile.inc` | Package/compatibility paths |
| HTTP feed and Notes requests | `http_fetch.inc`, content sources | Source-specific, disabled unless configured |
| Discord transport | `dependencies/DiscordRPC/service.cpp` | Presence sending defaults off |
| Raw keyboard indicator I/O | `caps_blink_windows.inc` | `CapsBlink.HardwareEnabled=0` |
| ASUS ACPI I/O and disk sampling | `asusblink/native_backend.cpp` | Both hardware and disk sampling default off |

The combined feature set can plausibly influence a generic machine-learning
classification. It is not evidence that any one component caused the result,
nor evidence that the result is a false positive.

The review found a concrete independent flaw: three legacy Live Tile helper
paths interpolated manifest/package values into double-quoted PowerShell
strings. They now use `BuildAppxRegistrationScript` and
`PowerShellSingleQuotedString`, matching the main registration path. This
prevents `$`, backticks, quotes, and spaces in paths/package names from changing
the PowerShell command. Source-level checks verify those paths; the affected
binary is not rebuilt on this machine while the detection remains unresolved.

## Source-only provider boundary

The 2026-09-21 continuation moves the CapsBlink and AsusBlink host wrappers
behind `DESKTOPSTUB_ENABLE_HARDWARE_SOURCES`. The default remains `1` so the
existing DesktopStub build behavior is unchanged. Setting it to `0` makes the
build script omit the ASUS service, native ACPI backend, and PDH import, while
the host reports that a selected hardware source was omitted. Configuration
cannot silently re-enable code that was excluded at build time.

This is a source-architecture and least-capability boundary. It does not
identify the detection cause, establish that either build is safe, or make a
false-positive claim. An inert no-hardware host test is now staged for a later
clean environment, but no binary was rebuilt or executed to validate this change.

## Current operating rule

- On 2026-09-30 the user explicitly authorized pushing the review branch and
  resuming tests step-by-step, with Defender scanning before execution where
  possible. Fresh builds and bounded inert tests are therefore permitted.
- Scan new test/product outputs before execution. A detection, failed scan,
  or missing/changed output blocks that stage. Do not restore old quarantined
  files, add exclusions, or use Allow on device.
- Package, Startup, account/network, real hardware and resident integrations
  remain outside the default test path. A scan without detections is not proof
  that a binary is safe or that the earlier detection was a false positive.
- Do not submit a binary to Microsoft without the user's specific approval:
  submissions disclose the file externally.

Microsoft provides a developer workflow to submit a suspected false positive
for analysis: [Submit files for analysis](https://learn.microsoft.com/en-us/unified-secops/submission-guide).
Microsoft also documents false-positive handling for endpoint detections:
[Address false positives and false negatives](https://learn.microsoft.com/en-us/defender-endpoint/defender-endpoint-false-positives-negatives).

Custom file scans use `MpCmdRun -Scan -ScanType 3 -File <path>` without launching
the target. Remediation remains enabled. Exit 0 alone is insufficient because
it can mean successful remediation; the validation gate also checks fresh
detection records, file presence and an unchanged SHA-256. See [Microsoft's
command-line documentation](https://learn.microsoft.com/en-us/defender-endpoint/command-line-arguments-microsoft-defender-antivirus).

## Remaining engineering decision

### Renewed detection on 2026-10-03

The user-authorized staged run built a fresh full validation host/broker and
scanned both without executing them; each custom scan reported no threats.
After the smoke copy completed its initial offline help/configuration commands,
Defender quarantined that copy as `Trojan:Win32/Bearfoos.A!ml` again. Remediation
succeeded, `IsActive=false`, and no test processes survived the failed runner.
The record's `DidThreatExecute=false` does not negate the preceding successful
test commands. See [the precise validation record](VALIDATION_2026-09-30.md).

Execution of the affected host is paused again. The remaining original build
must not substitute for the quarantined copy. Pre-execution scans are useful
but did not prevent this later detection, and the result has not been
established as a false positive.

A [local submission brief](defender-submission-2026-10-03.md) now records the
sample identity and event evidence without account files. The remaining build
also became unreadable with Windows' virus/PUA error, so its recorded hash has
not been freshly verified. The [source audit](audit-offline-startup-2026-10-03.md)
does not establish the detection cause. An [isolated validation plan](isolated-validation-plan.md)
is prepared; no upload, VM setup, restoration or protection change has occurred.

The optional hardware build boundary is now in place. A future separate-process
provider model, or a split for the optional package-registration helpers, would
still be a larger installation and user-experience decision. It must not be
framed as antivirus evasion; its purpose would be least capability and clearer
trust boundaries.

### User-requested detection investigation and topology check — 2026-10-03

The user prioritized resolving this detection and allowed compiled source DLLs
only if needed; the preference remains the current portable layout. The
[topology map](DESKTOPSTUB_TOPOLOGY.md) confirms that providers are compiled into
the host, with no external source DLL interface. A separate packaged broker
already exists. The compatibility AppX stub is currently a copy of the host.
These facts do not identify which code caused the classification.

The old original validation file was also quarantined successfully, with
initial detection at 06:52:40 and final status at 06:53:30 +07:00. Its path was
absent at the start of this continuation. Defender's normal signature update
advanced from 1.459.512.0 to 1.459.523.0. One normal, full-featured comparison
build from `53d9e32` was then compiled for non-executing analysis. Its custom
scan reported no threats, its hash remained unchanged, and static inspection
showed ordinary Windows imports. No source capability or DLL boundary changed.

The application was not executed. This scan does not establish that the prior
detection is fixed: the earlier file also passed a scan before quarantine, and
both the definitions and binary differ. The new hash, prepared external-review
text and limits are in [the comparison-build brief](defender-review-current-build.md).
No quarantined file was restored. No manual upload, exclusion or protection
disablement was performed; normal automatic sample-submission settings were
read and left unchanged, and their submission history has not been established.
