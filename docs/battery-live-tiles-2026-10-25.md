# Battery live tiles

Add one through **Tiles > Add tile > Battery** in the tray or Properties.
Existing profiles and fresh Desktop-only defaults do not acquire a Battery tile
automatically. Source options are independent for each Battery tile. All are
available in both GUIs, its INI, and `--set Tile.<id>.Battery.<key>=<value>`.

## Presentation

Small shows charge only. Medium shows charge, status, supplied runtime and wear.
Wide adds a separate right-hand metric; Large also has a full-width detail area.
The ordinary corner defaults to current energy in Wh. Shared text controls
govern the charge/body/corner numeral roles; name and corner-logo branding
follows the ordinary independent-tile default.

The critical view orders charge, runtime, then the configured action sentence.
At 25% charge with Windows critical level 20%, its default corner reads **5**:
`max(0, current charge - configured critical level)` percentage points. It does
not mean five minutes or five percent charge. No runtime is extrapolated from
this difference. The runtime comes only from Windows' usable estimate.

Windows reserve is the default activation threshold. `CriticalVisibility` can
instead be `Threshold` (custom charge percentage), `Margin` (percentage points
above Windows critical level), `Always`, or `Off`. Always does not depend on
reserve being usable; separate AC/DC enablement still applies. Unknown source
does not silently select DC policy.

The default sentence is `Critical battery action: %action%`. Set
`CriticalActionTemplate=Your computer is about to %action%` for the requested
wording. The configured critical percentage is not a guaranteed action boundary:
firmware and individual batteries can trigger an earlier action. DesktopStub
only reads policy; it never changes it or initiates a power action.

## Options

Every key below has the `Battery.` prefix inside `[Tile.<id>]`.

| Keys | Default / accepted values |
| --- | --- |
| `ShowStatus`, `ShowRuntime`, `ShowWear`, `ShowDetails` | `1`; boolean |
| `NumberDecimals` | `1`; `0..3`, trailing zeros removed |
| `RightColumnPercent` | `30`; `20..45` |
| `CornerMetric` | `CurrentCapacityWh`; also `FullCapacityWh`, `DesignCapacityWh`, `WearPercent`, `Percent`, `RuntimeMinutes`, `CriticalDelta`, `None` |
| `CornerLabel` | blank uses automatic metric/unit caption |
| `CriticalVisibility` | `Reserve`; also `Threshold`, `Margin`, `Always`, `Off` |
| `CriticalThresholdPercent`, `CriticalMarginPercent` | `20`, `5`; `0..100` |
| `CriticalOnAC`, `CriticalOnDC` | `0`, `1`; boolean |
| `CriticalCornerMetric`, `CriticalCornerLabel` | `CriticalDelta`, automatic caption; same metric choices |
| `TitleTemplate`, `StatusTemplate` | `%charge%`, `%status%` |
| `RuntimeTemplate`, `WearTemplate` | `%runtime% remaining`, `Wear level: %wear%` |
| `CriticalActionTemplate` | `Critical battery action: %action%` |
| `UnknownText`, `NoBatteryText` | `Unavailable`, `No battery` |
| `ACLabel`, `DCLabel` | `Plugged in`, `On battery` |
| `ChargingLabel`, `NotChargingLabel`, `DischargingLabel` | `Charging`, `Not charging`, `Discharging` |
| `ActionNoneLabel`, `ActionSleepLabel`, `ActionHibernateLabel`, `ActionShutdownLabel` | `Do nothing`, `Sleep`, `Hibernate`, `Shut down` |
| `RefreshSeconds` | `60`; `15..86400` |
| `DeviceDetails` | `1`; disable to skip device capacity/wear queries |
| `CollectionTimeoutMs`, `DeviceTimeoutMs` | `1500`, `250`; `100..10000`, `25..2000` |

Technical acquisition controls are under Advanced source settings. Critical
behavior and text/labels have separate submenus. Presentation-only edits reuse
the prepared reading. `ShowStatus` controls the action sentence in critical mode;
`ShowDetails` controls its ordinary power-status detail on Large.

Template tokens: `%charge%`, `%percent%`, `%status%`, `%runtime%`, `%wear%`,
`%capacitywh%`, `%fullcapacitywh%`, `%designcapacitywh%`, `%critical%`, `%reserve%`,
`%criticaldelta%`, `%action%`, `%scheme%`, `%source%`, `%batteries%`, `%saver%`.
Use `%%` for a literal percent and `\n` for a line break. Unknown tokens and
unbounded/control text are rejected. An unavailable runtime uses `UnknownText`
directly, without a misleading "remaining" sentence.

This private configuration-only example queries no hardware and creates no pin,
package or resident:

```powershell
DesktopStub.exe --ini C:\Tests\Battery.ini --configure-only `
  --set 'Tiles.Instances=battery' `
  --set 'Tile.battery.Type=Battery' `
  --set 'Tile.battery.Battery.CriticalVisibility=Margin' `
  --set 'Tile.battery.Battery.CriticalMarginPercent=5' `
  --set 'Tile.battery.Battery.CriticalActionTemplate=Your computer is about to %action%'
```

## Data and acceptance boundaries

The collector uses documented read-only Windows APIs. Absolute mWh capacities
become Wh; relative units never become invented energy. Wear is the clamped
full/design capacity ratio. Aggregate capacities and wear require complete,
comparable multiple-battery readings, not a partial subset. Zero charge is
distinct from no battery and unknown. Unknown runtime is never estimated.

Sampling runs on a demanded source worker, not on the UI/shared polling thread.
Driver waits are bounded, with retained buffers until canceled I/O completes.
Synchronous Windows/driver entry points cannot promise an absolute wall-clock
deadline. There is no extra cleanup thread or permanent sampling timer.

Windows 10 adaptive tiles use semantic text. Registration and older/preset
delivery use the same layout in bitmaps; old-template size limits still apply.
Windows chooses when Start displays an accepted update, so sampling intervals
are not a visible-refresh guarantee.

Validation uses synthetic policy/device readings and private offscreen images;
it does not access real battery hardware or alter the active profile. Native
API evidence, Battery Mode source comparison and limitations are documented in
[the collector notes](battery-native-collector-2026-10-25.md).

## October 25 validation

The portable model passed **153 checks**, the injected collector **38**, the
combined host **1,365**, and the source guard **2,273**. Native syntax and a
retained non-inert link witness passed; the latter's empty main calls no power
API. Fresh fixture and product binaries scanned clean before execution.

The combined host exercises real menu commands for every Battery field,
injected collection and presentation-only cache reuse, the 25/20-to-5 example,
all four native/adaptive size bindings, normal/doubled-DPI offscreen bitmaps,
Registration body parity, text-off/per-size flags, corner placement and older
image-only delivery. A fixture filename bug was corrected before its final pass;
the native collector also resolves action indices through possible-value
metadata instead of confusing UI indices with underlying `POWER_ACTION` values.

Compiled configuration-only tests round-tripped a private Battery INI, rejected
three invalid writes without changing it, assigned no resident identity, and
verified that the real active INI was unchanged. The optimized main binary is
SHA256 `9882A3E2DCE85B8E2512B92C48AB32A45DF5DAC093F192CC206C62CEF747C738`.
Evidence is under `build/review-validation/battery-oct25-*`. Real battery-driver
and active power-policy acceptance was not exercised by these tests.

The guarded installation completed with backup
`DesktopStub/build/SettingsBackup-Battery-20261025-185437-27c521`. Installed
binaries scanned clean, the existing package published a successful tile batch,
and the same resident passed 75 seconds of health. The Main identity, manifest,
settings/accounts/history and pins were retained. No Battery tile was added or
pinned automatically, and no battery source was activated by this installation.
The native health check therefore verifies the app's existing tile delivery;
Battery readings/rendering are covered by the injected and offscreen gates.
