# DNSAutoUpdate passive configuration

Source correction prepared on 2026-10-13. The coordinated managed fixture
passes 253 checks with real Startup integration disabled. A fresh normal DNS
product build and its custom Defender scan also pass. The product's resident
DNS or Startup integration was not executed.

## Finding and correction

`--configure-only` previously entered the ordinary application startup path:
it could inspect an existing shortcut while creating a profile, persist through
the Startup adapter, and reconcile the saved Startup preference before exiting.
Thus changing an unrelated DNS setting could add/remove a real launch entry.
That differed from the other managed products' passive configuration contract.

The passive branch now returns before Startup discovery/reconciliation,
resident control-object creation, interface enumeration, `dnscmd`, logging
runtime setup or tray construction. It uses a validated local INI batch under
the existing write lock. Existing Startup preferences are preserved, without
constructing or querying the Startup platform.

Explicit Startup edits via a flag, direct setting option or `--set` are rejected
when combined with `--configure-only`, before any profile mutation. Use the
existing `--startup`/`--no-startup` actions separately. Ordinary launches, tray
Startup edits, coupled rollback and normal resident reconciliation retain their
current behavior. This is a passive-command consistency repair, not a change
to the user's per-user Startup rule.

The local batch validates the full prospective effective profile before
publication. A rejected new profile leaves no default file. A replacement can
repair an invalid saved value before validation; an incomplete repair preserves
the exact original bytes. Existing DNS owner/address safeguards are unchanged.

## Regression scope

The `LegacyUtilitiesTests` fixture redirects the mutable INI specification into
one fresh temporary directory. Its Startup factory throws if constructed. Tests
cover true/false saved preferences, first profile creation, every Startup CLI
surface, exact byte preservation on an invalid update, repair of a bad saved
value, and rejection of a new apply profile missing its required allowlist.
The fixture disables its diagnostic callback and restores both profile path and
callback during cleanup. No product build/test was executed by this subtask.

## Selected profile and inspection parity

The subsequent source correction adds `--ini`, `--show-config` and
`--print-config`. Parsing now finishes before profile-dependent specifications,
hash identity and Startup arguments are initialized. Explicit relative INI paths
use the working directory, defaults remain beside the executable, and empty or
directory paths fail. Help/version do not initialize selected specifications.

Read-only inspection validates effective defaults/stored values without creating
or changing an INI or constructing the Startup platform. Combined
`--configure-only`/inspection prints the validated committed profile. Inspection
with implicit setting changes, operational control, one-shot or Startup actions
is rejected before publication.

Resident mutex/reload/exit/run-now identities, tray configuration opening, INI
publication and Startup identity all use the selected profile. A custom Startup
shortcut carries a quoted `--ini` argument. Only the default sidecar preserves
its old empty-argument/legacy-filename adoption path. DNS-zone ownership and the
existing relative-log-to-executable policy are unchanged.

Additional temporary-profile tests invoke the real passive entry point with a
throwing Startup factory. They cover missing/invalid inspection without writes,
combined repair/save/show, Unicode/spaced/relative paths, flag aliases, rejected
operational combinations, exact custom/default Startup metadata, isolated
resident/control name strings and help/version short-circuiting. No real
control events, resident app, link or DNS operation is created by these tests.
The full scoped managed fixture, including these new cases, passes 253 checks
under root's scan-gated runner. Real Startup remains disabled, and no DNS
product, real control events or resident process is launched by this validation.
The changed normal product compiles and custom-scans clean; these temporary-
profile checks do not establish live DNS or shell acceptance.
