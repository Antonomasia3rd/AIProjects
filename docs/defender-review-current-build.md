# Defender review: current comparison build

Prepared on 2026-10-03. The user approved submission of this comparison build
and sanitized report. Microsoft currently requires the user to finish sign-in;
no manual submission has been made. This is a different
file from the quarantined sample in the [incident brief](defender-submission-2026-10-03.md).
A clean scan of this file is not a determination about the older detection.

## Current file

- Local file: `DesktopStub/build/DesktopStubValidation.exe`.
- Source: `53d9e3229c92f6d8174ccf79aea8946142348428`.
- SHA-256: `ABDE676210EF687912698F9FE774D664CD0299308DDEC5619780D8EC2ABB7B95`.
- Size: 2,872,320 bytes. Authenticode status: `NotSigned`.
- Host OS: Windows 11 Enterprise, build `10.0.26200`.
- The normal build script was used, with the existing separate validation output
  names and all hardware providers compiled in. No provider was removed or
  converted to a DLL. The application was not executed.
- Defender definitions updated from `1.459.512.0` to `1.459.523.0` through its
  normal update command. Antivirus and real-time protection remained enabled.
- Custom scan: no threats reported; the file remained present with an unchanged
  SHA-256. Static PE inspection lists Windows/Windows API-set imports, with no
  per-source plugin DLL imports. This inspection does not prove safety.

The old original file was successfully quarantined at 06:53:30 +07:00, following
detection at 06:52:40 +07:00. Its path was absent before this build. The recorded
old SHA-256 is `84E00CA40C8C35D2531330DC64C1773647FEC23A81D4EE92B8C87B5DA1BD7ABB`;
the new file must never be described as that exact sample.

## Prepared submission information

Intended destination: [Microsoft Security Intelligence](https://www.microsoft.com/en-us/wdsi/filesubmission),
software developer review. The proposed disclosure is this one compiled file
and the factual text below; no INI, credentials, account data, full repository,
PDB or diagnostic archive is included. The user explicitly approved that
disclosure. Continue that approved submission after sign-in; do not request
the same upload permission again.

Suggested additional information:

> Please investigate a recurring Trojan:Win32/Bearfoos.A!ml detection of
> DesktopStub, an open-source Windows tile/wallpaper host:
> https://github.com/Antonomasia3rd/AIProjects.
>
> The previously detected build had SHA-256
> 84E00CA40C8C35D2531330DC64C1773647FEC23A81D4EE92B8C87B5DA1BD7ABB.
> It passed a custom scan on definitions 1.459.512.0, then was quarantined during
> bounded help/configuration testing on 2026-10-03. Event 1116 named the test
> runner RepoTools.exe; event 1117 recorded successful quarantine. We cannot
> attribute the classification to a particular operation. The original sample
> is quarantined and has not been restored.
>
> The attached file is a CURRENT COMPARISON BUILD, SHA-256
> ABDE676210EF687912698F9FE774D664CD0299308DDEC5619780D8EC2ABB7B95,
> from source commit 53d9e3229c92f6d8174ccf79aea8946142348428. Its custom scan on
> definitions 1.459.523.0 reports no threats. It has NOT been executed and must
> not be confused with the older sample. Please correlate the historical hash
> if it is available in your systems and advise whether further sample
> collection is needed for a determination.
>
> The full host includes optional package registration/PowerShell helpers,
> WinHTTP content sources, Discord IPC and keyboard/ASUS indicator backends.
> The earlier offline test path excluded package, Startup, network/account and
> hardware operations. This request does not presume the detection is a false
> positive. Please advise what caused the classification or what evidence is
> needed to resolve it.

Microsoft's [developer FAQ](https://learn.microsoft.com/en-us/defender-xdr/developer-faq)
directs developers to submit disputed detections and wait for a determination.
If Microsoft requires the exact old sample, this comparison file cannot replace
it; sample collection would be a separate scoped step.

## Local evidence

The ignored `build/review-validation/` directory contains
`detection-investigation-signature-update.log`,
`detection-investigation-standard-build.log`,
`detection-investigation-standard-host-defender.log`,
`detection-investigation-standard-host-scan.sha256`,
`detection-investigation-candidate.json`, and
`detection-investigation-pe-metadata.log`.

Existing Defender settings report MAPSReporting=2, SubmitSamplesConsent=1,
DisableBlockAtFirstSeen=false and DisableIOAVProtection=false. They were not
changed. This record does not establish whether Defender itself automatically
submitted either file; "no submission" here means no manual agent submission.
