# Per-tile delivery and Registration follow-up, October 20

This follows the earlier independent-instance installation. See
[the tile guide](tile-instances-2026-10-20.md) for configuration examples.

## Behavior

Each ordinary tile and gallery row captures its own delivery, image-cache,
asset-generation, text-size visibility and image-fitting settings. Existing
profiles copy missing fields from their old settings once. Future global edits
do not silently change existing tiles. Defaults are captured when a new tile
is created. Cache partitions include the profile and permanent tile owner, so
pruning one tile cannot prune a sibling's generated versions. Legacy slideshow
precache controls are hidden in composed-tile menus because that worker has no
selected tile snapshot.

Registration now has a separate Windows 10 package identity per user, profile
and tile, with its own image assets and a small transient launcher. The launcher
first forwards the click to the existing profile's resident; it does not copy
the full resident per tile. Static images contain rasterized text over the
configured background, normally transparent. They do not use the notification
queue. Registration galleries publish one photo per durable receipt; native
galleries keep their Windows-managed queue and the same disjoint ownership
ledger. Registration requires the Windows 10 manifest target; Windows 8/8.1
targets remain separate legacy compatibility paths.

Switching delivery through a tile's menu explains the different pin identities
and stages Registration only after that explicit choice. Existing pins and
ownership history are retained. The replacement Registration app must be pinned
manually from Start after its first successful publication. Direct INI/CLI edits
select the policy but cannot authorize a new package silently: select Prepare
Registration tile in that tile's menu to complete setup. Existing authorized
tiles can update their own packages in the background. Switching a tile back to
Live delivery keeps its old Registration entry for manual unpinning/recovery.

The main package's existing identity is preserved. Windows registers it per
current user. New Registration identities additionally include stable hashes
of the Windows user identity, normalized profile path and tile ID. Changing a
username does not rename those identities. Ownership markers check the full
scopes before updating or clearing cached packages.

Graceful exit respects each tile's clear preference. Registration cleanup
replaces its already-published generated images with transparent PNGs and
updates only that owned package; native cleanup clears the selected owned
notification queues. Neither path unpins tiles, deletes source photos, releases
reservations or resets the gallery ledger. Forced termination still cannot
guarantee cleanup.

RSS Default now uses one headline on Medium, two on Wide and three on Large,
with 15-pixel semibold titles, subdued summaries and bounded wrapping. Native
XML, bitmap rendering and status use the same size-specific row counts. Custom
layouts remain explicit. A layout revision invalidates older generated images.

The pin API now receives an anchor at the selected monitor work area's center,
converted into the existing hidden owner's client DIPs. The owner is positioned
without being shown or activated. Windows controls the native flyout's exact
size and placement; no foreign PickerHost window is moved.

Wallpaper snapshots capture their fitting mode. Disabled fit observation holds
that value until manual refresh/configuration change; enabled observation uses
the tile's deadline. Non-wallpaper sources using Auto fitting resolve Fill
without desktop registry queries. Shared process settings, such as application
startup and plugin actions, remain application settings.

## Validation record

The private user-profile preflight retained eight source instances, sixteen
gallery rows, all original account/INI values and prayer settings, every source
and gallery identity, and the exact gallery ledger with sixteen historical
owners and eighty-one reservations. It added 832 missing gallery policy fields.
The private input and exact backup were preserved. No packages or providers were
run by that preflight.

Windows SDK 19041 MakeAppx accepted the actual generated manifest, real launcher
and all eight image assets with semantic validation enabled. This was packaging
validation only, with no signing, deployment, activation or pinning.

Integrated tests use real Generate rendering, private images/INI/cache roots,
and an injected deployment result. They check distinct tile PNGs, changing one
headline without touching its sibling, migration cancellation, ownership and
user/profile separation, static-gallery receipts and scoped exit cleanup.
These tests caught and fixed an integer-reader path that bypassed local policy,
as well as a static-gallery receipt mutation. The first busy-machine test hit
the unchanged 45-second watchdog; subsequent validation must run without
compiler contention. No timing limit or pixel comparison was weakened.

Final gates passed:

- 724 extended host checks, zero failures, within the unchanged 45-second
  fixture watchdog (34.98 seconds on the final quiet run).
- 1,836 source guardrails, 69 pure instance/policy checks, 786 gallery engine
  checks, 111 gallery runtime checks, 107 media/pin/RSS layout checks and 28
  Registration model checks. Newly executed binaries received fresh Defender
  scans. The tiny launcher also passed its final inert argument-validation run.
- The copied-binary DesktopStub offline smoke passed, including existing exact
  pixel comparisons, changed-text/cache restoration, all supported tile sizes,
  transparent rendering and concurrent INI writes. It did not use package
  integration opt-ins.
- The final product CLI accepted independent source/gallery cache settings and
  rejected Registration with a Windows 8.1 target atomically, preserving the
  private input's exact bytes on rejection.

The reviewed host, broker and launcher are installed and freshly scanned.
Resident PID **6132** started at **18:35:55** and passed the guarded 75-second
startup check. All seven existing source destinations appeared after startup;
nine completed batches, including a complete 76-frame batch, and no failed
batches were observed in the recorded window. This validates actual native
publication as well as process survival. Registration deployment remains an
explicit in-app setup choice and was not performed on the user's real pins.

Binary and exact pre-upgrade sidecar backup:
`DesktopStub/build/ReviewBackupOct20-20261020-183544`.

| Installed artifact | SHA-256 |
| --- | --- |
| Host | `B64DD8E4932D4CB9CB8C3544B917F2A57A58D8FEDDD944847AE6AE4AD6CFE711` |
| Broker | `23D744188FC18EA6BE967FFFB334D119AA871C28A813C317610B06BABB75DFF0` |
| Transient launcher | `959262411D4F44BE66AAF134C67B656BA9F680957656A05391B6A871EC54E2C7` |

A passive 15.19-second sample of the installed full profile measured 0.0943%
mean CPU, 0.2571% peak one-second CPU, 15.59 MiB private commit and 17.26 MiB
total working set. CPU percentages are normalized across logical processors.
This is a short observation, not a maximum-load or long-term benchmark. Active
providers, gallery queues, configuration snapshots and caches affect memory;
this full-profile measurement does not establish that a wallpaper-only profile
matches the old approximately 4 MiB measurement. No such equivalence is claimed.

## Remaining acceptance limits

- Native Registration deployment, manual pinning and visible cache refresh are
  not exercised by inert validation. SDK schema success is not a claim that
  those Windows interactions were observed.
- Native pin-flyout placement, Start's RSS pixels and Battery Saver/lock/resume
  behavior still require ordinary use on the installed build.
- A malformed hand-edited gallery row still pauses that gallery configuration;
  ordinary source instances already isolate invalid siblings. Tray/CLI typed
  writes reject invalid policy values before committing.
- The historical four-pixel opaque caption antialiasing variation remains an
  open renderer finding. Its exact comparisons and tolerances are unchanged.
