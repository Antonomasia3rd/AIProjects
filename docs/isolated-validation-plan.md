# Plan for isolated Windows validation

Status: plan only. No VM has been provisioned and no Windows host feature or
Defender setting has been changed. This plan does not authorize restoring the
quarantined sample or executing it on the shared laptop.

## Environment

Use a disposable full Windows VM where Microsoft Defender Antivirus and its
normal protection operate. Record the guest OS build, Defender platform and
security-intelligence versions. Update the guest normally; do not downgrade
definitions or disable protection to obtain a passing result.

Keep the guest free of personal sign-ins, browser profiles, tokens, Startup
entries and live accounts. Transfer only the reviewed source and specific
authorized artifacts, without mounting the working repository or user profile.
Disable clipboard/drive/device sharing during reproduction. Hyper-V's
[enhanced-session documentation](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/enhanced-session-mode)
describes those shared local resources. An existing VM can be used; the user
must select where a new one would run before host setup is performed.

## Stages

1. Take a clean snapshot/checkpoint and record environment details.
2. Review the [submission brief](defender-submission-2026-10-03.md). Preserve the
   recorded sample's identity. A fresh build from the same source is a separate
   sample if its hash differs; do not silently substitute it in the evidence.
3. Scan the authorized sample without executing it. Keep the exact scan output,
   hash, detection records and event-log times. Stop if protection blocks it;
   do not bypass that decision inside the guest either.
4. If execution is appropriate, use the existing bounded RepoTools default
   smoke with only DesktopStub selected and explicit validation host/broker
   paths. Run interruption-cleanup fixtures first. Never pass
   `--allow-package-integration` or `--allow-startup-integration` in this pass.
5. Preserve failed fixtures and note the first command without a successful
   result. A preceding successful command does not prove what caused detection.
   Export only sanitized logs/metadata for review, not guest account data.
6. If detection is resolved and offline smoke finishes, retain exact bitmap
   comparisons. Investigate any recurrence of the four-pixel glyph difference;
   do not relax thresholds to make it pass.
7. Native Start-screen appearance on Windows 8/8.1/10 is a separate final test
   matrix. Registration, native tile delivery and sign-in behavior require
   explicit disposable-environment integration stages; offline success alone
   cannot validate them.

WSL is not needed for the portable model/layout tests. It does not supply the
native Windows shell/Defender environment required for this reproduction.
