# DesktopStub detection review brief — draft, not submitted

This is a local factual brief for a possible Microsoft Security Intelligence
submission. This historical sample remains quarantined and has not been
restored or manually uploaded. The user separately approved submission of a
later comparison build and sanitized report, which Microsoft received. A
later build at the same local filename is a different file; see the
[comparison-build brief and status](defender-review-current-build.md).

## File and source identity

- Product: DesktopStub, an open-source Windows wallpaper/tile content host.
- Repository: <https://github.com/Antonomasia3rd/AIProjects>.
- Production source reviewed at `b82bc87` on
  `codex/review-original-requirements`. Subsequent `d2257f3` changed documentation;
  `7da6f19` changed an inert fixture and documentation, not production code.
- Build: MSVC from Visual Studio 2019 Build Tools, Windows SDK 10.0.19041.0,
  using `DesktopStub/BuildDesktopStub.cmd`; reported version
  `DesktopStub-v27 (27.0.0.0)`. Hardware providers were included (`1`) but not
  enabled by the offline test.
- Original build filename: `DesktopStubValidation.exe`.
- Smoke runner copies the build with `File.Copy` to an isolated directory as
  `DesktopStub.exe`. The quarantined copy cannot be re-hashed without recovery;
  no recovery was attempted.
- Recorded SHA-256 of the original build before execution:
  `84E00CA40C8C35D2531330DC64C1773647FEC23A81D4EE92B8C87B5DA1BD7ABB`.
- Recorded file length: 2,872,320 bytes. Last-write metadata:
  2026-10-02 23:37:47.8144804 UTC.
- A subsequent attempt to verify the surviving build's hash was blocked by
  Windows with the virus/PUA error. The recorded hash has **not** been freshly
  re-verified. The original path must not be used to bypass quarantine.

## Detection and scan evidence

- Detection: `Trojan:Win32/Bearfoos.A!ml`, Threat ID `2147731250`.
- A custom `MpCmdRun -Scan -ScanType 3 -File <original-build>` scan reported no
  threats before execution. Remediation was enabled; no exclusion was added.
- The broker was also scanned without a detection, then only copied as a local
  manifest fixture; default smoke does not execute that helper.
- Detection record: 2026-10-03 06:39:06.508 +07:00.
- Defender Operational events 1116 include times 06:39:06.537 and
  06:39:09.783 +07:00. Their source is `System`, origin `Local machine`; the
  latter names the test runner `RepoTools.exe` as its associated process.
- Event 1117 at 06:40:00.712 +07:00 records successful `Quarantine`.
- Event security intelligence: AV/AS/NIS `1.459.512.0`.
- Follow-up record: `IsActive=false`, `ActionSuccess=true`, and
  `DidThreatExecute=false`. Earlier test commands completed according to the
  runner log; that flag does not establish that the executable never ran.
- The copied executable was absent afterward, and no test processes remained.

## Reproduction already observed

The bounded default runner uses temporary fixture data and a kill-on-close
Windows Job Object. No package/startup/hardware/account integration opt-in was
supplied. It copied the host, then completed:

1. Version and help output, including help with unapplied startup arguments.
2. Four startup-operation rejections in configure-only mode.
3. Rejection of external content providers in offline rendering.
4. Rejection of rendering without an explicit image/None background.
5. Updating temporary INI text while preserving existing startup preferences
   without applying them.
6. `--configure-only --content-source Wallpaper` against a synthetic layered
   profile, which disables layered content and preserves its entries.

The next planned invocation was the same preset command with a later explicit
`--set Content.Enabled=1`. The runner stopped with Windows' virus/PUA error
before recording that step as successful. Rendering and concurrent-write smoke
cases later in the suite were not reached. This sequence does **not** establish
which command, byte pattern, or external detection update caused classification.

## Suggested submission description

Please review this detection of DesktopStub as Trojan:Win32/Bearfoos.A!ml.
The locally built validation executable passed an on-demand custom Defender
scan, then its temporary test copy was quarantined while a bounded
offline help/configuration test sequence was running. The intended operations
at that point are command-line output and reads/writes of synthetic temporary
INI files. Package registration, startup integration, network providers and
hardware controls were excluded from the test entry path. The full executable
does contain those optional application capabilities. We have not established
that this is a false positive and request a determination for the recorded
file hash and, if authorized and available, the actual sample.

## Submission handling

Use Microsoft's [file submission portal](https://www.microsoft.com/en-us/wdsi/filesubmission)
as **Software developer**, with the appropriate Microsoft Defender Antivirus
product. Its [developer FAQ](https://learn.microsoft.com/en-us/defender-xdr/developer-faq)
describes disputing a detection and waiting for a final determination.

This brief intentionally excludes personal absolute paths and raw configuration
or account files. If a sample upload is authorized, provide only the specific
compiled file after verifying its identity through an approved collection
process. Do not upload the whole repository working directory or test archive.
Normal access to the surviving build is currently blocked; do not restore,
rename, rebuild, or weaken protection solely to get around that block.
