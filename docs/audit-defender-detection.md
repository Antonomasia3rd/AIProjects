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

## Current operating rule

- Do not rebuild, execute, restore, exclude, or allow the detected validation
  binary on this shared laptop.
- Continue only with source inspection and non-executable checks unless the
  user explicitly changes that decision.
- Do not submit a binary to Microsoft without the user's specific approval:
  submissions disclose the file externally.

Microsoft provides a developer workflow to submit a suspected false positive
for analysis: [Submit files for analysis](https://learn.microsoft.com/en-us/unified-secops/submission-guide).
Microsoft also documents false-positive handling for endpoint detections:
[Address false positives and false negatives](https://learn.microsoft.com/en-us/defender-endpoint/defender-endpoint-false-positives-negatives).

## Remaining engineering decision

If the user wants the default DesktopStub executable to have a smaller security
and detection surface, split raw keyboard/ASUS hardware providers and optional
package-registration helpers into separately built, explicitly launched
providers. That would be an architectural change and requires a user choice
about installation and user experience. It must not be framed as antivirus
evasion; its purpose is least capability and clearer trust boundaries.
