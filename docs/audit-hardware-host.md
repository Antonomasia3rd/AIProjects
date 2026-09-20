# DesktopStub action-provider lifecycle review

Updated 2026-09-19. This bounded review covers Caps, ASUS, and Discord host
wrappers, with fake/inert tests only. Native keyboard/ASUS backends are excluded
from the harness; no hardware, account, network, or global input tests are used.

## Findings

- ASUS `Cancel` retains its cached key while `Stop(0)` is pending. `Refresh`
  checks key equality before checking service completion, so quick reenable
  with identical configuration can leave the source permanently stopped.
- Discord checks completion before its key cache and normally recovers, but
  quick reconfiguration during a pending stop can call Reload on a canceled
  service. Stop paths also retain cached desired state inconsistently.
- A direct invalid Caps configuration fails Controller validation but can
  leave the previous action running. The outer composition reader already
  cancels providers on invalid INI edits; the wrapper itself should preserve
  that cancellation behavior too.
- Caps uses a static logical-reader function and atomic cache, so its cleanup
  does not capture a destructible per-refresh object. UI queries have a bounded
  SendMessageTimeout and fall back to cached logical state during shutdown.
  Pending driver cleanup must remain on the owner worker, without UI joins.

## Planned repair and ownership

`dependencies/DesktopStub/ga_content_runtime.inc`: explicit pending-stop state,
cache invalidation at cancellation, completion checks before restart/reload,
and invalid-Caps cancellation. `DesktopStub/tools/ContentRuntimeTests.cpp`:
fake/inert regressions for cancel/reenable/restart/invalid configuration.

The initial account usage check reported 13% five-hour and 18% weekly consumed.
Checks will be repeated before validation. Implementation and tests are pending.
