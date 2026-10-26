# WuWa freshness and independent failure presentation

Each Wuthering Waves tile has **Source settings > Data freshness and failures** in both the tray and Properties. The same fields use `SourceHealth.<key>` inside `[Tile.<id>]` and ordinary typed CLI `--set` assignments. The portable policy in `dependencies/source_health.h` is reusable, but this change activates it only for WuWa; it does not silently alter the other sources' behavior.

The default keeps useful last-successful resources during an outage and prepends a subtle **Refreshed 5m ago** row while they are stale. A healthy refresh removes that row and all failure state. `AgeDisplay=Always` shows the age while healthy too; `Off` hides it. Age uses the actual worker completion time, not the most recent failed attempt or idle poll. Its default 60-second buckets avoid changing the prepared image on every scheduler read. There is no extra age timer or persistent worker.

The state is scoped to the exact token/role/device revision. An account/role change, unpin/provider destruction or process restart discards the in-memory last-good snapshot and age; no account data or freshness sidecar is written. Changing refresh cadence, timeout or byte limits preserves the same account's useful last-good resources and original success time while superseding its request. Editing presentation policy does not perform another network request.

## Completed attempts and thresholds

The worker records one outcome for each completed source refresh. Re-reading a cached result cannot add failures. The OAuth, role discovery and inner role-query retries are one source refresh, not multiple failed attempts. Canceled or superseded work cannot update the new account's state. The existing bounded code-1005 retry sequence remains within the configured HTTP deadline; this policy adds no immediate retry loop and keeps the user's `Kuro.RefreshSeconds` cadence.

Ordinary tile failure text and popup notices each default to five consecutive completed source failures, with independently configurable attempt and elapsed criteria. Elapsed seconds run from the first consecutive completed failure, not application startup or the last successful update. `Either` accepts either enabled criterion; `Both` requires all enabled criteria. A zero disables that individual criterion. Both criteria zero never opens an ordinary failure gate.

For example, a ten-minute refresh interval can produce its fifth failure fifty minutes after a success if the first failure occurs ten minutes later. That is not a hard-coded five-to-fifty conversion. Changing cadence, fetch duration or the separate elapsed threshold changes the timing. No old successful result is relabeled as a fresh measurement.

`AuthenticationImmediate=1` lets rejected/account-attention or setup failures bypass the ordinary delay. Rejection does not prove that the saved login expired. Set it to `0` to apply the configured delay to those outcomes too. Transient connection/server failures and invalid responses otherwise follow the configured thresholds. Missing sign-in remains an explicit setup state without starting network activity.

## Tile versus popup behavior

`TileFailureMode=Append` retains resource rows and adds threshold-qualified failure text to the subtle status row. Medium/Wide/Large use the existing size-aware game layout: the status consumes a visible row, so a smaller tile may show fewer resource rows, while the retained structured snapshot remains intact. `Replace` explicitly replaces the visible resources with status/error presentation; switching back to Append recovers the retained values without fetching. `Off` suppresses failure wording independently of age and popup policy. TextMode Off and the existing size gates still apply.

Before a first success, failures below the tile threshold show `NoDataText` rather than immediate technical failure text. Detailed sanitized source diagnostics remain available in **All tile contents**, including while the tile quietly retains useful data. Account tokens and arbitrary transport exception text are not included in tile or popup wording.

Popup failure and recovery controls are separate from tile presentation and independent of each other. A threshold-qualified failure transition is coalesced until a healthy result; successful recovery resets the gates and can issue one recovery notice. Disabling failure popups does not disable an explicitly enabled recovery popup, so recovery-only notices are supported. Disable both toggles to silence both kinds. Existing application master notifications, source-error preference and Auto/Toast/Balloon routing remain authoritative. Blank popup templates retain application-wide configurable wording.

Windows decides when an accepted Live Tile notification is displayed. The age is captured in prepared content; it is not a guarantee that Start will redraw a live counter every minute, particularly under Battery Saver.

## Settings

| `SourceHealth.` key | Default / supported values |
| --- | --- |
| `RetainLastGood` | `1`; boolean |
| `AgeDisplay` | `Stale`; `Stale`, `Always`, `Off` |
| `AgeResolutionSeconds` | `60`; `1..3600`, presentation buckets only |
| `AgeTemplate` | `Refreshed %age% ago` |
| `TileFailureMode` | `Append`; `Append`, `Replace`, `Off` |
| `TileFailureAttempts`, `PopupFailureAttempts` | `5`; `0..1000`, zero disables the criterion |
| `TileFailureSeconds`, `PopupFailureSeconds` | `0`; `0..604800`, measured from first consecutive failure |
| `TileThreshold`, `PopupThreshold` | `Either`; `Either` or `Both` |
| `PopupFailureEnabled`, `PopupRecoveryEnabled` | `1`; boolean |
| `AuthenticationImmediate` | `1`; boolean, independent of ordinary thresholds |
| `TileFailureText` | `Refresh delayed: %error%` |
| `NoDataText` | `Waiting for the first successful refresh.` |
| `PopupFailureTitle`, `PopupFailureBody` | blank uses application wording |
| `PopupRecoveryTitle`, `PopupRecoveryBody` | blank uses application wording |

Templates support `%age%`, `%ageSeconds%`, `%ageMinutes%`, `%failures%`, `%elapsedSeconds%`, `%error%` and `%tile%`. `%%` writes a percent and `\n` makes a line break. Compact `%age%` uses minute/hour/day units; the numeric age tokens let users supply different unit wording. Age tokens share the configured presentation bucket; failure elapsed thresholds remain precise. Unknown tokens, control text and oversized patterns are rejected before saving; expanded output is bounded.

Example for an existing WuWa tile:

```ini
[Tile.wuwa]
Kuro.RefreshSeconds=600
SourceHealth.RetainLastGood=1
SourceHealth.AgeDisplay=Stale
SourceHealth.TileFailureAttempts=5
SourceHealth.TileFailureSeconds=0
SourceHealth.PopupFailureAttempts=5
SourceHealth.PopupFailureSeconds=3000
SourceHealth.PopupThreshold=Either
SourceHealth.PopupFailureBody=%tile% could not refresh after %failures% attempts. %error%
```

This does not add a tile, sign in, change its account, or set its other existing fields. All notification thresholds and wording are local preferences; they do not repair Kuro's remote service or prove a real token/server will be accepted.

## Validation boundary

Focused portable-policy and provider tests use synthetic responses and a controllable monotonic clock. The host fixture `KuroSourceHealthHostTests.inc` covers the actual menu registry, typed settings, independent tile values, guarded writes, ComposeTileInstance, content-key buckets, semantic game XML and popup transition gates. No account, password, real token, browser, notification, package or hardware integration is exercised. Focused counts and the final combined-host gate are recorded after completion; installation/native acceptance remains root-owned.

At the focused source freeze, `SourceHealthTests` passed **59 checks** and `KuroNotesTests` passed **99**, with optimized `/W4 /WX /O2` builds and clean Defender scans before execution. Evidence: `build/review-validation/source-health-model-oct26*` and `kuro-source-health-oct26*`. The combined host fixture was supplied for root's final gate; a completed host run or installation is not claimed by these focused results.
