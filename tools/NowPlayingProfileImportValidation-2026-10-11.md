# NowPlayingTile donor profile import fixture

The new read-only adapter is `dependencies/content_sources/now_playing_profile.inc`.
Its API is `aip::now_playing_profile::ImportProfileText(text, plan, error)` and
`ImportProfileFile(path, plan, error)`. A successful `ImportPlan` contains a typed
`ProfileSnapshot`, a `std::vector<aip::IniSetting>` host batch, and diagnostic
strings. Failure clears the entire returned plan and supplies an error.

The adapter uses the existing native INI document parser and bounded file reader.
Comments, quoted section/key/value names, native BOM encodings, case-insensitive
names, and effective repeated-key behavior come from that shared implementation.
Separator aliases normalize to the six native donor settings; conflicting
semantic aliases fail before any batch is returned.

| Donor layout | Imported entries | Host TextMode |
| --- | --- | --- |
| Text | `None` background, `SMTC` text | Auto |
| Artwork | `SMTCArtwork` background, empty TextSources | Auto |
| Combined | `SMTCArtwork` background, `SMTC` text | Overlay |
| Cycle | Text, Artwork, Combined in that order | Overlay |

The host's existing artwork fallback provides text when an Artwork entry has no
thumbnail. This fixture checks the imported schema, not a live media session.
Cycle emits advisories for the host's global TextMode and for replacing the
donor's Windows notification queue with host entry cycling. The importer does
not write `Content.CycleSeconds`; the destination's preference remains in use.

`UpdateIntervalSeconds` maps to `Content.RefreshSeconds`. The donor's
`TileRefreshSeconds` only republished an unchanged notification/queue in
`MaybeUpdateLiveTile`; no equivalent host delivery preference exists. It remains
in the typed snapshot with an explicit diagnostic and is not converted to a
source-refresh or cycle interval.

`ShowTrayIcon`, `RunAtStartup`, and `ShowMenuAsDropdown` map respectively to host
`Settings.TrayIcon`, `Settings.RunAtStartup`, and `Settings.ShowMenuAsDropdown`.
The importer performs no Startup operation. Before applying a default import,
the host must use `ProjectConfigurationOnly(plan, projection, error)`. This
returns a typed `ConfigurationProjection` with both Startup keys omitted,
`donorRunAtStartup` metadata, and an explicit diagnostic showing the deferred
true/false preference. The full plan's source snapshot remains intact. Existing
host Run at sign-in controls or a separate `--startup` / `--no-startup` command
provide the explicit opt-in through the normal host Startup transaction.
`RunAtStartupPackaged` is never inferred from the packaged donor StartupTask.

`RemoveContentEntriesFromText(text)` removes obsolete `Content.*` sections from
an in-memory candidate with the native INI helpers. Later CLI/tray wiring calls
it inside the existing atomic INI transaction before applying projected entries
and normalized explicit overrides. The helper preserves `[Content]`, including
the existing cycle cadence, and unrelated sections. It performs no file access.
Default CLI import should select the existing configure-only path, which runs
before resident signaling and rejects Startup/activation integration.

The current native donor exposes no configurable widget text, font, color, image
or geometry fields. Unsupported preferences are retained in the snapshot and
diagnosed; they are not assigned invented or unrelated host keys. Source files
are read without repair or mutation. The adapter includes no media, artwork,
Startup, package, registry, account or device implementation.

Validation on 2026-10-11:

- `tools/BuildNowPlayingProfileImportTests.cmd` compiled the fixture under
  MSVC C++17, `/W4 /WX`. It performs compilation only.
- PowerShell 7 `tools/TestNowPlayingProfileImportTests.ps1 -Run` used
  `tools/ReviewValidation.ps1` to custom-scan the exact binary before execution.
  Defender reported no threats. No protection configuration was changed.
- SHA-256:
  `9E7A4483AA2383A0E5E21D1669ED41D9B9B17B1EE6D9D8CA16BF2181F3246979`.
- Exact fixture result: `NowPlayingTile profile import: 111 inert checks passed.`
- The fixture used synthetic INI text and files within a uniquely created
  temporary directory. Its internal deadline was 45 seconds; the outer process
  deadline was 60 seconds. Tests covered all four layouts, aliases/case, native
  comments/quotes/BOM encodings, inherited donor defaults, explicit host override
  order, invalid settings/syntax/Unicode, oversized and missing files, empty
  output on failure, byte-for-byte preservation after success or rejection,
  Startup-free configuration projection, visible deferred Startup metadata, and
  content entry replacement with unrelated host settings preserved.
- Evidence is in `build/review-validation/now-playing-profile-import-build.log`,
  `now-playing-profile-import-defender.log`,
  `now-playing-profile-import-scan.sha256`, and
  `now-playing-profile-import-tests.log`.
- Host CLI/tray wiring and injected artwork composition have a separate host
  gate recorded below. Normal Startup transactions, actual SMTC sessions and
  visible Live Tile delivery were outside these inert fixtures.
- Codex usage was checked during validation: the available weekly account
  window was 25% used and ordinary usage remained allowed.

## Native host wiring

`--now-playing-import-profile INI` now selects configure-only automatically.
Relative donor paths follow the target INI directory convention. The same-file
source/target check runs before reading the donor or writing the destination.
Expansion uses `ProjectConfigurationOnly`, keeps the full donor snapshot in
command metadata, and applies the last selected complete donor before explicit
host settings. Diagnostics are printed after a successful local save.

CLI publication removes all old `Content.*` sections inside the existing INI
transaction, applies the merged setting batch, and validates the prospective
configuration again under that transaction. Preflight uses the same fresh-entry
defaults, allowing obsolete optional fields to be replaced. The existing offline
command rejects explicit Startup, rendering and activation operations before
publishing the imported settings.

The content menu now includes `Import NowPlayingTile profile...`. Its route uses
the same source guard and safe projection, replaces entries in the existing
atomic menu transaction, and displays the deferred Startup choice and remaining
diagnostics. The import handler calls no media/session refresh or Startup API.
Live behavior follows the resident's existing configuration polling.

The owned Notes expansion also received the requested credential precedence
correction: explicit plain/protected token or user-ID settings clear their alias
family and canonicalize the chosen field before applying the override. Synthetic
regressions exercise both directions without logging or opening credential values.

`BuildNowPlayingHostIntegrationTests.cmd` compiles separate inert runtime and
passive host binaries. `TestNowPlayingHostIntegrationTests.ps1 -Run` scans them
before bounded execution. The final passive CLI binary passed 18 cases, including
all layouts, explicit precedence, deferred Startup, malformed/disallowed inputs
with unchanged target hashes, missing/self sources, repeated full imports,
relative paths and no package/assets creation. The imported-layout runtime and
Notes regressions passed. After the independently owned configuration-snapshot
fix froze, fresh service objects and both host binaries were rebuilt. The final
runtime result was `208 content runtime/menu checks; 0 failures`. The strict
200-reader/300-writer concurrent-publication assertion remained unchanged and
passed. The final passive result was `NowPlaying passive CLI checks passed: 18;
malformed/disallowed inputs made zero target changes.` Both host binaries had
clean Defender custom scans before execution; the runner enforced 60-second
stages and the runtime fixture retained its 45-second self-deadline.

The standalone importer was also rebuilt against the frozen model with `/W4
/WX` and repeated its clean scan and 111 passing checks. Host compilation used
the existing `/W4` setting and retained existing SDK shadowing, Discord unused
functions, fixture unused-parameter and inert constant-condition warnings.

Final compiled/scanned checkpoint:

| Binary | SHA-256 |
| --- | --- |
| `NowPlayingProfileImportTests.exe` | `9E7A4483AA2383A0E5E21D1669ED41D9B9B17B1EE6D9D8CA16BF2181F3246979` |
| `NowPlayingContentRuntimeTestsOct11.exe` | `C3F79A4751BF0DC959F42CDA99C1FF44021534ED1954FE50EF1CB0B007B5DE54` |
| `DesktopStubNowPlayingPassiveOct11.exe` | `0F882C9AF339C6E7FC771C3FDDB75D86CD80DBE3D88C056F8B7ACFA1CAB385E9` |

Logs and exact scan hashes are in `build/review-validation/` under the
`now-playing-profile-import-*`, `now-playing-host-*-build`,
`now-playing-host-runtime-*`, `now-playing-host-passive-*` and
`now-playing-host-passive-case-*` names. Codex usage was checked during host
validation at 37% used in the available weekly window, with ordinary usage
allowed. No real Startup/package/device/media/account integration was run,
and no donor was retired in this checkpoint.
