# Media and source tile presentation, 2026-10-15

The SMTC binding had two concrete presentation defects: every column used
vertical center stacking, and the image selected a pending play/pause action by
searching formatted badge text for `Playing`. The host now carries explicit
current playback state from the provider and uses top-aligned presentation.
Playing shows play, paused shows pause, stopped shows stop, and missing state
shows an unknown indicator. The configured tile click action is independent.

The shared settings are `[SMTC] Indicator=Status|AppIcon|None` (default `Status`)
and `ShowRepeatShuffle=0|1` (default `0`). The normal shared typed-setting path
validates both persisted configuration and generic CLI/tray writes. Root owns
the corresponding defaults and menu controls. `None` removes the main indicator
column when optional metadata is also hidden. Known repeat/shuffle off values
remain distinct from missing metadata; unknown values produce no extra icon.
The provider reads the current nullable
[repeat mode](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessionplaybackinfo.autorepeatmode)
and [shuffle state](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessionplaybackinfo.isshuffleactive)
when supplied by the player.

AppIcon mode resolves the originating AUMID through an uncached
[AppInfo.GetFromAppUserModelId](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.appinfo.getfromappusermodelid)
factory on the existing source worker. This API requires Windows 10 version 2004
or later; unsupported Windows versions and unresolved player IDs fall back to
current playback status. There is no executable-path guessing or downloaded
icon pack. The logo uses the same total source deadline, at most 1 MiB encoded
bytes, and at most 256 by 256 decoded pixels. Failures are cached for five
minutes for that AUMID; a changed source retries independently. Source text and
logo remain in one immutable snapshot.

The existing artwork cache still decodes and bounds images. Its optional logo
mode retains the original validated encoding, preserving alpha that the
ordinary 24-bit artwork BMP would discard. Rendering publishes a PNG in
`Assets/MediaIndicators` using an immutable content-derived name. Those package
copies remain readable for older notifications; this patch does not prune them
while Windows could still reference them. The normal SMTC artwork path retains
its previous bitmap behavior.

Both native adaptive XML and bitmap simulation now use the same top-oriented
media policy. Optional repeat/shuffle shrinks the icon column so three stacked
indicators fit Medium and Wide heights. Native text uses explicit wrapping.
Bitmap text uses bounded wrapped fields and clipping within each field. This
models the chosen adaptive structure; it does not claim exact shell pixels
across Windows builds, DPI settings, fonts or RTL.

Discord presence now has a compact application/presence headline. Details,
status and state occupy a wrapped lower field. The old generated `Configure
Details and State under Tiles ...` placeholder becomes `Set Details and State
in Discord settings.` without modifying the user's saved configuration.

The generic `Snapshot.detailRows` and `detailTopic` fields let Notes supply its
resource overview without numbering it as RSS. Native/bitmap layouts display
up to three rows on Medium/Wide and five on Large, with a resource icon beside
the first row. Sole Notes sources use the root-owned row formatter; mixed
sources preserve their existing text composition. Notes loading and timeout
snapshots carry the correct game resource. Explicit account Server preferences
are validated against the shared game whitelist and participate in source cache
keys, preventing reuse of another server's cached request.

## Prepared inert validation

- `tools/MediaTileLayoutTests.cpp`: explicit status semantics, optional unknown
  metadata, top/bounded columns for all three sizes, no-indicator width,
  repeat/shuffle icon bounds, native wrapping/escaping, generic detail rows, and
  shared media/server setting validation.
- `tools/SmtcSourceTests.cpp`: injected current playback/repeat/shuffle values,
  original-encoding logo leases, absent sessions, disabled icon work, and
  oversized/invalid logos without contacting a player.
- `tools/DiscordServiceTests.cpp`: compact headline plus retained presence
  details/state and a shorter lower-field setup instruction.
- Root owns integrated XML/bitmap/host fixtures and the baked cache-key fields.

This subtask did not build or execute tests, contact media sessions/accounts,
read the user's live settings, or replace the resident. Root coordinates scans,
bounded test execution and final target-system acceptance. Tests of source
policy and generated pixels do not prove the shell's actual pinned-tile layout
or a player's native metadata availability.
