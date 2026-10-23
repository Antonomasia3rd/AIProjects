# October 23 installed source, notification and fresh-profile update

The reviewed build is installed in DesktopStub/build. The user-requested settings reset completed with one primary Desktop wallpaper tile, LiveTile delivery, NameAndLogo branding and both startup preferences off. No RSS, game, Prayer, Calendar, Clock or Gallery source was reactivated. Source photographs and Windows Start pins were retained. Existing source pins need reconfiguration or manual removal; a settings reset does not remove or rearrange them.

## Installed behavior

- RSS and Prayer notifications mean desktop popups/tray balloons, using the same backend as Notify when generation starts. They are separate from Live Tile updates, disabled per tile by default, and silent by default. RSS supports a new-item threshold, cooldown and a quiet initial baseline. Prayer supports event selection, lead time, General/Reminder/Alarm and independent sound. Application notification API selection supports Auto, Toast and legacy Balloon; pin-all progress and operation/source transition notices use that backend.
- Prayer uses the Clock-sized main time, defaults to AM/PM, and puts prayer name and location in the same Secondary style. Sunrise/Shuruq is included in the timetable, with independent next-event and alert selection.
- Clock/world-clock tiles and optional local-file Calendar agendas are independent sources. Calendar is date-only until a supported local ICS file is explicitly selected. Overseas Wuthering Waves notes use an explicitly supplied SDK access token; other Kuro games and Chinese-region accounts are not claimed.
- The primary Wallpaper fallback uses the original embedded Desktop artwork and geometry. Gallery keeps its separate Photo topic. Fresh profiles are Desktop-only; explicit Set up Desktop tile / --setup-desktop performs one main-package bootstrap and relaunches with package identity. Ordinary generation does not repeatedly register it.
- New Discord tiles use the verified standalone defaults: Details is the foreground window title; State shows CPU and RAM used/total/percentage. Existing explicit values are preserved unless the user requests a reset.
- Gallery exact-image activation uses the Windows-reported clicked notification and ownership-checked opaque records. It never guesses the last submitted image. Static Registration, missing metadata or a changed original uses an explained folder/settings fallback. Actual shell click acceptance remains to be checked.
- Gallery configuration batches INI writes without changing ledger semantics. An identical complete image queue can be skipped only after all native slot acknowledgements and durable ledger commit. Force refresh, restart, configuration, lifecycle, epoch and rendering-context changes invalidate that proof. Larger pools continue advancing. Shrink behavior defaults to unpinning removed owned IDs, notifying, and no extra application confirmation; these are configurable.

References and configuration examples are in [the source/alert guide](tile-sources-alerts-2026-10-23.md), [setup guide](desktop-host-setup-2026-10-23.md), [Clock/Calendar guide](clock-calendar-2026-10-23.md), [WuWa guide](wuthering-waves-notes-2026-10-23.md), [Gallery clicks](gallery-image-clicks-2026-10-23.md) and [repeat suppression](gallery-identical-queue-2026-10-23.md).

## Validation and known retained failure

Final Core host tests passed 24,669 checks; combined new-source host tests passed 150; revision tests passed 466; Gallery lifecycle tests passed 295; no-hardware tests passed 3; source guards passed 1,927. Focused adapters/models also passed their recorded source, notification, image, INI and timing tests. Product and validation executables were scanned before execution. The later content-only offline smoke passed composition, adaptive rendering, hidden-text restoration, invalid-input and concurrent-write checks.

The full offline smoke **failed** the strict legacy Medium static-preset equality check: four unchanged-caption antialiasing pixels differ when an unsupported secondary input changes. The current PNG pair, coordinates and colors are identical to the historical defect. The legacy renderer/layout/specification are unchanged; the failing profile has no typed sources and does not reach the new rendering branches. No renderer change, cache workaround or tolerance relaxation was made. This known pre-existing defect remains open and was not used to hold the independently validated fixes indefinitely. Read [the preserved recurrence evidence](render-determinism-recurrence-2026-10-23.md).

## Installation and backup

Final installed SHA-256 values:

| Artifact | SHA-256 |
| --- | --- |
| DesktopStub.exe | 0B6DEA386BED46266F12B10A99DCD4AD2A67E86A34974429667F30451C8A741B |
| DesktopStubLiveTileBroker.exe | F254C65B867CD359AD096DB6E57F439790FE4E31AEE437BC63DA8B06474E3898 |
| DesktopStubTileLauncher.exe | 057AFB6B49C72321419FA4C9550D0E60D5A05EFC492E88243659C0EB682923C3 |

The staged fresh INI hash before normal runtime state writes was 25944D78727E71104A23496344F1F86BFC16F0D491DD462687A0CFB6B90F315F.

The complete pre-reset settings/accounts/gallery history/alert sidecars, click records when present, old binaries and main manifest are archived under DesktopStub/build/SettingsResetBackup-20261023-080729-a630cc. Its backup-hashes.json records saved file lengths and hashes. An earlier failed attempt also retained SettingsResetBackup-20261023-080610-4d6dc2.

The first installer attempt failed because PowerShell bound the null File.Replace backup argument to an empty path. Recovery restored the original profile/binaries, verified the original host hash, and relaunched PID 57348. The replacement call was corrected to use an explicit backup path and verified on private files before retrying. Ten injected deferred-recovery checks cover a slow resident exiting after a failed installation stops waiting; this helper never forces a process to stop or creates a persistent task.

The successful retry scanned and hash-checked all three canonical binaries immediately before native setup. Unpackaged setup PID 24284 registered the existing main package, then relaunched packaged PID 28228. Logs recorded COM registration success, deferred publication until identity was available, then an actual successful primary Live Tile publication and one-frame successful batch. The packaged resident remained stable for 75 seconds, with no old source/gallery target publication in that window. This verifies native submission; it does not certify Windows' visible Start rendering or native popup/click behavior.

Evidence is under build/review-validation: sources-reset-oct23-install-retry.log, sources-reset-oct23-passive-metrics.json, resume-oct23-preset-final-ContentRuntimeTests-tests.log, resume-oct23-preset-final-ContentNewSourceHostTests-tests.log and resume-oct23-final-content-smoke.log. Default smoke never used package-integration opt-in; actual installation was a separate explicitly authorized operation.

## Short resource sample

Thirty two-second samples over 60.69 seconds on the 12-logical-processor laptop measured mean CPU 0.0687%, highest interval 0.1931%, private bytes 4.77–5.01 MiB, and working set 11.34–11.48 MiB for the fresh Desktop-only resident. This supports a low active-module baseline, but is neither a controlled comparison with the older 4 MiB version nor a lock/resume, large-gallery or long-duration peak test. Working set and private bytes are different counters. Enabling sources adds provider state, image/text caches, parsed data and optional runtime work; disabling them does not promise bit-for-bit equivalence to the old executable.

## Remaining acceptance and suggestions

The historical raster issue, actual Win+L spike attribution, controlled old/new memory comparison, live account/API acceptance, notification UI, Registration manual pin migration and chaseable Gallery click behavior remain explicitly unverified. Private fixtures do not claim these. The user's lower-priority font selection, lyrics, weekly Calendar timetable, supported fitness API feasibility and Battery suggestions remain in [the follow-up list](tile-followups-2026-10-23.md); they are not included in this installation.
