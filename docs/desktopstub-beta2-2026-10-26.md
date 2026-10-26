# DesktopStub v27 beta 2 — October 26

Version **27.0.0.2** contains the [Properties, Profiles, source-health and host
preparation follow-up](desktopstub-properties-health-2026-10-26.md). Its source
commit is `be8b8220592f7ab7144ce8533bfc45c9e2f6e4a1`, pushed to
`codex/review-original-requirements`.

## Payload

- Folder: `DesktopStub/releases/DesktopStub-v27-beta2/`.
- ZIP: `DesktopStub/releases/DesktopStub-v27-beta2-x64.zip`.
- ZIP SHA256: `2B0D52EABBAAA5E2BAB73E5270CB8674BB08772368E4560F3E94971B96C05023`.
- Seven files: three matching EXEs, read-only image inspection script, license,
  beta README and hash/source manifest. No settings, credentials, generated
  assets or profile coordinator are included.

Extract the whole ZIP to a permanent writable local folder on the test PC/VM,
then run DesktopStub.exe normally. Fresh setup contains one Desktop wallpaper
tile, Registration/Main, optional sources/plugins and both startup methods off.
After generation/registration finishes, pin Desktop from Start's All apps list.
Keep the utility running to follow wallpaper changes. No setup command is
needed for the ordinary first run.

All three EXEs should stay together for the additional features. The earlier
beta's single-EXE Registration/Main trial remains historical evidence for the
core path; this beta's native trial used the complete payload. Do not copy
another machine's generated INI/package/profile/cache state for a fresh test.
Windows must permit package registration and the chosen Start host must support
tiles. The 1809 main-wallpaper report remains unresolved.

For an upgrade rather than a reset, close the old resident, back up its folder
and retain its INI while replacing matching binaries. Existing explicit
delivery choices are preserved. The conservative v1-v25 migration and its
4,010 historical/customized checks remain the [beta baseline](desktopstub-beta-2026-10-26.md),
not a claim that every customized old profile/account has native acceptance.

## Validation

Full host, broker and launcher builds succeeded with matching 27.0.0.2 version
resources. Fresh Defender scans passed before execution. No historical
quarantine was restored or protection bypassed.

The final gates passed: 2,157 combined host checks, 2,285 source checks,
1,313 Properties policy checks, 39 Profiles model checks, 59 shared health-policy
checks and 99 Kuro provider checks. The host fixtures use injected account,
network, notification, profile/process and hardware operations. The unchanged
four-pixel legacy renderer discrepancy remains open.

A real cold launch at the canonical development installation generated
150x150, 310x150 and 310x310 Desktop wallpaper PNGs and registered exactly one
main package, version 27.0.0.2. The native appearance receipt observed the
resident alive after 214 seconds. No account source or extra profile was added.

Computer-use inspection verified that --profile-manager opens the Profiles
section in the existing Properties window, Main opens an indented subpage,
the former filled row highlight is absent, and a deliberately black custom
color remains readable on the dark theme after F5. The external edit was made
through --configure-only, then discarded with the validation state. Native
Start pin presentation, real Kuro session stability, other themes/OS versions
and full multi-profile action acceptance remain separate.

## Reset and recovery

The user explicitly renewed reset permission after compaction. The original
installation was stopped and reset before testing, and the validation trial
was reset again afterward. At completion the canonical build directory has
exactly seven payload files with matching hashes, no INI, no resident and no
trial package. The next ordinary launch creates fresh settings. This is a
historical completion snapshot; inspect actual state before later operations.

Canonical installation:
`D:\Users\Amiya\AppData\Local\Programs\Scripts\AIProjects\DesktopStub\build`.

Recovery archives below that repository:

- `artifacts/desktopstub-reset-20261026-195613-027929/`: pre-beta2 build,
  settings/profile coordinator and owned package removal receipt.
- `artifacts/desktopstub-reset-20261026-200157-4647e3/`: the native validation
  build/settings, final fresh-state receipt and `validation-build/` containing
  the former root build's bounded fixtures, build logs and scan receipts.
- `artifacts/desktopstub-feedback-20261026-properties/beta2-native-*.json`:
  cold-start/appearance/asset observations. The UI screenshots were inspected
  in the conversation, not exported as source artifacts.

Source photos, the Desktop OAuth client file, external browser accounts,
unrelated packages and other tasks' edits were not reset. Archived credentials
are not automatically restored into the fresh installation.

## Shared-checkout isolation

Another task switched the original checkout to `codex/windhawkmods` during
the run. The DesktopStub commit was explicitly pushed to its intended branch.
Its accidental inclusion on the Windhawk branch was reverted without changing
the unrelated Windhawk working edits. Delivery documentation now uses the
managed worktree at
`C:\Users\Amiya\.codex\worktrees\desktopstub-beta2-delivery\AIProjects`,
on `codex/review-original-requirements`. The installed app and shareable ZIP
remain at the canonical D: repository paths above. Do not derive the installed
app's reset scope from the managed worktree's current directory.

## Remaining work

The [findings reconciliation](desktopstub-open-findings-2026-10-25.md) remains
authoritative. The new [1809/lock-screen report](desktopstub-1809-history-and-lockscreen-2026-10-26.md)
and [provider report](desktopstub-provider-parity-feasibility-2026-10-26.md)
separate verified APIs/source history from unimplemented integrations. No new
lock-screen, Yahoo/Microsoft/CalDAV/IMAP provider or 1809 fix is claimed by this
beta. Performance remains completed for now based on the user's observation.
