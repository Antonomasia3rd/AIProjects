// ==WindhawkMod==
// @id              logonui-tweaks
// @name            LogonUI Tweaks
// @description     Configurable native-style tweaks for Windows LogonUI system controls and user list
// @version         0.1.0-alpha5g
// @author          Vivo GKB
// @github          https://github.com/pancakes.v2027
// @include         logonui.exe
// @architecture    amd64
// @compilerOptions -ladvapi32 -lole32 -loleaut32 -lruntimeobject -lgdi32 -lwtsapi32
// @license         MIT
// ==/WindhawkMod==


// ==WindhawkModReadme==
/*
# LogonUI Tweaks

## alpha5g source candidate

This workspace candidate validates the actual SystemProvider image on every
startup and fixes battery-watcher rollback after partial initialization failure.
It has passed inert x64 tests and a local build; those checks do not establish
runtime compatibility. The inherited XAML cleanup-timeout issue remains open,
so this candidate is not ready to install or hot-reload. It does not yet add the
historical Back action. The installed alpha5f source remains a separate baseline.

Adds configurable native-style tweaks to Windows LogonUI while keeping the
underlying behavior as close to Windows itself as practical. The original
Battery/Volume/Clock system-control module is joined by an independently fail-closed
user-list status module.

## Features

- Restores per-user signed-in status in the modern user list. The default
  `Legacy AuthUI-style` mode is now process-local snapshot-based: after the
  first account rows are realized, further WTS connect/disconnect/lock/login
  events do not automatically mutate those account status rows. This is an
  approximation of legacy AuthUI behavior, not a claim of exact Windows 7/8/8.1
  implementation parity. Enable `Non-native user-list customizations ->
  Continuously refresh existing account status rows` to restore alpha4u's
  live WTS behavior. Native transient messages and anonymous-session Footer
  are independent non-native options, disabled by default for new settings.
  `Legacy AuthUI-style` reconstructs `Signed in`, `Locked`, and
  `Signed in remotely from <client>` from native Windows session state. RDP
  lock/unlock notifications are retained as an event-backed lock-state override
  because WTSSessionInfoEx can lag the corresponding WTS event.
  In this mode, membership in the native WTS session table is authoritative
  for whether the status row exists, avoiding delayed `UserViewModel.IsLoggedIn`
  transitions during live logon/logoff while the user list stays visible.
- An optional `Windows 11 UserViewModel` mode keeps the current-OS local
  `UserViewModel.LogonStatus` behavior while still restoring the remote-client
  text that modern LogonUI no longer exposes.
- Active RDP users are matched by `UserViewModel.QualifiedName` against the
  native WTS session table.
- Patched user-list rows render DisplayName, status, qualified name, and the
  48-DIP profile picture from one UserViewModel snapshot. A per-presenter
  `DataContextChanged` watcher re-arms the bounded repair bridge when ListView
  virtualization retargets a realized container, preventing stale row ownership.
- Optional always-visible qualified account names (`DOMAIN\User` /
  `COMPUTER\User`) and optional compact unsigned-user rows. The qualified-name
  forcing option is off by default; when disabled the display respects
  UserViewModel.IsDomainNameVisible while still using the full account name
  internally for WTS session matching.
- Optional custom account-row ordering: display-name, qualified name, and
  steady-state status can be shown in any valid order, including conditional
  two-line presets for signed-in vs signed-out accounts. A custom token can
  replace the visible status line with a literal non-native message, optionally
  overridden by exact qualified account name. Windows-owned view-model values
  are never mutated. Off by default, including for upgrades that use defaults.
- Optional per-user session detail rows. `Summary` and `Full diagnostics`
  retain their existing WTS/UserViewModel semantics. `Custom` adds independent
  opt-in controls for nine WTS and UserViewModel diagnostic lines, with no
  change to the main status source. All extra details remain off by default.
- Optional read-only native UserViewModel.PropertyChanged observer for RE.
  On each realized account presenter it uses the documented WinRT
  Windows.UI.Xaml.Data.INotifyPropertyChanged interface, records native property
  names (with account association and per-subscription event caps), and does
  NOT change or refresh UI status. The probe is off by default; enable file
  logging to capture transitions on the secure desktop. Subscriptions are
  detached on presenter Unloaded, row DataContext replacement, and hot-unload
  on the XAML thread.
  The presence of this interface does not guarantee LogonStatus emits events.
  A failed hot-unload XAML rendezvous must be treated as an unsafe cleanup
  diagnostic rather than assuming event detachment succeeded.
- alpha5c adds opt-in adaptive coalescing for redundant XAML lifecycle
  user-row repairs. It never skips a new repair generation's first traversal,
  retains the initial realization/virtualized presenter observers and all
  legacy AuthUI-style snapshot semantics, preserves native transient priority,
  and settles only after two stable post-grace passes. The optimization is
  disabled by default pending real Windows measurements; a diagnostic log
  summarizes executed clean/dirty passes and callbacks suppressed by its
  bounded cooldown. It does not change native Windows credential state.
- alpha5d adds an opt-in, read-only identity comparison to distinguish
  duplicated XAML traversal scopes from genuinely separate user-row presenters.
  It piggybacks on existing repair walks, does not poll or change the repair
  schedule, and logs only when a bounded per-root identity set changes.
  Disabled by default. Identity overlap alone does not authorize skipping
  root-specific work or modifying Windows-owned elements.
- alpha5e adds an opt-in, fail-closed per-presenter reuse experiment.
  Both XAML roots still traverse their rows; only very recent, fully
  validated unchanged results may be reused when exact COM identity overlap
  has been proven within the current lifecycle generation. No timers, new
  hooks or native Windows control mutation are introduced; off by default.
- On the validated Windows 11 25H2 build, exact transient LogonUX status text
  (such as `Welcome`, `Preparing Windows`, and `Please wait for ...`) is
  observed read-only from the native `StatusMessage` TextBlock and bridged
  across LogonUI processes by WTS session ID. While a live native transient is
  present it outranks reconstructed `Signed in` / `Locked` / `Disconnected`
  text; when Windows clears/unloads the native message, the normal WTS-backed
  steady-state semantics resume. The bridge retains the last proven session identity
  through native sign-out long enough to carry `Signing out`, and a new WTS session
  incarnation resets that remembered identity. No `RootViewModel::DisplayStatus`
  detour is used. alpha4k added a status-only UI fast path so bridge updates
  mirror the latest native StatusMessage without rebuilding every user row.
  alpha4u eliminates the separate lower pending-status overlay (which
  could overlap Windows' native Welcome and was invisible for multiple
  concurrent unassigned sessions). Pending sessions now show native text
  only in their existing mod-owned ListView Footer Status: field; the newest
  session is listed first. Once WTS assigns a user, that user's existing
  status row is the only mod-owned destination for native text. Footer
  visibility depends on the native ListView viewport; no scrolling or
  virtualized-row insertion is forced.
  alpha4t closes a disconnected-status filtering gap in the full user-row
  refresh path: the same generic native "Please wait"/"Locking" exclusion
  now applies during live identity lookup, and WTSDisconnected cannot be
  overridden by a temporarily stale writer snapshot. Exact native service
  messages and "Signing out" remain supported. No synthetic delays are used.
  alpha4s extends pre-identity diagnostics to live, nonterminal LogonUI
  sessions with no WTS-assigned username, including local console sign-in and
  the first sign-in after boot. Transport is labelled RDP, local console, or
  unknown from observed WTS evidence; it never calls protocol=0 remote. The
  existing Windhawk setting key is retained for backwards compatibility.
  alpha4l presents the optional pre-identity remote diagnostic through the
  supported UsersListView/ListViewBase Footer property instead of mutating the
  virtualizing ItemsStackPanel. alpha4n promotes the exact pending native
  StatusMessage into that Footer's headline so short-lived messages remain
  visible before an account row exists, and revalidates live transient text at
  row-apply time so an older full-refresh snapshot cannot overwrite a newer
  status/clear publication. alpha4o gives exact native transient bursts
  scheduling priority over structural/WTS repair passes, deferring the
  expensive six-row lifecycle walk until native StatusMessage text clears.
  It also mirrors a pre-identity pending status into an owned ListViewBase
  Header so the exact Windows text remains visible near the top of a long
  diagnostic user list while the account identity is still unavailable.
  alpha4p fixes the duplicated fallback Header/Footer label: the Header now
  exists only for a nonempty native message, while the Footer retains session
  diagnostics. The Header follows its exact originating RDP session across
  the WTS account-identity handoff until that process reports a native
  clear/unload, preventing an off-screen user row from prematurely replacing
  the only visible text. Optional bounded CompositionTarget.Rendering traces
  compare XAML render-pass timing against text-property/console-apply times;
  no arbitrary display dwell, fake Windows status, UpdateLayout, or synthetic
  LogonUX model is introduced. alpha4p1 fixes WinRT UInt32 boxing for MinGW
  (DWORD is unsigned long, whereas C++/WinRT UInt32 uses uint32_t).
- Live Battery icon using Windows-owned LogonUX battery state.
- `Automatic` Battery visibility follows Windows' native
  `IsBatteryStatusVisible` decision; Always show and Hide are also available.
- Genuine classic Windows battery flyout.
- Live master-volume icon using event-driven Core Audio notifications.
- Genuine `SndVol.exe -f` compact volume flyout and classic Volume Mixer.
- Mouse-wheel and precision-touchpad two-finger scrolling over the Volume
  button to adjust master volume, with configurable Windows 10-style or
  Windows 8-style mute/unmute behavior.
- EarTrumpet-style Volume middle-click toggles mute by default.
- Battery middle-click is independently configurable (Advanced Power Options,
  classic battery flyout, or no action).
- Optional Clock button with taskbar-style locale-formatted time + short date; seconds/date are configurable.
- Clock left-click uses the genuine legacy CTrayClock/ClockFlyoutWindow
  backend through timedate.cpl's own COM class factory. A harvested x64
  timedate.cpl/PDB corpus supplies tested fast paths across Windows 10/11;
  unknown builds are accepted only when Windhawk symbols uniquely resolve the
  same CTrayClock ShowFlyout/HideFlyout ABI and the live COM vtable matches.
- Mouse-wheel and precision-touchpad two-finger scrolling over the Battery
  button to adjust internal-display brightness. The WMI backend matches
  `WmiMonitorBrightness` and `WmiMonitorBrightnessMethods` by `InstanceName`
  and adjusts all active internal brightness-capable displays.
- Precision-touchpad / high-resolution wheel streams use a shared percentage
  step (1% by default) plus a separately configurable delta threshold
  (30 units by default) so fine 1% changes don't make scrolling feel sluggish.
- Per-control visibility for Windows-owned IME mode, Input/language,
  Network sign-in, Disconnect, Network, Accessibility, and Power controls.
- Optional Battery and Volume percentage labels with configurable font, size,
  position, spacing, and formatting.
- Configurable button order.
- Independent placement controls for the Battery flyout, compact SndVol,
  Volume Mixer, and Advanced Power Options.
- Direct Advanced Power Options hosting for the classic battery flyout on the
  secure desktop.
- Hot-disable teardown detaches persistent XAML/Core Audio callbacks on the
  LogonUI UI thread and restores the native button strip before the mod DLL is
  unloaded. Process-local helper window classes are explicitly unregistered so
  a later hot-load cannot reuse a stale WNDPROC from an unloaded mod image.
- Optional shared file logging is useful for console/Secure Desktop sessions
  whose Windhawk output isn't visible to the interactive user. Its full path
  is configurable and defaults to
  `%PUBLIC%\Desktop\LogonUITweaks.log`, alongside the default
  diagnostic XAML-capture directory.
- Settings are grouped into Battery, Volume, shared scrolling/percentage
  appearance, Windows-owned buttons, layout, and diagnostics. Saving settings
  requests a controlled mod reload so visual and behavioral changes apply
  consistently to an already-running LogonUI.

## Native behavior

The default user-list status mode is modeled after the older AuthUI distinction
between `Signed in`, `Locked`, and remote-client status, while using current
Windows WTS session information as the backend. `Locked` is reserved for the
active console session when native WTS state reports that session locked;
other matched local signed-in sessions show `Signed in`. The optional Windows
11 mode instead uses `UserViewModel.LogonStatus` for local users.

Primary left-click behavior remains deliberately native-oriented:

- Battery left-click opens the genuine classic Windows battery flyout.
- Volume left-click launches `SndVol.exe` with the configured parameters.

Secondary input is configurable: Volume middle-click can toggle mute and
Battery middle-click can open Advanced Power Options or the classic battery
flyout.

This preserves Windows-version-specific SndVol behavior. For example, newer
Windows versions can open the compact `-f` flyout while the full Volume Mixer
is already open, while older versions may instead focus the existing Mixer.

## Live updates and input

Normal operation does not poll continuously:

- Volume state uses Core Audio callbacks.
- Battery percentage/state uses Windows power notifications.
- Optional Battery flyout live refresh uses a one-shot debounce after an
  actual power notification.
- Wheel/touchpad volume adjustment reacts only to routed pointer-wheel events.
- The optional Clock display uses a XAML DispatcherTimer (1-second tick, with
  text updated only when the formatted value changes).

Traditional mouse-wheel detents use the per-control defaults: 2% for Volume
and 10% for Battery brightness. Precision-touchpad / high-resolution wheel
streams use a shared 1% step every 30 accumulated wheel-delta units by
default, which is roughly four 1% changes per traditional 120-unit detent of
finger travel. Windows doesn't expose a reliable "this wheel event came from
a touchpad" flag here, so the mod classifies high-resolution streams from
their wheel-delta pattern.

Rapid WMI brightness gestures keep a short-lived desired-brightness cache
per active internal display so multiple fine scroll packets build on the last
requested value instead of a possibly stale WMI read. The monitor/method
instances are paired by `InstanceName`, and the cache automatically
resynchronizes after an idle pause.

The button-order parser is defensive: duplicate/unknown tokens are ignored and
an accidental leading `buttonOrder:` label is tolerated rather than silently
moving the first valid control to the end.

## Compatibility

The intended support floor is **Windows 10 1809 (17763) and newer**. Older
Windows releases use substantially different LogonUX/XAML implementations and
aren't supported by the structural SystemProvider resolver.

The **system-controls** feature set keeps that compatibility policy. The new
**user-list status** feature currently has a stricter private-ABI gate and is
validated only for the supplied Windows 11 25H2 build 26200.6584
(`Windows.UI.Logon.dll` timestamp `0xF49AD882`, SizeOfImage `0x00402000`). On
other builds it fails closed without disabling Battery/Volume or native-button
features.

The mod has no Windows-build allowlist. SystemProvider hook discovery always
validates the actual loaded AMD64 image using the unique structural resolver,
with matching symbols as a fallback. OS build and UBR are diagnostic only.
Other internal features retain their independent image/ABI validation policies.
An unresolved feature fails closed instead of guessing an address.

**End-to-end runtime-tested builds:**

- Windows 10 1809 / build 17763.2114
- Windows 10 22H2 / build 19045.7725
- Windows 11 25H2 / build 26200.6584
- Windows 11 25H2 / build 26200.9445

The final hot-load/hot-unload lifecycle was additionally stress-tested on
Windows 10 22H2 with a persistent LogonUI process, including rapid repeated
enable/disable cycles and the language/IME selector's nested UI state.

**Offline structurally validated implementation families:**

- 17763
- 18362
- 19041
- 22000
- 22621
- 26100

For the classic Battery flyout, unknown `stobject.dll` builds first use a
symbol-free structural resolver that recognizes Windows' own BatMeterFlyout,
BATTERY_STATE, and Power Options relationships; symbols are only a fallback if
that validation fails. The SystemProvider resolver validates a unique x64
runtime-function start and implementation shape before hooking.

The genuine classic Clock flyout is backed by a harvested x64
`timedate.cpl`/public-PDB corpus. 110 PDB-backed images independently confirmed
an unchanged `CTrayClock::ShowFlyout(HWND, const RECT*)` ABI and stable COM
vtable slots 3/4 for ShowFlyout/HideFlyout, spanning Windows 10 10240 through
modern Windows 11 images in the corpus. Known image identities use validated
RVA fast paths; unknown images are accepted only when Windhawk symbols resolve
the same methods uniquely and the live COM object's vtable matches those
resolved addresses. Otherwise the Clock flyout fails closed.


## Notes

- The percentage text is an intentional non-native addition. It uses `Segoe UI`
  by default and can be changed in Settings.
- `Automatic` Volume renderer follows the native generation where possible and
  can fall back to Windows SndVol resources.
- The optional event-driven classic Battery flyout refresh rebuilds the genuine
  flyout after a power event. On some versions this can produce a brief visual
  refresh/flicker.
- Battery brightness scrolling prefers the Windows `ROOT\\WMI`
  `WmiMonitorBrightness` / `WmiMonitorBrightnessMethods` interface, pairs
  active value/method instances by `InstanceName`, and adjusts every matched
  internal brightness-capable display. It uses WMI's Automation-compatible
  integer types for method parameters, with the legacy `\\.\LCD` backlight
  IOCTL path as a fallback when WMI isn't usable.
- Windows 11 can keep the Input/language button visually empty until a
  credential text box is activated. The mod identifies the containing
  `LogonUX.InputSwitchUserControl` up front, so visibility/order settings do not
  depend on the inner button having been lazy-loaded yet. Dormant IME/Input
  slots collapse instead of leaving blank gaps when reordered. External spacing
  before a reordered Input/standalone-IME control is configurable and defaults
  to Automatic, derived from the native Power button's left margin so Windows
  10 and Windows 11 keep their own generation-specific spacing. An adjacent
  IME+Input pair is treated as one group, and the narrow IME-mode glyph has a
  separate visual-only X offset so it can be tightened without shrinking the
  native hit target.
- Windows 11 exposes the IME mode indicator as a separate Windows-owned
  `LogonUX.ImeModeIndicatorUserControl` with its own visibility/order token.
  The tested Windows 10 1809 and Windows 10 22H2 layouts have no separate
  IME-mode container; their language/IME state is represented by
  `LogonUX.InputSwitchUserControl` alone.
- On the tested Windows 10/11 layouts, the two auxiliary direct-button slots
  immediately before Network are exposed as `network-signin` and `disconnect`.
  Unknown extra native slots remain available as `native1`, `native2`, etc.
- Diagnostic logging is disabled by default. Normal logging keeps resolver,
  backend, layout, and error information concise. `Verbose diagnostics`
  enables the large native-button/XAML-tree traces used for reverse
  engineering and can write through either Windhawk logging or shared file
  logging. Shared-file logging defaults to
  `%PUBLIC%\Desktop\LogonUITweaks.log`; its path is
  configurable and the file isn't automatically rotated. Diagnostic XAML
  capture is independent, captures only the SystemProvider subtree, and
  defaults to `%PUBLIC%\Desktop`; its output directory is configurable.
- Transient `SystemProvider.Unloaded` events are treated as non-terminal:
  the injection session and its callbacks are retained and revalidated on the
  matching `Loaded` event. Callbacks are detached when a session is actually
  retired or during hot-unload. Duplicate PowerOptions lazy-load retries for
  the same SystemProvider instance are coalesced.
- Hot-load/reload recovery uses both queued and sent HWND messages plus
  GETMESSAGE/CALLWNDPROC rendezvous hooks, with a unique probe cookie for each
  attempt. It fans probes out to active/focused/fallback HWNDs and the thread
  queue, then performs bounded delayed rescans if a transient nested UI (such
  as the language/IME selector) still delays the first probe.
- Native Windows controls are never removed/reinserted merely to implement
  keyboard traversal. Visual order uses Grid.Column and keyboard order uses
  TabIndex, avoiding native Unloaded/Loaded side effects during hot-load and
  hot-unload.
- Saving settings requests a controlled Windhawk reload of the mod. This uses
  the same hot-unload-safe teardown path and ensures button visibility, order,
  renderer, placement, diagnostics, and interaction settings all take effect
  immediately on an already-running LogonUI.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- battery:
  - visibility: automatic
    $name: Battery button visibility
    $description: >-
      Automatic follows Windows' native LogonUX battery availability. Always
      show keeps the Battery control visible even when Windows reports no
      battery. Hide removes the injected Battery control entirely.
    $options:
    - automatic: Automatic (follow Windows)
    - show: Always show
    - hide: Hide
  - clickAction: open
    $name: Click action
    $description: >-
      Open the genuine classic Windows battery flyout, or disable clicking.
      No synthetic toggle behavior is applied.
    $options:
    - open: Open classic battery flyout
    - none: Do nothing
  - middleClickAction: advanced
    $name: Middle-click action
    $description: >-
      Action for pressing the mouse wheel over the Battery button. Advanced
      Power Options is a useful secondary Battery action and is the default.
    $options:
    - advanced: Open Advanced Power Options
    - open: Open classic battery flyout
    - none: Do nothing
  - brightnessScroll:
    - action: adjust
      $name: Wheel / touchpad scroll action
      $description: >-
        Adjust brightness from a physical mouse wheel or precision-touchpad
        two-finger vertical scroll while the pointer is over the Battery
        button. The WMI backend adjusts all active internal brightness-capable
        displays by matching their InstanceName values; the legacy LCD backend
        remains a fallback.
      $options:
      - none: Do nothing
      - adjust: Adjust display brightness
    - stepPercent: 10
      $name: Brightness step per mouse-wheel notch (%)
    - reverse: false
      $name: Reverse brightness scroll direction
    $name: Brightness scrolling
  - percentage:
    - show: true
      $name: Show Battery percentage beside icon
    - position: right
      $name: Percentage position
      $options:
      - right: Right of icon
      - left: Left of icon
    - hideAt100: false
      $name: Hide Battery percentage at 100%
    $name: Percentage
  - flyout:
    - placement: button
      $name: Classic battery flyout placement
      $options:
      - button: Anchor to Battery button
      - windows: Windows default
    - anchorSide: automatic
      $name: Anchor side
      $options:
      - automatic: Automatic (prefer above)
      - above: Above
      - below: Below
    - offsetX: 0
      $name: X offset (screen px)
    - offsetY: 0
      $name: Y offset (screen px)
    - refresh: windows
      $name: Live refresh
      $options:
      - windows: Windows default (refresh on open only)
      - event-driven: Event-driven rebuild while open
    - eventDebounceDelayMs: 500
      $name: Event debounce delay (ms)
      $description: One-shot debounce after a Windows power notification. This is not a polling interval.
    $name: Classic battery flyout
  - advancedPower:
    - action: advanced
      $name: '"More power options" action'
      $options:
      - advanced: Open Advanced Power Options directly
      - windows: Windows default
    - placement: battery
      $name: Advanced Power Options placement
      $options:
      - battery: Anchor to Battery button
      - windows: Windows default
    - anchorSide: automatic
      $name: Anchor side
      $options:
      - automatic: Automatic (prefer above)
      - above: Above
      - below: Below
    - offsetX: 0
      $name: X offset (screen px)
    - offsetY: 0
      $name: Y offset (screen px)
    $name: Advanced Power Options
  $name: Battery

- volume:
  - showButton: true
    $name: Show Volume button
  - clickAction: launch
    $name: Click action
    $description: >-
      Launch SndVol using the configured parameters, or disable clicking.
      SndVol itself decides what happens when a full Volume Mixer already
      exists, preserving version-specific Windows behavior.
    $options:
    - launch: Launch SndVol
    - none: Do nothing
  - middleClickAction: toggle-mute
    $name: Middle-click action
    $description: >-
      EarTrumpet-style mouse-wheel-button action for the Volume button.
    $options:
    - toggle-mute: Toggle mute
    - none: Do nothing
  - scroll:
    - action: adjust
      $name: Wheel / touchpad scroll action
      $description: >-
        Adjust master volume from a physical mouse wheel or precision-touchpad
        two-finger scroll while the pointer is over the Volume button.
      $options:
      - none: Do nothing
      - adjust: Adjust master volume
    - stepPercent: 2
      $name: Volume step per mouse-wheel notch (%)
    - unmuteBehavior: increase
      $name: Auto-unmute direction while scrolling
      $description: >-
        Increase reproduces the Windows 10-style behavior: scrolling toward
        higher volume while muted eventually unmutes, while scrolling toward
        lower volume stays muted. Never is Windows 8-style. Either/decrease
        are extra tweak modes.
      $options:
      - increase: Volume increase only (Windows 10-style)
      - never: Never (Windows 8-style)
      - decrease: Volume decrease only
      - either: Either direction
    - unmuteThresholdSteps: 1
      $name: Auto-unmute threshold (logical scroll steps)
      $description: >-
        Number of consecutive logical scroll steps in the configured direction
        required before a muted endpoint is unmuted. 1 matches the immediate
        Windows 10-style response. The counter resets when direction changes.
    - muteAtZero: true
      $name: Mute when scrolling reaches 0%
      $description: >-
        When enabled, reaching 0% through scrolling explicitly mutes the Core
        Audio endpoint. This avoids hardware/drivers which can still produce
        audible output at scalar volume 0.
    - reverse: false
      $name: Reverse Volume scroll direction
    $name: Volume scrolling
  - percentage:
    - show: true
      $name: Show Volume percentage beside icon
    - position: right
      $name: Percentage position
      $options:
      - right: Right of icon
      - left: Left of icon
    - hideAt100: false
      $name: Hide Volume percentage at 100%
    - showMutedText: false
      $name: Append "(muted)" to Volume percentage
    $name: Percentage
  - renderer: automatic
    $name: Volume icon renderer
    $options:
    - automatic: Automatic (recommended)
    - mdl2: Segoe MDL2 Assets
    - fluent: Segoe Fluent Icons
    - sndvol-auto: SndVolSSO automatic resource lookup
    - sndvol-dll: SndVolSSO.dll resources
    - sndvol-mun: SndVolSSO.dll.mun resources
  - uiHandoff: automatic
    $name: Volume live-update UI handoff
    $options:
    - automatic: Automatic (runtime capability detection)
    - dispatcherqueue: Windows.System.DispatcherQueue
    - coredispatcher: Windows.UI.Core.CoreDispatcher
    - legacy-message-window: Legacy message-only window
  - sndVol:
    - launchParameters: -f
      $name: SndVol launch parameters
    - compact:
      - placement: button
        $name: Compact flyout placement
        $options:
        - button: Anchor to Volume button
        - windows: Windows default
      - anchorSide: automatic
        $name: Anchor side
        $options:
        - automatic: Automatic (prefer above)
        - above: Above
        - below: Below
      - offsetX: 0
        $name: X offset (screen px)
      - offsetY: 0
        $name: Y offset (screen px)
      $name: Compact SndVol flyout
    - mixer:
      - placement: button
        $name: Full Volume Mixer placement
        $options:
        - button: Anchor to Volume button
        - windows: Windows default
      - anchorSide: automatic
        $name: Anchor side
        $options:
        - automatic: Automatic (prefer above)
        - above: Above
        - below: Below
      - offsetX: 0
        $name: X offset (screen px)
      - offsetY: 0
        $name: Y offset (screen px)
      $name: Full Volume Mixer
    $name: SndVol
  $name: Volume


- clock:
  - showButton: true
    $name: Show Clock button
  - showSeconds: false
    $name: Show seconds
  - showDate: true
    $name: Show date
    $description: >-
      Show the locale-formatted short date below the time, similar to the
      Windows taskbar clock.
  - clickAction: flyout
    $name: Click action
    $description: >-
      Classic flyout invokes Windows' own legacy CTrayClock/ClockFlyoutWindow
      backend from timedate.cpl. Tested timedate.cpl builds use validated
      corpus fast paths; unknown builds can fall back to matching symbols.
      The live CTrayClock vtable is always validated before private methods are
      called. The full Date and Time Control Panel remains an explicit
      alternative because LogonUI is privileged and that dialog exposes
      system-time settings.
    $options:
    - flyout: Windows classic clock flyout
    - timedate: Open classic Date and Time (experimental)
    - none: Do nothing
  - flyout:
    - placement: button
      $name: Placement
      $options:
      - button: Anchor to Clock button
      - windows: Windows default
    - anchorSide: automatic
      $name: Anchor side
      $options:
      - automatic: Automatic (prefer above)
      - above: Above
      - below: Below
    - offsetX: 0
      $name: X offset (screen px)
    - offsetY: 0
      $name: Y offset (screen px)
    $name: Classic clock flyout
  - fullDateTime:
    - placement: button
      $name: Placement
      $options:
      - button: Anchor to Clock button
      - windows: Windows default
    - anchorSide: automatic
      $name: Anchor side
      $options:
      - automatic: Automatic (prefer above)
      - above: Above
      - below: Below
    - offsetX: 0
      $name: X offset (screen px)
    - offsetY: 0
      $name: Y offset (screen px)
    $name: Full Date and Time dialog
  $name: Clock

- precisionScrolling:
  - stepPercent: 1
    $name: Precision touchpad / high-resolution scroll step (%)
    $description: >-
      Shared percentage step for Battery and Volume when the routed wheel
      stream behaves like a precision touchpad or high-resolution wheel.
      Traditional mouse-wheel detents keep each control's own step setting.
  - deltaPerStep: 30
    $name: Precision touchpad / high-resolution delta per step
    $description: >-
      Accumulated wheel-delta units required for each precision-scroll
      percentage step. Lower values are faster/more sensitive; higher values
      are slower. Traditional mouse-wheel detents always use the standard
      120-unit detent and are unaffected by this setting.
  $name: Precision scrolling (shared)

- percentageLabels:
  - showPercentSign: true
    $name: Show percent sign
  - fontFamily: Segoe UI
    $name: Percentage font family
    $description: Windows font-family name used by both percentage labels.
  - fontSize: 12
    $name: Percentage text size
  - spacing: 4
    $name: Icon/percentage spacing (DIP)
  $name: Percentage label appearance (shared)

- nativeButtons:
  - showImeMode: true
    $name: Show IME mode indicator
    $description: >-
      Controls Windows' separate IME-mode indicator container. It may remain
      visually empty unless the active input method exposes an IME mode.
  - showInput: true
    $name: Show Input / language button
    $description: >-
      Controls Windows' InputSwitchUserControl (for example ENG / US). On
      Windows 11 the inner language button can be lazy-loaded only after a
      credential text box is activated; this setting still targets the
      container before that.
  - showNetworkSignIn: true
    $name: Show Network sign-in button
    $description: >-
      Controls Windows' optional Network sign-in system button. It is normally
      hidden unless the current sign-in/network scenario needs it.
  - showDisconnect: true
    $name: Show Disconnect button
    $description: >-
      Controls Windows' optional Disconnect system button. It is normally
      hidden unless the current sign-in/network scenario needs it.
  - showNetwork: true
    $name: Show Network button
  - showAccessibility: true
    $name: Show Accessibility button
  - showPower: true
    $name: Show Power button
  - showOther: true
    $name: Show other Windows-owned system buttons
    $description: >-
      Controls any additional native slots that this mod can't identify as
      IME, Input, Network sign-in, Disconnect, Network, Accessibility, or
      Power.
  - imeInputSpacing: -1
    $name: Reordered IME / Input spacing (DIP)
    $description: >-
      Extra left spacing used when a visible Input container (or a standalone
      IME container) follows a different visible system control. -1
      automatically follows the native generation's system-button spacing
      (for example 8 DIP on the tested Windows 11 layout and 0 DIP on Windows
      10 1809). Values 0..32 force an explicit spacing. A visible adjacent
      IME + Input pair is treated as one native group.
  - imeModeVisualOffsetX: -4
    $name: IME mode glyph X offset (DIP)
    $description: >-
      Visual-only horizontal offset for the native IME mode glyph. Negative
      values move the glyph left without shrinking or moving its native hit
      target.
  $name: Windows-owned system buttons

- layout:
  - buttonOrder: battery,volume,clock,ime,input,network-signin,disconnect,network,accessibility,power
    $name: System button order (left to right)
    $description: >-
      Comma-separated order. Named tokens are ime, input, battery, volume, clock,
      network-signin, disconnect, network, accessibility, power, plus
      native1/native2/... when an unknown native slot is discovered.
  $name: System strip layout

- userList:
  - enabled: true
    $name: Enable user-list status tweaks
    $description: >-
      Enables the independently fail-closed user-list module. If the private
      Windows.UI.Logon UserViewModel ABI isn't validated on this Windows build,
      this module disables itself while the system-control tweaks keep working.
  - showStatus: true
    $name: Show status for signed-in users
    $description: >-
      Shows a status line for signed-in users. Active RDP users can show
      "Signed in remotely from <client>" when a matching WTS session is available.
  - showNativeTransientStatus: false
    $name: Mirror native transient messages into user rows (non-native)
    $description: >-
      Optional non-native live enhancement (off by default). On the currently
      validated Windows 11 build, temporarily prefer the exact
      LogonUX StatusMessage text Windows is already displaying (for example
      Welcome, Preparing Windows, Please wait for a service, or any other text
      Windows supplies there). The observer is read-only and uses a cross-process
      session bridge; it does
      not hook RootViewModel::DisplayStatus or synthesize transient wording.
  - showPendingRemoteAttempt: false
    $name: Show unassigned sign-in sessions (non-native)
    $description: >-
      Show a diagnostic Footer for live LogonUI sessions that have no WTS-assigned
      account, on local console sign-in (including first boot or Switch User)
      as well as incoming remote sign-in. Label the transport using WTS evidence;
      unknown does not mean remote. While native transient mirroring is enabled,
      show its exact text in the Footer Status: line. No floating overlay or
      fabricated user tile; remove the Footer after identity assignment or
      session termination. Enable native transient mirroring above to activate
      the cross-process observer and unassigned-session Footer.
  - statusBehavior: legacy-authui
    $name: Status behavior
    $description: >-
      Legacy AuthUI-style reconstructs the older Signed in / Locked / Signed in
      remotely from <client> semantics from native WTS session state. Windows 11
      UserViewModel keeps the current-OS local LogonStatus behavior while still
      restoring the remote-client text.
    $options:
    - legacy-authui: Legacy AuthUI-style (recommended)
    - windows11: Windows 11 UserViewModel
  - alwaysShowQualifiedName: false
    $name: Force qualified account name (non-native)
    $description: >-
      Disabled by default. Off respects the original Windows
      UserViewModel.IsDomainNameVisible value instead of forcing an extra
      COMPUTER\\User / DOMAIN\\User line on local sign-in rows.
      On forces that line to be visible. The full account name is still read
      internally for matching WTS sessions, regardless of this presentation
      setting. The existing setting key is retained for compatibility.
  - sessionDetails: off
    $name: Extra per-user session rows
    $description: >-
      Adds non-native WTS-backed detail rows for signed-in users. Summary shows
      a WTS-aware state (for example Locked or Disconnected), Session N, and a
      precise connection description such as Active console session, Local
      session (disconnected), or Signed in remotely from <client>. Full
      diagnostics additionally shows WTS state, station, protocol, lock state,
      and the current UserViewModel status snapshot. Custom displays only the
      individually selected lines under Custom row detail sources, without
      changing the main status text. Off preserves the normal three-line
      presentation.
    $options:
    - off: Off (native-style presentation)
    - summary: Summary (session ID + connection origin)
    - full: Full diagnostics
    - custom: Custom (select individual detail sources)
  - fallbackRefreshIntervalMs: 0
    $name: Safety refresh interval (ms)
    $description: >-
      Normal updates are event-driven through WTS session notifications. 0 disables
      periodic polling; values 1000-60000 enable an optional safety refresh.
  - compactUnsignedRows: false
    $name: Compact users that are not signed in
    $description: >-
      Experimental. Shrinks unsigned-in rows while leaving signed-in rows at the
      native Windows height. Disabled by default.
  - compactUnsignedRowHeight: 48
    $name: Compact unsigned-in row height (DIP)
    $description: >-
      Used only when compact unsigned rows is enabled. Clamped to 48-60 DIP because
      the stock user picture is 48 DIP.
  $name: User list

- rowLayout:
  - enabled: false
    $name: Enable unified user-row layout (non-native)
    $description: >-
      Off (default) keeps native-style rows and optional legacy diagnostics.
      On composes all row sources through one rule list; WTS and UserViewModel
      state are still read for correct identity and session matching.
  - lines: 'name,first(lockedStatus,qualified)'
    $name: Ordered row sources and priority fallbacks
    $description: >-
      Comma-separated sources, up to 16 lines. Priority fallback uses
      first(source,source,...). Example: name,qualified,status or
      name,first(lockedStatus,remoteStatus,qualified),custom.
      Supported: name, qualified, status, lockedStatus, remoteStatus,
      nativeStatus, custom, sessionId, connection, wtsState, station,
      protocol, lockState, vmLoggedIn, vmStatusVisible, vmLogonStatus.
      name must appear as a direct source. Empty fields are skipped.
      Invalid syntax fails closed to name,status,qualified.
  - order: manual
    $name: Order of eligible row lines
    $description: >-
      Manual respects the order above. First observed or Last observed orders
      visible rules by when this LogonUI process first observed a nonempty
      value; simultaneous observations keep configuration order. This is not
      historical Windows event order. Observation happens only during normal
      row refreshes, with no extra timer or polling.
    $options:
    - manual: Manual order
    - first: First observed (oldest first)
    - last: Last observed (newest first)
  - customText: ''
    $name: Default custom text
    $description: >-
      Literal text displayed only when a rule selects custom. Maximum 160
      characters. Visible on the Windows sign-in screen; do not put secrets.
  - customAccountTexts: ''
    $name: Custom text overrides by qualified account
    $description: >-
      Semicolon-separated COMPUTER\User|Text entries. Case-insensitive exact
      account match. Example A1504VA\Amiya|Owner;A1504VA\Emu|Guest.
  $name: Unified custom user-row layout (non-native)
  $description: >-
    All named core TextBlocks and diagnostic sources share the same safe,
    mod-owned row ordering. Priority candidates are evaluated left-to-right
    using actual Windows/WTS state; the underlying native elements remain
    preserved for hot-disable restoration. No Windows state is changed.

- customRowDetails:
  - sessionId: false
    $name: WTS - Session ID
    $description: >-
      Show the matched WTS session ID as a separate user-row detail line.
      Applies only when User list / Extra per-user session rows = Custom.
  - connection: false
    $name: WTS - Connection origin / client
    $description: >-
      Show console or RDP connection context, including the RDP client when
      known. Uses WTS protocol and connection state; this is presentation-only.
  - wtsState: false
    $name: WTS - Connection state
    $description: >-
      Show WTS Active, Connected, Disconnected, etc. as a diagnostic line.
  - station: false
    $name: WTS - Station name
    $description: >-
      Show WTS window-station name, if available.
  - protocol: false
    $name: WTS - Protocol / transport
    $description: >-
      Show WTS protocol 0 (console), 2 (RDP), or another reported value.
  - lockState: false
    $name: WTS - Lock state
    $description: >-
      Show lock state from WTSSessionInfoEx or WTS lock/unlock events.
  - vmLoggedIn: false
    $name: UserViewModel - IsLoggedIn
    $description: >-
      Show Windows' own IsLoggedIn flag. Can be enabled even when no WTS
      session matches the account; it does not establish WTS membership.
  - vmStatusVisible: false
    $name: UserViewModel - IsLogonStatusVisible
    $description: >-
      Show Windows' own IsLogonStatusVisible flag.
  - vmLogonStatus: false
    $name: UserViewModel - LogonStatus
    $description: >-
      Show the exact current UserViewModel LogonStatus string, without making
      it the main status line. This is not the separate transient StatusMessage.
  $name: Custom row detail sources
  $description: >-
    These switches apply only when User list / Extra per-user session rows
    is set to Custom. All are off by default. They change which diagnostics
    are displayed, not which APIs LogonUI must use to match a user or session.
    Summary and Full diagnostics remain unchanged, and Custom does not alter
    the primary status source. WTS-only lines need a matched WTS session;
    UserViewModel-only lines can appear for accounts without a WTS session.

- nonNativeUserList:
  - adaptiveRepairCoalescing: false
    $name: Experimental adaptive coalescing of clean lifecycle repairs
    $description: >-
      Alpha5c optimization experiment, off by default. Keeps the first
      layout-repair scan on every XAML lifecycle/WTS generation and all
      presenter DataContextChanged event wakeups. When rows remain clean
      and their count is stable, spaces subsequent no-change full-tree
      walks to 150/300 ms instead of fixed 75 ms before the existing 500 ms
      realization grace. Once grace elapses, two independent clean passes
      with the original 75 ms separation are still required to settle.
      Any row change or generation change immediately cancels backoff.
      The hard 10-second deadline, empty-tree retry, native transient
      priority and snapshot/live WTS semantics are unchanged. This adds
      no timers or polling, and touches only mod-owned XAML elements.
  - identityGuardedRowReuse: false
    $name: Experimental exact-identity guarded duplicate row work reuse
    $description: >-
      Alpha5f safety-hardened experimental optimization, off by default. Both retained XAML
      roots continue their own bounded layout walks and lifecycle settling.
      Only when a complete current-generation IUnknown presenter identity
      match was observed for both roots on one XAML thread, this may reuse a
      recent clean per-presenter repair result from the other root (maximum
      age 50 ms). It also checks the actual UserViewModel COM interface
      identity and presenter geometry, skips no new or recycled presenters,
      and requires factory-style snapshot rows: no live sessions, native
      transient mirroring, custom row layout or advanced diagnostics. Any
      mismatch or errors falls back to the original per-presenter repair.
      WTS or settings generation changes disable reuse immediately, and a
      superseded in-flight reuse pass is replayed once through the original
      repair path using a fresh WTS capture. This rare recovery adds no
      polling threads, timers, or routine XAML traversals. Not enabled by
      default; disable it if anything looks stale or unlike native LogonUI.
  - liveSessionRowUpdates: false
    $name: Continuously refresh existing account status rows
    $description: >-
      Non-native customization. Off (default) captures WTS session membership,
      state, lock condition and connection information when this LogonUI first
      realizes account rows, then preserves that snapshot until LogonUI restarts
      or the mod settings are reloaded. This approximates older AuthUI-style
      status snapshots; exact Windows 7/8/8.1 refresh semantics can vary.
      On continuously refreshes user-row statuses on WTS events (live RDP
      connect/disconnect, lock/unlock, logon/logoff), retaining the alpha4u
      live behavior. Does not affect native Windows sign-in or security state.
      Transient status mirroring and anonymous-session diagnostics remain
      separate optional settings under User list for backward compatibility.
  $name: Non-native user-list customizations

- diagnostics:
  - windhawkLogging: false
    $name: Windhawk diagnostic logging
    $description: >-
      Enable concise resolver, backend, layout, and error logging in Windhawk.
  - fileLogging: false
    $name: Write diagnostics to a shared file
    $description: >-
      Also append diagnostic messages to the configured log path. This is
      useful for console/Secure Desktop LogonUI sessions whose Windhawk output
      isn't visible in the current user's UI. The file is not automatically
      rotated, so delete or archive it if it becomes large.
  - fileLogPath: '%PUBLIC%\Desktop\LogonUITweaks.log'
    $name: Diagnostic log file path
    $description: >-
      Full path for shared file logging. Environment variables such as
      %PUBLIC% are expanded. The parent directory must already exist.
  - verbose: false
    $name: Verbose reverse-engineering diagnostics
    $description: >-
      Enable large native-button semantic dumps and dynamic XAML-tree traces.
      Normally leave this off. It is independent of screenshot capture, but
      requires either Windhawk logging or file logging for text output.
  - vmPropertyProbe: false
    $name: Observe native UserViewModel PropertyChanged (read-only)
    $description: >-
      Experimental reverse-engineering probe. Off by default. On subscribes
      to the standard XAML INotifyPropertyChanged interface on each realized
      LogonUX UserViewModel, and logs native property names with VM/account
      association (maximum 256 events per registration). No user-row refresh,
      native status mutation, private method hook, or polling is introduced.
      Requires Windhawk diagnostic logging or shared file logging to observe
      results. Enable only for controlled sign-in/RDP tests; filenames and
      account names in shared logs may reveal local account information.
  - vmStateCorrelation: false
    $name: Correlate native UserViewModel getters with WTS events (read-only)
    $description: >-
      Experimental alpha5b diagnostic; off by default. Automatically enables
      the native PropertyChanged observer, then queues one read-only getter
      snapshot on the already-existing LogonUI XAML-thread message bridge.
      Captures current UserViewModel.IsLoggedIn, IsLogonStatusVisible,
      IsDomainNameVisible and LogonStatus alongside live WTS session state.
      Coalesces simultaneous notifications, throttles selection-only bursts,
      records only changed state or a baseline, and never updates the UI.
      This is not the native-style user-row session snapshot; the live WTS
      values are sampled strictly for diagnostics. Avoid long-term activation.
  - rowIdentityProbe: false
    $name: Compare native row-presenter identities between XAML roots (read-only)
    $description: >-
      Experimental alpha5d structural probe; off by default. Records COM
      IUnknown identities of ListViewItemPresenter objects only while the
      existing lifecycle/bootstrap repair code already traverses those
      elements. Compares identity sets between roots on the same XAML thread
      and repair generation. Logs one grouped record per changed root set,
      capped at 64 presenters and 12 recent roots; no second tree traversal,
      native state getters, event subscriptions, timers, or row changes.
      Requires file logging or Windhawk logging. Disable after a short test.
  - xamlCapture: false
    $name: Diagnostic XAML screenshot capture
    $description: >-
      Capture only the LogonUI SystemProvider subtree for troubleshooting and
      reverse engineering. Disabled by default.
  - xamlCaptureDirectory: '%PUBLIC%\Desktop'
    $name: XAML capture directory
    $description: >-
      Directory for diagnostic BMP captures. Environment variables such as
      %PUBLIC% are expanded. The directory must already exist.
  $name: Diagnostics
*/
// ==/WindhawkModSettings==

// Design notes:
// - No Windows build/version allowlist. Tested builds use verified RVAs only as
//   fast paths. Unknown stobject.dll builds use a symbol-free structural Battery
//   resolver first, then fall back to runtime symbols if structural validation
//   fails.
// - Structural ABI/layout checks remain fail-closed to avoid mutating an
//   unexpected LogonUI tree.
// - Battery uses Windows-owned LogonUX state and the genuine stobject/BatMeter
//   classic flyout. Optional live refresh rebuilds through ShowBatteryUI rather
//   than re-entering stobject's low-level UI population routine. Volume uses event-driven Core Audio state and native SndVol.
// - Placement, click/wheel behavior, injected/native-button visibility,
 //   dynamic percentage labels, renderer choice, live-update handoff, and
 //   concise/verbose diagnostics are independently configurable.
// - Battery and Volume clicks deliberately don't synthesize toggle behavior.
//   Battery always invokes the genuine classic Windows flyout path; Volume
//   directly launches SndVol so Windows keeps its own version-specific
//   existing-Mixer behavior.
// - Detailed behavior and compatibility notes are documented above and in
//   the companion README.

#include <windows.h>
#include <unknwn.h>
#include <wtsapi32.h>
#include <dbt.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <objidl.h>
#include <propsys.h>
#include <wrl.h>
#include <winternl.h>
#include <winioctl.h>
#include <wbemidl.h>
#include <windhawk_utils.h>

#include <algorithm>
#include <array>
#include <limits>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <cstddef>
#include <cmath>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <new>
#include <string_view>
#include <vector>

#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <winrt/base.h>

using namespace winrt;
using namespace winrt::Windows::UI::Xaml;
namespace Automation = winrt::Windows::UI::Xaml::Automation;
namespace Controls = winrt::Windows::UI::Xaml::Controls;
namespace Data = winrt::Windows::UI::Xaml::Data;
namespace Input = winrt::Windows::UI::Xaml::Input;
namespace Media = winrt::Windows::UI::Xaml::Media;
namespace Shapes = winrt::Windows::UI::Xaml::Shapes;

namespace {

constexpr wchar_t kTargetModule[] = L"Windows.UI.Logon.dll";
constexpr wchar_t kInjectedBatteryName[] = L"WindhawkLogonUITweaksBatteryVisual";
constexpr wchar_t kInjectedVolumeName[] = L"WindhawkLogonUITweaksVolumeVisual";
constexpr wchar_t kInjectedClockName[] = L"WindhawkLogonUITweaksClockVisual";

#ifndef FILE_DEVICE_VIDEO
#define FILE_DEVICE_VIDEO 0x00000023
#endif

#ifndef IOCTL_VIDEO_QUERY_SUPPORTED_BRIGHTNESS
#define IOCTL_VIDEO_QUERY_SUPPORTED_BRIGHTNESS \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x125, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIDEO_QUERY_DISPLAY_BRIGHTNESS \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x126, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIDEO_SET_DISPLAY_BRIGHTNESS \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x127, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

struct DisplayBrightnessIoctl {
    UCHAR displayPolicy;
    UCHAR acBrightness;
    UCHAR dcBrightness;
};

constexpr UCHAR kDisplayPolicyAc = 0x01;
constexpr UCHAR kDisplayPolicyDc = 0x02;
constexpr UCHAR kDisplayPolicyBoth = 0x03;

HANDLE g_stopEvent = nullptr;
HANDLE g_hookThread = nullptr;
std::atomic<bool> g_hookInstalled{false};
std::atomic<bool> g_captureScheduled{false};
std::atomic<unsigned> g_captureAsyncInFlight{0};
std::atomic<bool> g_lateAttachPending{false};
std::atomic<HHOOK> g_lateAttachHook{nullptr};
std::atomic<HHOOK> g_lateAttachCallWndHook{nullptr};
UINT g_lateAttachMessage = 0;
HANDLE g_lateAttachCompletedEvent = nullptr;
std::atomic<bool> g_lateAttachProbeMatched{false};
std::atomic<DWORD> g_lateAttachProbeThreadId{0};
std::atomic<ULONG_PTR> g_lateAttachProbeCookie{0};
std::atomic<ULONG_PTR> g_lateAttachCookieCounter{1};

// Hot-unload safety. Windhawk calls Wh_ModBeforeUninit while this DLL and its
// callbacks are still executable. Use that phase to detach every callback
// owned by persistent LogonUI XAML objects before the DLL can disappear.
std::atomic<bool> g_unloading{false};
std::atomic<bool> g_hotCleanupPending{false};
std::atomic<HHOOK> g_hotCleanupHook{nullptr};
std::atomic<HHOOK> g_hotCleanupCallWndHook{nullptr};
UINT g_hotCleanupMessage = 0;
std::atomic<DWORD> g_hotCleanupProbeThreadId{0};
std::atomic<ULONG_PTR> g_hotCleanupProbeCookie{0};
std::atomic<ULONG_PTR> g_hotCleanupCookieCounter{1};
HANDLE g_hotCleanupCompletedEvent = nullptr;
HANDLE g_hotCleanupFenceEvent = nullptr;
std::atomic<int> g_hotCleanupFenceCountdown{0};
std::atomic<unsigned> g_audioCallbackObjectsAlive{0};
std::atomic<unsigned> g_audioCallbacksInFlight{0};
// Last thread positively validated as hosting LogonUX.SystemProviderUserControl.
// Keep this across transient Unloaded events; LogonUI can have more than one
// XAML thread/window over the lifetime of the process.
std::atomic<DWORD> g_xamlUiThreadId{0};
SRWLOCK g_knownXamlThreadsLock = SRWLOCK_INIT;
std::vector<DWORD> g_knownXamlThreads;

namespace UserStatus {
void OnSystemProviderLoaded(void* sender);
[[maybe_unused]] void OnSystemProviderUnloaded();
}

void RememberXamlThread(DWORD threadId, bool hostsSystemProvider) {
    if (!threadId) return;

    AcquireSRWLockExclusive(&g_knownXamlThreadsLock);
    if (std::find(g_knownXamlThreads.begin(), g_knownXamlThreads.end(),
                  threadId) == g_knownXamlThreads.end()) {
        g_knownXamlThreads.push_back(threadId);
    }
    ReleaseSRWLockExclusive(&g_knownXamlThreadsLock);

    if (hostsSystemProvider) {
        g_xamlUiThreadId.store(threadId, std::memory_order_release);
    }
}

std::vector<DWORD> SnapshotKnownXamlThreads() {
    std::vector<DWORD> result;
    AcquireSRWLockShared(&g_knownXamlThreadsLock);
    result = g_knownXamlThreads;
    ReleaseSRWLockShared(&g_knownXamlThreadsLock);
    return result;
}

void ClearKnownXamlThreads() {
    AcquireSRWLockExclusive(&g_knownXamlThreadsLock);
    g_knownXamlThreads.clear();
    ReleaseSRWLockExclusive(&g_knownXamlThreadsLock);
    g_xamlUiThreadId.store(0, std::memory_order_release);
}

std::atomic<bool> g_enableLogging{false};
std::atomic<bool> g_enableFileLogging{false};
std::atomic<bool> g_enableVerboseDiagnostics{false};
std::atomic<bool> g_enableXamlCapture{false};

SRWLOCK g_layoutRetryLock = SRWLOCK_INIT;
std::vector<void*> g_layoutRetryOwners;
SRWLOCK g_fileLogLock = SRWLOCK_INIT;
std::wstring g_sharedDiagnosticLogPath =
    L"C:\\Users\\Public\\Desktop\\LogonUITweaks.log";

void ModLogImpl(int sourceLine, const char* sourceFunction,
                const wchar_t* format, ...) {
    const bool windhawkLogging =
        g_enableLogging.load(std::memory_order_relaxed);
    const bool fileLogging =
        g_enableFileLogging.load(std::memory_order_relaxed);
    if (!windhawkLogging && !fileLogging) {
        return;
    }

    wchar_t message[2048]{};
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(message, _countof(message), _TRUNCATE, format, args);
    va_end(args);

    if (windhawkLogging) {
        // Keep the original call site visible even though formatting is
        // centralized so the same message can also be written to a shared
        // cross-session file.
        Wh_Log(L"[src:%d:%S] %s", sourceLine,
               sourceFunction ? sourceFunction : "?", message);
    }

    if (!fileLogging) {
        return;
    }

    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t line[2600]{};
    _snwprintf_s(
        line, _countof(line), _TRUNCATE,
        L"%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu tid=%lu [src:%d:%S] %s\r\n",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        GetCurrentProcessId(), GetCurrentThreadId(), sourceLine,
        sourceFunction ? sourceFunction : "?", message);

    const int utf8Length = WideCharToMultiByte(
        CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 1) {
        return;
    }
    std::string utf8(static_cast<size_t>(utf8Length - 1), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, line, -1, utf8.data(), utf8Length - 1,
        nullptr, nullptr);

    AcquireSRWLockExclusive(&g_fileLogLock);
    HANDLE file = INVALID_HANDLE_VALUE;
    if (!g_sharedDiagnosticLogPath.empty()) {
        file = CreateFileW(
            g_sharedDiagnosticLogPath.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
    }
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()),
                  &written, nullptr);
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&g_fileLogLock);
}

#define ModLog(...) ModLogImpl(__LINE__, __FUNCTION__, __VA_ARGS__)

enum class VolumeRendererMode : int {
    Automatic = 0,
    SegoeMdl2,
    SegoeFluent,
    SndVolAutomatic,
    SndVolDll,
    SndVolMun,
};

enum class SndVolResourceSource : int {
    WindowsAutomatic = 0,
    DirectDll,
    DirectMun,
};

enum class VolumeUiHandoffMode : int {
    Automatic = 0,
    DispatcherQueue,
    CoreDispatcher,
    LegacyMessageWindow,
};

enum class BatteryClickAction : int {
    Open = 0,
    None,
};

enum class BatteryMiddleClickAction : int {
    AdvancedPower = 0,
    OpenFlyout,
    None,
};

enum class BatteryFlyoutRefreshMode : int {
    WindowsDefault = 0,
    EventDriven,
};

enum class VolumeClickAction : int {
    Launch = 0,
    None,
};

enum class VolumeMiddleClickAction : int {
    ToggleMute = 0,
    None,
};

enum class VolumeScrollUnmuteBehavior : int {
    Never = 0,
    Increase,
    Decrease,
    Either,
};

enum class ClockClickAction : int {
    ClassicFlyout = 0,
    DateTimeDialog,
    None,
};

enum class BatteryFlyoutResolveState : int {
    NotStarted = 0,
    Resolving,
    Ready,
    Failed,
};

enum class ClassicClockAbiResolveState : int {
    NotStarted = 0,
    Resolving,
    Ready,
    Failed,
};

enum class BatteryFlyoutPlacementMode : int {
    AnchorToButton = 0,
    WindowsDefault,
};

enum class MorePowerOptionsAction : int {
    AdvancedPowerOptions = 0,
    WindowsDefault,
};

enum class AdvancedPowerPlacementMode : int {
    AnchorToBatteryButton = 0,
    WindowsDefault,
};

enum class ClockPlacementMode : int {
    AnchorToButton = 0,
    WindowsDefault,
};

enum class SndVolPlacementMode : int {
    AnchorToButton = 0,
    WindowsDefault,
};

enum class PercentagePosition : int {
    Right = 0,
    Left,
};

enum class AnchorSide : int {
    Automatic = 0,
    Above,
    Below,
};

enum class VolumeWheelAction : int {
    None = 0,
    Adjust,
};

enum class BatteryWheelAction : int {
    None = 0,
    AdjustBrightness,
};

std::atomic<VolumeRendererMode> g_volumeRenderer{
    VolumeRendererMode::Automatic};
std::atomic<VolumeUiHandoffMode> g_volumeUiHandoff{
    VolumeUiHandoffMode::Automatic};
std::atomic<bool> g_showBatteryButton{true};
std::atomic<bool> g_batteryVisibilityAutomatic{true};
std::atomic<bool> g_showVolumeButton{true};
std::atomic<bool> g_showImeModeButton{true};
std::atomic<bool> g_showInputButton{true};
std::atomic<bool> g_showNetworkSignInButton{true};
std::atomic<bool> g_showDisconnectButton{true};
std::atomic<bool> g_showNetworkButton{true};
std::atomic<bool> g_showAccessibilityButton{true};
std::atomic<bool> g_showPowerButton{true};
std::atomic<bool> g_showOtherNativeButtons{true};
std::atomic<int> g_nativeImeInputSpacing{-1};
std::atomic<int> g_imeModeVisualOffsetX{-4};
std::atomic<bool> g_showBatteryPercentage{true};
std::atomic<bool> g_showVolumePercentage{true};
std::atomic<PercentagePosition> g_batteryPercentagePosition{
    PercentagePosition::Right};
std::atomic<PercentagePosition> g_volumePercentagePosition{
    PercentagePosition::Right};
std::atomic<bool> g_hideBatteryPercentageAt100{false};
std::atomic<bool> g_hideVolumePercentageAt100{false};
std::atomic<bool> g_showMutedTextInVolumePercentage{false};
std::atomic<bool> g_percentageShowPercentSign{true};
std::atomic<int> g_percentageFontSize{12};
std::atomic<int> g_percentageSpacing{4};
std::atomic<BatteryClickAction> g_batteryClickAction{
    BatteryClickAction::Open};
std::atomic<BatteryMiddleClickAction> g_batteryMiddleClickAction{
    BatteryMiddleClickAction::AdvancedPower};
std::atomic<BatteryFlyoutRefreshMode> g_batteryFlyoutRefresh{
    BatteryFlyoutRefreshMode::WindowsDefault};
std::atomic<DWORD> g_batteryEventDebounceDelayMs{500};
std::atomic<VolumeClickAction> g_volumeClickAction{
    VolumeClickAction::Launch};
std::atomic<VolumeMiddleClickAction> g_volumeMiddleClickAction{
    VolumeMiddleClickAction::ToggleMute};
std::atomic<VolumeWheelAction> g_volumeWheelAction{
    VolumeWheelAction::None};
std::atomic<int> g_volumeWheelStepPercent{2};
std::atomic<VolumeScrollUnmuteBehavior> g_volumeScrollUnmuteBehavior{
    VolumeScrollUnmuteBehavior::Increase};
std::atomic<int> g_volumeUnmuteThresholdSteps{1};
std::atomic<bool> g_volumeMuteAtZero{true};
std::atomic<bool> g_volumeWheelReverse{false};
std::atomic<BatteryWheelAction> g_batteryWheelAction{
    BatteryWheelAction::AdjustBrightness};
std::atomic<int> g_batteryWheelStepPercent{10};
std::atomic<bool> g_batteryWheelReverse{false};
std::atomic<int> g_precisionScrollStepPercent{1};
std::atomic<int> g_precisionScrollDeltaPerStep{30};
std::atomic<int> g_brightnessPendingPercent{0};
std::atomic<bool> g_brightnessWorkerStop{false};
HANDLE g_brightnessWorkerEvent = nullptr;
HANDLE g_brightnessWorkerThread = nullptr;
SRWLOCK g_brightnessWorkerLock = SRWLOCK_INIT;
std::atomic<BatteryFlyoutPlacementMode> g_batteryFlyoutPlacement{
    BatteryFlyoutPlacementMode::AnchorToButton};
std::atomic<AnchorSide> g_batteryFlyoutAnchorSide{
    AnchorSide::Automatic};
std::atomic<int> g_batteryFlyoutOffsetX{0};
std::atomic<int> g_batteryFlyoutOffsetY{0};
std::atomic<MorePowerOptionsAction> g_morePowerOptionsAction{
    MorePowerOptionsAction::AdvancedPowerOptions};
std::atomic<AdvancedPowerPlacementMode> g_advancedPowerPlacement{
    AdvancedPowerPlacementMode::AnchorToBatteryButton};
std::atomic<AnchorSide> g_advancedPowerAnchorSide{
    AnchorSide::Automatic};
std::atomic<int> g_advancedPowerOffsetX{0};
std::atomic<int> g_advancedPowerOffsetY{0};
std::atomic<SndVolPlacementMode> g_sndVolPlacement{
    SndVolPlacementMode::AnchorToButton};
std::atomic<AnchorSide> g_sndVolAnchorSide{
    AnchorSide::Automatic};
std::atomic<int> g_sndVolOffsetX{0};
std::atomic<int> g_sndVolOffsetY{0};
std::atomic<SndVolPlacementMode> g_sndVolMixerPlacement{
    SndVolPlacementMode::AnchorToButton};
std::atomic<AnchorSide> g_sndVolMixerAnchorSide{
    AnchorSide::Automatic};
std::atomic<int> g_sndVolMixerOffsetX{0};
std::atomic<int> g_sndVolMixerOffsetY{0};
std::atomic<void*> g_showBatteryUiTarget{nullptr};
std::atomic<void*> g_launchPowerCplTarget{nullptr};
std::atomic<void*> g_stobjectBatteryState{nullptr};
std::atomic<HMODULE> g_stobjectModule{nullptr};
std::atomic<HMODULE> g_batMeterModule{nullptr};
std::atomic<void*> g_queryBatteryDataTarget{nullptr};
std::atomic<void*> g_updateBatteryDataTarget{nullptr};
std::atomic<void*> g_cleanupBatteryDataTarget{nullptr};
std::atomic<BatteryFlyoutResolveState> g_batteryFlyoutResolveState{
    BatteryFlyoutResolveState::NotStarted};
HANDLE g_batteryResolverThread = nullptr;
HANDLE g_batteryNotificationThread = nullptr;
std::atomic<HWND> g_batteryUiBridgeWindow{nullptr};
winrt::weak_ref<Controls::TextBlock> g_batteryPercentText;
std::atomic<bool> g_launchPowerCplHookInstalled{false};
std::atomic<bool> g_advancedPowerDialogActive{false};
HANDLE g_advancedPowerThread = nullptr;
std::atomic<bool> g_showClockButton{true};
std::atomic<bool> g_clockShowSeconds{false};
std::atomic<bool> g_clockShowDate{true};
std::atomic<ClockClickAction> g_clockClickAction{
    ClockClickAction::ClassicFlyout};
std::atomic<ClockPlacementMode> g_clockFlyoutPlacement{
    ClockPlacementMode::AnchorToButton};
std::atomic<AnchorSide> g_clockFlyoutAnchorSide{
    AnchorSide::Automatic};
std::atomic<int> g_clockFlyoutOffsetX{0};
std::atomic<int> g_clockFlyoutOffsetY{0};
std::atomic<ClockPlacementMode> g_clockDateTimePlacement{
    ClockPlacementMode::AnchorToButton};
std::atomic<AnchorSide> g_clockDateTimeAnchorSide{
    AnchorSide::Automatic};
std::atomic<int> g_clockDateTimeOffsetX{0};
std::atomic<int> g_clockDateTimeOffsetY{0};
SRWLOCK g_classicClockBackendLock = SRWLOCK_INIT;
HMODULE g_timeDateModule = nullptr;
IUnknown* g_trayClockObject = nullptr;
bool g_classicClockResolveAttempted = false;
std::atomic<ClassicClockAbiResolveState> g_classicClockAbiResolveState{
    ClassicClockAbiResolveState::NotStarted};
std::atomic<uintptr_t> g_classicClockShowFlyoutRva{0};
std::atomic<uintptr_t> g_classicClockHideFlyoutRva{0};
std::atomic<DWORD> g_classicClockResolvedTimestamp{0};
std::atomic<DWORD> g_classicClockResolvedSizeOfImage{0};
HANDLE g_classicClockResolverThread = nullptr;
std::atomic<bool> g_dateTimeDialogActive{false};
HANDLE g_dateTimeDialogThread = nullptr;
std::atomic<DWORD> g_osBuild{0};
SRWLOCK g_settingsLock = SRWLOCK_INIT;
SRWLOCK g_batteryAnchorLock = SRWLOCK_INIT;
RECT g_batteryAnchorRect{};
std::atomic<bool> g_batteryAnchorValid{false};
SRWLOCK g_sndVolAnchorLock = SRWLOCK_INIT;
RECT g_sndVolAnchorRect{};
std::atomic<bool> g_sndVolAnchorValid{false};
HANDLE g_sndVolWindowMonitorThread = nullptr;
SRWLOCK g_clockAnchorLock = SRWLOCK_INIT;
RECT g_clockAnchorRect{};
std::atomic<bool> g_clockAnchorValid{false};
HANDLE g_clockWindowMonitorThread = nullptr;
std::wstring g_buttonOrder = L"battery,volume,clock,ime,input,network-signin,disconnect,network,accessibility,power";
std::wstring g_sndVolLaunchParameters = L"-f";
std::wstring g_percentageFontFamily = L"Segoe UI";
std::wstring g_xamlCaptureDirectory = L"C:\\Users\\Public\\Desktop";

const wchar_t* VolumeRendererModeName(VolumeRendererMode mode) {
    switch (mode) {
        case VolumeRendererMode::Automatic:
            return L"Automatic";
        case VolumeRendererMode::SegoeMdl2:
            return L"Segoe MDL2 Assets";
        case VolumeRendererMode::SegoeFluent:
            return L"Segoe Fluent Icons";
        case VolumeRendererMode::SndVolAutomatic:
            return L"SndVolSSO (automatic Windows resource lookup)";
        case VolumeRendererMode::SndVolDll:
            return L"SndVolSSO.dll resources";
        case VolumeRendererMode::SndVolMun:
            return L"SndVolSSO.dll.mun resources";
    }
    return L"Unknown";
}

const wchar_t* VolumeUiHandoffModeName(VolumeUiHandoffMode mode) {
    switch (mode) {
        case VolumeUiHandoffMode::Automatic:
            return L"Automatic";
        case VolumeUiHandoffMode::DispatcherQueue:
            return L"DispatcherQueue";
        case VolumeUiHandoffMode::CoreDispatcher:
            return L"CoreDispatcher";
        case VolumeUiHandoffMode::LegacyMessageWindow:
            return L"Legacy message-only window";
    }
    return L"Unknown";
}

const wchar_t* BatteryClickActionName(BatteryClickAction action) {
    switch (action) {
        case BatteryClickAction::Open:
            return L"Open classic battery flyout";
        case BatteryClickAction::None:
            return L"Do nothing";
    }
    return L"Unknown";
}

const wchar_t* BatteryMiddleClickActionName(
        BatteryMiddleClickAction action) {
    switch (action) {
        case BatteryMiddleClickAction::AdvancedPower:
            return L"Open Advanced Power Options";
        case BatteryMiddleClickAction::OpenFlyout:
            return L"Open classic battery flyout";
        case BatteryMiddleClickAction::None:
            return L"Do nothing";
    }
    return L"Unknown";
}

const wchar_t* BatteryFlyoutRefreshModeName(
        BatteryFlyoutRefreshMode mode) {
    switch (mode) {
        case BatteryFlyoutRefreshMode::WindowsDefault:
            return L"Windows default";
        case BatteryFlyoutRefreshMode::EventDriven:
            return L"Event-driven";
    }
    return L"Unknown";
}

const wchar_t* VolumeClickActionName(VolumeClickAction action) {
    switch (action) {
        case VolumeClickAction::Launch:
            return L"Launch SndVol";
        case VolumeClickAction::None:
            return L"Do nothing";
    }
    return L"Unknown";
}

const wchar_t* VolumeMiddleClickActionName(
        VolumeMiddleClickAction action) {
    switch (action) {
        case VolumeMiddleClickAction::ToggleMute:
            return L"Toggle mute";
        case VolumeMiddleClickAction::None:
            return L"Do nothing";
    }
    return L"Unknown";
}

const wchar_t* VolumeScrollUnmuteBehaviorName(
        VolumeScrollUnmuteBehavior behavior) {
    switch (behavior) {
        case VolumeScrollUnmuteBehavior::Never:
            return L"Never";
        case VolumeScrollUnmuteBehavior::Increase:
            return L"Increase only";
        case VolumeScrollUnmuteBehavior::Decrease:
            return L"Decrease only";
        case VolumeScrollUnmuteBehavior::Either:
            return L"Either direction";
    }
    return L"Unknown";
}

const wchar_t* ClockClickActionName(ClockClickAction action) {
    switch (action) {
        case ClockClickAction::ClassicFlyout:
            return L"Windows classic clock flyout";
        case ClockClickAction::DateTimeDialog:
            return L"Open classic Date and Time";
        case ClockClickAction::None:
            return L"Do nothing";
    }
    return L"Unknown";
}

const wchar_t* PercentagePositionName(PercentagePosition position) {
    return position == PercentagePosition::Left ? L"Left" : L"Right";
}

const wchar_t* AnchorSideName(AnchorSide side) {
    switch (side) {
        case AnchorSide::Automatic:
            return L"Automatic";
        case AnchorSide::Above:
            return L"Above";
        case AnchorSide::Below:
            return L"Below";
    }
    return L"Unknown";
}

const wchar_t* VolumeWheelActionName(VolumeWheelAction action) {
    return action == VolumeWheelAction::Adjust
        ? L"Adjust master volume"
        : L"Do nothing";
}

const wchar_t* BatteryWheelActionName(BatteryWheelAction action) {
    return action == BatteryWheelAction::AdjustBrightness
        ? L"Adjust display brightness"
        : L"Do nothing";
}

AnchorSide ParseAnchorSide(PCWSTR value) {
    if (value) {
        if (_wcsicmp(value, L"above") == 0) {
            return AnchorSide::Above;
        }
        if (_wcsicmp(value, L"below") == 0) {
            return AnchorSide::Below;
        }
    }
    return AnchorSide::Automatic;
}

PercentagePosition ParsePercentagePosition(PCWSTR value) {
    return value && _wcsicmp(value, L"left") == 0
        ? PercentagePosition::Left
        : PercentagePosition::Right;
}

const wchar_t* BatteryFlyoutPlacementModeName(
        BatteryFlyoutPlacementMode mode) {
    switch (mode) {
        case BatteryFlyoutPlacementMode::AnchorToButton:
            return L"Anchor to Battery button";
        case BatteryFlyoutPlacementMode::WindowsDefault:
            return L"Windows default";
    }
    return L"Unknown";
}

const wchar_t* MorePowerOptionsActionName(
        MorePowerOptionsAction action) {
    switch (action) {
        case MorePowerOptionsAction::AdvancedPowerOptions:
            return L"Open Advanced Power Options directly";
        case MorePowerOptionsAction::WindowsDefault:
            return L"Windows default";
    }
    return L"Unknown";
}

const wchar_t* AdvancedPowerPlacementModeName(
        AdvancedPowerPlacementMode mode) {
    switch (mode) {
        case AdvancedPowerPlacementMode::AnchorToBatteryButton:
            return L"Anchor to Battery button";
        case AdvancedPowerPlacementMode::WindowsDefault:
            return L"Windows default";
    }
    return L"Unknown";
}

const wchar_t* ClockPlacementModeName(
        ClockPlacementMode mode) {
    switch (mode) {
        case ClockPlacementMode::AnchorToButton:
            return L"Anchor to Clock button";
        case ClockPlacementMode::WindowsDefault:
            return L"Windows default";
    }
    return L"Unknown";
}

const wchar_t* SndVolPlacementModeName(
        SndVolPlacementMode mode) {
    switch (mode) {
        case SndVolPlacementMode::AnchorToButton:
            return L"Anchor to Volume button";
        case SndVolPlacementMode::WindowsDefault:
            return L"Windows default";
    }
    return L"Unknown";
}

std::wstring ExpandEnvironmentPathOrDefault(
        PCWSTR configuredValue,
        PCWSTR defaultValue) {
    std::wstring input =
        configuredValue && *configuredValue
            ? std::wstring(configuredValue)
            : std::wstring(defaultValue ? defaultValue : L"");

    if (input.empty()) {
        return {};
    }

    const DWORD required =
        ExpandEnvironmentStringsW(input.c_str(), nullptr, 0);
    if (required == 0) {
        return input;
    }

    std::vector<wchar_t> buffer(required);
    const DWORD written = ExpandEnvironmentStringsW(
        input.c_str(), buffer.data(), required);
    if (written == 0 || written > required) {
        return input;
    }

    return std::wstring(buffer.data());
}

void LoadSettings() {
    const bool enableLogging =
        Wh_GetIntSetting(L"diagnostics.windhawkLogging") != 0;
    const bool enableFileLogging =
        Wh_GetIntSetting(L"diagnostics.fileLogging") != 0;
    const bool enableVerboseDiagnostics =
        Wh_GetIntSetting(L"diagnostics.verbose") != 0;
    const bool enableXamlCapture =
        Wh_GetIntSetting(L"diagnostics.xamlCapture") != 0;

    PCWSTR fileLogPathSetting =
        Wh_GetStringSetting(L"diagnostics.fileLogPath");
    std::wstring fileLogPath = ExpandEnvironmentPathOrDefault(
        fileLogPathSetting,
        L"%PUBLIC%\\Desktop\\LogonUITweaks.log");
    Wh_FreeStringSetting(fileLogPathSetting);

    PCWSTR xamlCaptureDirectorySetting =
        Wh_GetStringSetting(L"diagnostics.xamlCaptureDirectory");
    std::wstring xamlCaptureDirectory = ExpandEnvironmentPathOrDefault(
        xamlCaptureDirectorySetting,
        L"%PUBLIC%\\Desktop");
    Wh_FreeStringSetting(xamlCaptureDirectorySetting);

    AcquireSRWLockExclusive(&g_fileLogLock);
    g_sharedDiagnosticLogPath = fileLogPath;
    ReleaseSRWLockExclusive(&g_fileLogLock);

    AcquireSRWLockExclusive(&g_settingsLock);
    g_xamlCaptureDirectory = xamlCaptureDirectory;
    ReleaseSRWLockExclusive(&g_settingsLock);

    g_enableLogging.store(enableLogging, std::memory_order_relaxed);
    g_enableFileLogging.store(enableFileLogging, std::memory_order_relaxed);
    g_enableVerboseDiagnostics.store(
        enableVerboseDiagnostics, std::memory_order_relaxed);
    g_enableXamlCapture.store(enableXamlCapture, std::memory_order_relaxed);

    PCWSTR batteryVisibilitySetting =
        Wh_GetStringSetting(L"battery.visibility");
    bool showBattery = true;
    bool automaticBatteryVisibility = true;
    if (batteryVisibilitySetting) {
        if (_wcsicmp(batteryVisibilitySetting, L"hide") == 0) {
            showBattery = false;
            automaticBatteryVisibility = false;
        } else if (_wcsicmp(batteryVisibilitySetting, L"show") == 0) {
            showBattery = true;
            automaticBatteryVisibility = false;
        }
    }
    Wh_FreeStringSetting(batteryVisibilitySetting);

    const bool showVolume =
        Wh_GetIntSetting(L"volume.showButton") != 0;
    const bool showImeMode =
        Wh_GetIntSetting(L"nativeButtons.showImeMode") != 0;
    const bool showInput =
        Wh_GetIntSetting(L"nativeButtons.showInput") != 0;
    const bool showNetworkSignIn =
        Wh_GetIntSetting(L"nativeButtons.showNetworkSignIn") != 0;
    const bool showDisconnect =
        Wh_GetIntSetting(L"nativeButtons.showDisconnect") != 0;
    const bool showNetwork =
        Wh_GetIntSetting(L"nativeButtons.showNetwork") != 0;
    const bool showAccessibility =
        Wh_GetIntSetting(L"nativeButtons.showAccessibility") != 0;
    const bool showPower =
        Wh_GetIntSetting(L"nativeButtons.showPower") != 0;
    const bool showOtherNative =
        Wh_GetIntSetting(L"nativeButtons.showOther") != 0;
    int nativeImeInputSpacing =
        Wh_GetIntSetting(L"nativeButtons.imeInputSpacing");
    nativeImeInputSpacing = std::clamp(nativeImeInputSpacing, -1, 32);
    int imeModeVisualOffsetX =
        Wh_GetIntSetting(L"nativeButtons.imeModeVisualOffsetX");
    imeModeVisualOffsetX = std::clamp(imeModeVisualOffsetX, -32, 32);
    const bool showBatteryPercentage =
        Wh_GetIntSetting(L"battery.percentage.show") != 0;
    const bool showVolumePercentage =
        Wh_GetIntSetting(L"volume.percentage.show") != 0;
    const bool hideBatteryPercentageAt100 =
        Wh_GetIntSetting(L"battery.percentage.hideAt100") != 0;
    const bool hideVolumePercentageAt100 =
        Wh_GetIntSetting(L"volume.percentage.hideAt100") != 0;
    const bool showMutedTextInVolumePercentage =
        Wh_GetIntSetting(L"volume.percentage.showMutedText") != 0;
    const bool percentageShowPercentSign =
        Wh_GetIntSetting(L"percentageLabels.showPercentSign") != 0;

    int percentageFontSize =
        Wh_GetIntSetting(L"percentageLabels.fontSize");
    percentageFontSize = std::clamp(percentageFontSize, 8, 30);
    int percentageSpacing =
        Wh_GetIntSetting(L"percentageLabels.spacing");
    percentageSpacing = std::clamp(percentageSpacing, 0, 32);

    PCWSTR batteryPercentagePositionSetting =
        Wh_GetStringSetting(L"battery.percentage.position");
    const auto batteryPercentagePosition =
        ParsePercentagePosition(batteryPercentagePositionSetting);
    Wh_FreeStringSetting(batteryPercentagePositionSetting);

    PCWSTR volumePercentagePositionSetting =
        Wh_GetStringSetting(L"volume.percentage.position");
    const auto volumePercentagePosition =
        ParsePercentagePosition(volumePercentagePositionSetting);
    Wh_FreeStringSetting(volumePercentagePositionSetting);

    PCWSTR percentageFontFamilySetting =
        Wh_GetStringSetting(L"percentageLabels.fontFamily");
    std::wstring percentageFontFamily =
        (percentageFontFamilySetting && *percentageFontFamilySetting)
            ? std::wstring(percentageFontFamilySetting)
            : std::wstring(L"Segoe UI");
    Wh_FreeStringSetting(percentageFontFamilySetting);

    g_showBatteryButton.store(showBattery, std::memory_order_relaxed);
    g_batteryVisibilityAutomatic.store(
        automaticBatteryVisibility, std::memory_order_relaxed);
    g_showVolumeButton.store(showVolume, std::memory_order_relaxed);
    g_showImeModeButton.store(showImeMode, std::memory_order_relaxed);
    g_showInputButton.store(showInput, std::memory_order_relaxed);
    g_showNetworkSignInButton.store(
        showNetworkSignIn, std::memory_order_relaxed);
    g_showDisconnectButton.store(
        showDisconnect, std::memory_order_relaxed);
    g_showNetworkButton.store(showNetwork, std::memory_order_relaxed);
    g_showAccessibilityButton.store(
        showAccessibility, std::memory_order_relaxed);
    g_showPowerButton.store(showPower, std::memory_order_relaxed);
    g_showOtherNativeButtons.store(
        showOtherNative, std::memory_order_relaxed);
    g_nativeImeInputSpacing.store(
        nativeImeInputSpacing, std::memory_order_relaxed);
    g_imeModeVisualOffsetX.store(
        imeModeVisualOffsetX, std::memory_order_relaxed);
    g_showBatteryPercentage.store(
        showBatteryPercentage, std::memory_order_relaxed);
    g_showVolumePercentage.store(
        showVolumePercentage, std::memory_order_relaxed);
    g_batteryPercentagePosition.store(
        batteryPercentagePosition, std::memory_order_relaxed);
    g_volumePercentagePosition.store(
        volumePercentagePosition, std::memory_order_relaxed);
    g_hideBatteryPercentageAt100.store(
        hideBatteryPercentageAt100, std::memory_order_relaxed);
    g_hideVolumePercentageAt100.store(
        hideVolumePercentageAt100, std::memory_order_relaxed);
    g_showMutedTextInVolumePercentage.store(
        showMutedTextInVolumePercentage, std::memory_order_relaxed);
    g_percentageShowPercentSign.store(
        percentageShowPercentSign, std::memory_order_relaxed);
    g_percentageFontSize.store(
        percentageFontSize, std::memory_order_relaxed);
    g_percentageSpacing.store(
        percentageSpacing, std::memory_order_relaxed);

    PCWSTR renderer = Wh_GetStringSetting(L"volume.renderer");
    VolumeRendererMode mode = VolumeRendererMode::Automatic;

    if (renderer) {
        if (_wcsicmp(renderer, L"mdl2") == 0) {
            mode = VolumeRendererMode::SegoeMdl2;
        } else if (_wcsicmp(renderer, L"fluent") == 0) {
            mode = VolumeRendererMode::SegoeFluent;
        } else if (_wcsicmp(renderer, L"sndvol-auto") == 0) {
            mode = VolumeRendererMode::SndVolAutomatic;
        } else if (_wcsicmp(renderer, L"sndvol-dll") == 0) {
            mode = VolumeRendererMode::SndVolDll;
        } else if (_wcsicmp(renderer, L"sndvol-mun") == 0) {
            mode = VolumeRendererMode::SndVolMun;
        }
    }

    Wh_FreeStringSetting(renderer);
    g_volumeRenderer.store(mode);
    ModLog(L"Volume renderer setting: %s", VolumeRendererModeName(mode));

    PCWSTR handoffSetting = Wh_GetStringSetting(L"volume.uiHandoff");
    VolumeUiHandoffMode handoffMode = VolumeUiHandoffMode::Automatic;
    if (handoffSetting) {
        if (_wcsicmp(handoffSetting, L"dispatcherqueue") == 0) {
            handoffMode = VolumeUiHandoffMode::DispatcherQueue;
        } else if (_wcsicmp(handoffSetting, L"coredispatcher") == 0) {
            handoffMode = VolumeUiHandoffMode::CoreDispatcher;
        } else if (_wcsicmp(handoffSetting, L"legacy-message-window") == 0) {
            handoffMode = VolumeUiHandoffMode::LegacyMessageWindow;
        }
    }
    Wh_FreeStringSetting(handoffSetting);
    g_volumeUiHandoff.store(handoffMode);
    ModLog(L"Volume UI handoff setting: %s",
           VolumeUiHandoffModeName(handoffMode));

    PCWSTR batteryClickSetting =
        Wh_GetStringSetting(L"battery.clickAction");
    BatteryClickAction batteryClickAction = BatteryClickAction::Open;
    if (batteryClickSetting &&
        _wcsicmp(batteryClickSetting, L"none") == 0) {
        batteryClickAction = BatteryClickAction::None;
    }
    Wh_FreeStringSetting(batteryClickSetting);
    g_batteryClickAction.store(batteryClickAction);
    ModLog(L"Battery click action: %s",
           BatteryClickActionName(batteryClickAction));

    PCWSTR batteryMiddleClickSetting =
        Wh_GetStringSetting(L"battery.middleClickAction");
    BatteryMiddleClickAction batteryMiddleClickAction =
        BatteryMiddleClickAction::AdvancedPower;
    if (batteryMiddleClickSetting) {
        if (_wcsicmp(batteryMiddleClickSetting, L"open") == 0) {
            batteryMiddleClickAction =
                BatteryMiddleClickAction::OpenFlyout;
        } else if (_wcsicmp(batteryMiddleClickSetting, L"none") == 0) {
            batteryMiddleClickAction =
                BatteryMiddleClickAction::None;
        }
    }
    Wh_FreeStringSetting(batteryMiddleClickSetting);
    g_batteryMiddleClickAction.store(batteryMiddleClickAction);
    ModLog(L"Battery middle-click action: %s",
           BatteryMiddleClickActionName(batteryMiddleClickAction));

    PCWSTR batteryRefreshSetting =
        Wh_GetStringSetting(L"battery.flyout.refresh");
    BatteryFlyoutRefreshMode batteryRefresh =
        BatteryFlyoutRefreshMode::WindowsDefault;
    if (batteryRefreshSetting &&
        _wcsicmp(batteryRefreshSetting, L"event-driven") == 0) {
        batteryRefresh = BatteryFlyoutRefreshMode::EventDriven;
    }
    Wh_FreeStringSetting(batteryRefreshSetting);
    g_batteryFlyoutRefresh.store(batteryRefresh);
    ModLog(L"Battery flyout refresh: %s",
           BatteryFlyoutRefreshModeName(batteryRefresh));

    int batteryEventDebounceDelay =
        Wh_GetIntSetting(L"battery.flyout.eventDebounceDelayMs");
    batteryEventDebounceDelay =
        std::clamp(batteryEventDebounceDelay, 0, 5000);
    g_batteryEventDebounceDelayMs.store(
        static_cast<DWORD>(batteryEventDebounceDelay),
        std::memory_order_relaxed);
    ModLog(L"Battery event debounce delay: %d ms (one-shot; not polling)",
           batteryEventDebounceDelay);

    PCWSTR batteryWheelSetting =
        Wh_GetStringSetting(L"battery.brightnessScroll.action");
    BatteryWheelAction batteryWheelAction =
        (batteryWheelSetting &&
         _wcsicmp(batteryWheelSetting, L"adjust") == 0)
            ? BatteryWheelAction::AdjustBrightness
            : BatteryWheelAction::None;
    Wh_FreeStringSetting(batteryWheelSetting);

    int batteryWheelStep =
        Wh_GetIntSetting(L"battery.brightnessScroll.stepPercent");
    batteryWheelStep = std::clamp(batteryWheelStep, 1, 25);
    const bool batteryWheelReverse =
        Wh_GetIntSetting(L"battery.brightnessScroll.reverse") != 0;

    g_batteryWheelAction.store(batteryWheelAction);
    g_batteryWheelStepPercent.store(batteryWheelStep);
    g_batteryWheelReverse.store(batteryWheelReverse);

    ModLog(L"Battery wheel: action=%s step=%d%% reverse=%d",
           BatteryWheelActionName(batteryWheelAction),
           batteryWheelStep,
           batteryWheelReverse ? 1 : 0);

    int precisionScrollStep =
        Wh_GetIntSetting(L"precisionScrolling.stepPercent");
    precisionScrollStep = std::clamp(precisionScrollStep, 1, 25);
    g_precisionScrollStepPercent.store(
        precisionScrollStep, std::memory_order_relaxed);

    int precisionScrollDeltaPerStep =
        Wh_GetIntSetting(L"precisionScrolling.deltaPerStep");
    precisionScrollDeltaPerStep =
        std::clamp(precisionScrollDeltaPerStep, 1, WHEEL_DELTA);
    g_precisionScrollDeltaPerStep.store(
        precisionScrollDeltaPerStep, std::memory_order_relaxed);

    ModLog(L"Precision/high-resolution scroll: step=%d%% deltaPerStep=%d (shared Battery/Volume)",
           precisionScrollStep,
           precisionScrollDeltaPerStep);

    PCWSTR volumeClickSetting =
        Wh_GetStringSetting(L"volume.clickAction");
    VolumeClickAction volumeClickAction =
        VolumeClickAction::Launch;
    if (volumeClickSetting &&
        _wcsicmp(volumeClickSetting, L"none") == 0) {
        volumeClickAction = VolumeClickAction::None;
    }
    Wh_FreeStringSetting(volumeClickSetting);
    g_volumeClickAction.store(volumeClickAction);
    ModLog(L"Volume click action: %s",
           VolumeClickActionName(volumeClickAction));

    PCWSTR volumeWheelSetting =
        Wh_GetStringSetting(L"volume.scroll.action");
    VolumeWheelAction volumeWheelAction =
        (volumeWheelSetting &&
         _wcsicmp(volumeWheelSetting, L"adjust") == 0)
            ? VolumeWheelAction::Adjust
            : VolumeWheelAction::None;
    Wh_FreeStringSetting(volumeWheelSetting);

    PCWSTR volumeMiddleClickSetting =
        Wh_GetStringSetting(L"volume.middleClickAction");
    VolumeMiddleClickAction volumeMiddleClickAction =
        VolumeMiddleClickAction::ToggleMute;
    if (volumeMiddleClickSetting &&
        _wcsicmp(volumeMiddleClickSetting, L"none") == 0) {
        volumeMiddleClickAction = VolumeMiddleClickAction::None;
    }
    Wh_FreeStringSetting(volumeMiddleClickSetting);
    g_volumeMiddleClickAction.store(volumeMiddleClickAction);
    ModLog(L"Volume middle-click action: %s",
           VolumeMiddleClickActionName(volumeMiddleClickAction));

    int volumeWheelStep =
        Wh_GetIntSetting(L"volume.scroll.stepPercent");
    volumeWheelStep = std::clamp(volumeWheelStep, 1, 25);

    PCWSTR volumeUnmuteBehaviorSetting =
        Wh_GetStringSetting(L"volume.scroll.unmuteBehavior");
    VolumeScrollUnmuteBehavior volumeUnmuteBehavior =
        VolumeScrollUnmuteBehavior::Increase;
    if (volumeUnmuteBehaviorSetting) {
        if (_wcsicmp(volumeUnmuteBehaviorSetting, L"never") == 0) {
            volumeUnmuteBehavior =
                VolumeScrollUnmuteBehavior::Never;
        } else if (_wcsicmp(volumeUnmuteBehaviorSetting, L"decrease") == 0) {
            volumeUnmuteBehavior =
                VolumeScrollUnmuteBehavior::Decrease;
        } else if (_wcsicmp(volumeUnmuteBehaviorSetting, L"either") == 0) {
            volumeUnmuteBehavior =
                VolumeScrollUnmuteBehavior::Either;
        }
    }
    Wh_FreeStringSetting(volumeUnmuteBehaviorSetting);

    int volumeUnmuteThresholdSteps =
        Wh_GetIntSetting(L"volume.scroll.unmuteThresholdSteps");
    volumeUnmuteThresholdSteps =
        std::clamp(volumeUnmuteThresholdSteps, 1, 64);

    const bool volumeMuteAtZero =
        Wh_GetIntSetting(L"volume.scroll.muteAtZero") != 0;
    const bool volumeWheelReverse =
        Wh_GetIntSetting(L"volume.scroll.reverse") != 0;

    g_volumeWheelAction.store(volumeWheelAction);
    g_volumeWheelStepPercent.store(volumeWheelStep);
    g_volumeScrollUnmuteBehavior.store(volumeUnmuteBehavior);
    g_volumeUnmuteThresholdSteps.store(volumeUnmuteThresholdSteps);
    g_volumeMuteAtZero.store(volumeMuteAtZero);
    g_volumeWheelReverse.store(volumeWheelReverse);

    ModLog(L"Volume wheel: action=%s step=%d%% unmute=%s threshold=%d muteAtZero=%d reverse=%d",
           VolumeWheelActionName(volumeWheelAction),
           volumeWheelStep,
           VolumeScrollUnmuteBehaviorName(volumeUnmuteBehavior),
           volumeUnmuteThresholdSteps,
           volumeMuteAtZero ? 1 : 0,
           volumeWheelReverse ? 1 : 0);

    g_showClockButton.store(
        Wh_GetIntSetting(L"clock.showButton") != 0);
    g_clockShowSeconds.store(
        Wh_GetIntSetting(L"clock.showSeconds") != 0);
    g_clockShowDate.store(
        Wh_GetIntSetting(L"clock.showDate") != 0);

    PCWSTR clockClickSetting =
        Wh_GetStringSetting(L"clock.clickAction");
    ClockClickAction clockClickAction =
        ClockClickAction::ClassicFlyout;
    if (clockClickSetting) {
        if (_wcsicmp(clockClickSetting, L"timedate") == 0) {
            clockClickAction = ClockClickAction::DateTimeDialog;
        } else if (_wcsicmp(clockClickSetting, L"none") == 0) {
            clockClickAction = ClockClickAction::None;
        }
    }
    Wh_FreeStringSetting(clockClickSetting);
    g_clockClickAction.store(clockClickAction);

    ModLog(L"Clock: show=%d seconds=%d date=%d click=%s",
           g_showClockButton.load(std::memory_order_relaxed) ? 1 : 0,
           g_clockShowSeconds.load(std::memory_order_relaxed) ? 1 : 0,
           g_clockShowDate.load(std::memory_order_relaxed) ? 1 : 0,
           ClockClickActionName(clockClickAction));

    PCWSTR clockFlyoutPlacementSetting =
        Wh_GetStringSetting(L"clock.flyout.placement");
    ClockPlacementMode clockFlyoutPlacement =
        ClockPlacementMode::AnchorToButton;
    if (clockFlyoutPlacementSetting &&
        _wcsicmp(clockFlyoutPlacementSetting, L"windows") == 0) {
        clockFlyoutPlacement = ClockPlacementMode::WindowsDefault;
    }
    Wh_FreeStringSetting(clockFlyoutPlacementSetting);
    g_clockFlyoutPlacement.store(clockFlyoutPlacement);

    PCWSTR clockFlyoutSideSetting =
        Wh_GetStringSetting(L"clock.flyout.anchorSide");
    const auto clockFlyoutSide =
        ParseAnchorSide(clockFlyoutSideSetting);
    Wh_FreeStringSetting(clockFlyoutSideSetting);
    const int clockFlyoutOffsetX =
        std::clamp(Wh_GetIntSetting(L"clock.flyout.offsetX"), -2000, 2000);
    const int clockFlyoutOffsetY =
        std::clamp(Wh_GetIntSetting(L"clock.flyout.offsetY"), -2000, 2000);
    g_clockFlyoutAnchorSide.store(clockFlyoutSide);
    g_clockFlyoutOffsetX.store(clockFlyoutOffsetX);
    g_clockFlyoutOffsetY.store(clockFlyoutOffsetY);

    ModLog(L"Clock flyout placement: %s side=%s offset=(%d,%d)",
           ClockPlacementModeName(clockFlyoutPlacement),
           AnchorSideName(clockFlyoutSide),
           clockFlyoutOffsetX,
           clockFlyoutOffsetY);

    PCWSTR clockDateTimePlacementSetting =
        Wh_GetStringSetting(L"clock.fullDateTime.placement");
    ClockPlacementMode clockDateTimePlacement =
        ClockPlacementMode::AnchorToButton;
    if (clockDateTimePlacementSetting &&
        _wcsicmp(clockDateTimePlacementSetting, L"windows") == 0) {
        clockDateTimePlacement = ClockPlacementMode::WindowsDefault;
    }
    Wh_FreeStringSetting(clockDateTimePlacementSetting);
    g_clockDateTimePlacement.store(clockDateTimePlacement);

    PCWSTR clockDateTimeSideSetting =
        Wh_GetStringSetting(L"clock.fullDateTime.anchorSide");
    const auto clockDateTimeSide =
        ParseAnchorSide(clockDateTimeSideSetting);
    Wh_FreeStringSetting(clockDateTimeSideSetting);
    const int clockDateTimeOffsetX =
        std::clamp(Wh_GetIntSetting(L"clock.fullDateTime.offsetX"), -2000, 2000);
    const int clockDateTimeOffsetY =
        std::clamp(Wh_GetIntSetting(L"clock.fullDateTime.offsetY"), -2000, 2000);
    g_clockDateTimeAnchorSide.store(clockDateTimeSide);
    g_clockDateTimeOffsetX.store(clockDateTimeOffsetX);
    g_clockDateTimeOffsetY.store(clockDateTimeOffsetY);

    ModLog(L"Full Date and Time placement: %s side=%s offset=(%d,%d)",
           ClockPlacementModeName(clockDateTimePlacement),
           AnchorSideName(clockDateTimeSide),
           clockDateTimeOffsetX,
           clockDateTimeOffsetY);

    PCWSTR batteryPlacementSetting =
        Wh_GetStringSetting(L"battery.flyout.placement");
    BatteryFlyoutPlacementMode batteryPlacement =
        BatteryFlyoutPlacementMode::AnchorToButton;
    if (batteryPlacementSetting &&
        _wcsicmp(batteryPlacementSetting, L"windows") == 0) {
        batteryPlacement = BatteryFlyoutPlacementMode::WindowsDefault;
    }
    Wh_FreeStringSetting(batteryPlacementSetting);
    g_batteryFlyoutPlacement.store(batteryPlacement);
    ModLog(L"Battery flyout placement: %s",
           BatteryFlyoutPlacementModeName(batteryPlacement));

    PCWSTR batteryFlyoutSideSetting =
        Wh_GetStringSetting(L"battery.flyout.anchorSide");
    const auto batteryFlyoutSide =
        ParseAnchorSide(batteryFlyoutSideSetting);
    Wh_FreeStringSetting(batteryFlyoutSideSetting);
    const int batteryFlyoutOffsetX =
        std::clamp(Wh_GetIntSetting(L"battery.flyout.offsetX"), -2000, 2000);
    const int batteryFlyoutOffsetY =
        std::clamp(Wh_GetIntSetting(L"battery.flyout.offsetY"), -2000, 2000);
    g_batteryFlyoutAnchorSide.store(batteryFlyoutSide);
    g_batteryFlyoutOffsetX.store(batteryFlyoutOffsetX);
    g_batteryFlyoutOffsetY.store(batteryFlyoutOffsetY);
    ModLog(L"Battery flyout anchor: side=%s offset=(%d,%d)",
           AnchorSideName(batteryFlyoutSide),
           batteryFlyoutOffsetX,
           batteryFlyoutOffsetY);

    PCWSTR morePowerSetting =
        Wh_GetStringSetting(L"battery.advancedPower.action");
    MorePowerOptionsAction morePowerAction =
        MorePowerOptionsAction::AdvancedPowerOptions;
    if (morePowerSetting &&
        _wcsicmp(morePowerSetting, L"windows") == 0) {
        morePowerAction = MorePowerOptionsAction::WindowsDefault;
    }
    Wh_FreeStringSetting(morePowerSetting);
    g_morePowerOptionsAction.store(morePowerAction);
    ModLog(L"More power options action: %s",
           MorePowerOptionsActionName(morePowerAction));

    PCWSTR advancedPowerPlacementSetting =
        Wh_GetStringSetting(L"battery.advancedPower.placement");
    AdvancedPowerPlacementMode advancedPowerPlacement =
        AdvancedPowerPlacementMode::AnchorToBatteryButton;
    if (advancedPowerPlacementSetting &&
        _wcsicmp(advancedPowerPlacementSetting, L"windows") == 0) {
        advancedPowerPlacement =
            AdvancedPowerPlacementMode::WindowsDefault;
    }
    Wh_FreeStringSetting(advancedPowerPlacementSetting);
    g_advancedPowerPlacement.store(advancedPowerPlacement);
    ModLog(L"Advanced Power Options placement: %s",
           AdvancedPowerPlacementModeName(advancedPowerPlacement));

    PCWSTR advancedPowerSideSetting =
        Wh_GetStringSetting(L"battery.advancedPower.anchorSide");
    const auto advancedPowerSide =
        ParseAnchorSide(advancedPowerSideSetting);
    Wh_FreeStringSetting(advancedPowerSideSetting);
    const int advancedPowerOffsetX =
        std::clamp(Wh_GetIntSetting(L"battery.advancedPower.offsetX"), -2000, 2000);
    const int advancedPowerOffsetY =
        std::clamp(Wh_GetIntSetting(L"battery.advancedPower.offsetY"), -2000, 2000);
    g_advancedPowerAnchorSide.store(advancedPowerSide);
    g_advancedPowerOffsetX.store(advancedPowerOffsetX);
    g_advancedPowerOffsetY.store(advancedPowerOffsetY);
    ModLog(L"Advanced Power anchor: side=%s offset=(%d,%d)",
           AnchorSideName(advancedPowerSide),
           advancedPowerOffsetX,
           advancedPowerOffsetY);

    PCWSTR sndVolPlacementSetting =
        Wh_GetStringSetting(L"volume.sndVol.compact.placement");
    SndVolPlacementMode sndVolPlacement =
        SndVolPlacementMode::AnchorToButton;
    if (sndVolPlacementSetting &&
        _wcsicmp(sndVolPlacementSetting, L"windows") == 0) {
        sndVolPlacement = SndVolPlacementMode::WindowsDefault;
    }
    Wh_FreeStringSetting(sndVolPlacementSetting);
    g_sndVolPlacement.store(sndVolPlacement);
    ModLog(L"SndVol placement: %s",
           SndVolPlacementModeName(sndVolPlacement));

    PCWSTR sndVolSideSetting =
        Wh_GetStringSetting(L"volume.sndVol.compact.anchorSide");
    const auto sndVolSide = ParseAnchorSide(sndVolSideSetting);
    Wh_FreeStringSetting(sndVolSideSetting);
    const int sndVolOffsetX =
        std::clamp(Wh_GetIntSetting(L"volume.sndVol.compact.offsetX"), -2000, 2000);
    const int sndVolOffsetY =
        std::clamp(Wh_GetIntSetting(L"volume.sndVol.compact.offsetY"), -2000, 2000);
    g_sndVolAnchorSide.store(sndVolSide);
    g_sndVolOffsetX.store(sndVolOffsetX);
    g_sndVolOffsetY.store(sndVolOffsetY);
    ModLog(L"SndVol compact anchor: side=%s offset=(%d,%d)",
           AnchorSideName(sndVolSide), sndVolOffsetX, sndVolOffsetY);

    PCWSTR sndVolMixerPlacementSetting =
        Wh_GetStringSetting(L"volume.sndVol.mixer.placement");
    SndVolPlacementMode sndVolMixerPlacement =
        SndVolPlacementMode::AnchorToButton;
    if (sndVolMixerPlacementSetting &&
        _wcsicmp(sndVolMixerPlacementSetting, L"windows") == 0) {
        sndVolMixerPlacement = SndVolPlacementMode::WindowsDefault;
    }
    Wh_FreeStringSetting(sndVolMixerPlacementSetting);
    g_sndVolMixerPlacement.store(sndVolMixerPlacement);
    ModLog(L"SndVol mixer placement: %s",
           SndVolPlacementModeName(sndVolMixerPlacement));

    PCWSTR sndVolMixerSideSetting =
        Wh_GetStringSetting(L"volume.sndVol.mixer.anchorSide");
    const auto sndVolMixerSide =
        ParseAnchorSide(sndVolMixerSideSetting);
    Wh_FreeStringSetting(sndVolMixerSideSetting);
    const int sndVolMixerOffsetX =
        std::clamp(Wh_GetIntSetting(L"volume.sndVol.mixer.offsetX"), -2000, 2000);
    const int sndVolMixerOffsetY =
        std::clamp(Wh_GetIntSetting(L"volume.sndVol.mixer.offsetY"), -2000, 2000);
    g_sndVolMixerAnchorSide.store(sndVolMixerSide);
    g_sndVolMixerOffsetX.store(sndVolMixerOffsetX);
    g_sndVolMixerOffsetY.store(sndVolMixerOffsetY);
    ModLog(L"SndVol mixer anchor: side=%s offset=(%d,%d)",
           AnchorSideName(sndVolMixerSide),
           sndVolMixerOffsetX,
           sndVolMixerOffsetY);

    PCWSTR sndVolParametersSetting =
        Wh_GetStringSetting(L"volume.sndVol.launchParameters");
    std::wstring sndVolParameters =
        sndVolParametersSetting ? std::wstring(sndVolParametersSetting)
                                : std::wstring(L"-f");
    Wh_FreeStringSetting(sndVolParametersSetting);

    PCWSTR buttonOrder = Wh_GetStringSetting(L"layout.buttonOrder");
    std::wstring order =
        (buttonOrder && *buttonOrder)
            ? std::wstring(buttonOrder)
            : std::wstring(L"battery,volume,clock,ime,input,network-signin,disconnect,network,accessibility,power");
    Wh_FreeStringSetting(buttonOrder);

    AcquireSRWLockExclusive(&g_settingsLock);
    g_buttonOrder = order;
    g_sndVolLaunchParameters = sndVolParameters;
    g_percentageFontFamily = percentageFontFamily;
    ReleaseSRWLockExclusive(&g_settingsLock);
    ModLog(L"Button order setting: %s", order.c_str());
    ModLog(L"SndVol launch parameters: %s",
           sndVolParameters.empty() ? L"<empty>" : sndVolParameters.c_str());
    ModLog(L"Native visibility: ime=%d input=%d networkSignIn=%d disconnect=%d network=%d accessibility=%d power=%d other=%d",
           showImeMode ? 1 : 0,
           showInput ? 1 : 0,
           showNetworkSignIn ? 1 : 0,
           showDisconnect ? 1 : 0,
           showNetwork ? 1 : 0,
           showAccessibility ? 1 : 0,
           showPower ? 1 : 0,
           showOtherNative ? 1 : 0);
    if (nativeImeInputSpacing < 0) {
        ModLog(L"Reordered IME/Input external spacing: Automatic; IME glyph visual offset: %d DIP",
               imeModeVisualOffsetX);
    } else {
        ModLog(L"Reordered IME/Input external spacing: %d DIP; IME glyph visual offset: %d DIP",
               nativeImeInputSpacing,
               imeModeVisualOffsetX);
    }
    const wchar_t* batteryVisibilityName =
        !showBattery
            ? L"Hide"
            : automaticBatteryVisibility
                ? L"Automatic"
                : L"Always show";
    ModLog(L"Injected buttons: battery=%s volume=%d percentages: battery=%d(%s) volume=%d(%s) font=%s size=%d spacing=%d sign=%d",
           batteryVisibilityName,
           showVolume ? 1 : 0,
           showBatteryPercentage ? 1 : 0,
           PercentagePositionName(batteryPercentagePosition),
           showVolumePercentage ? 1 : 0,
           PercentagePositionName(volumePercentagePosition),
           percentageFontFamily.c_str(),
           percentageFontSize,
           percentageSpacing,
           percentageShowPercentSign ? 1 : 0);
    ModLog(L"Diagnostics: logging=%d fileLogging=%d verbose=%d xamlCapture=%d filePath=%s captureDir=%s",
           enableLogging ? 1 : 0,
           enableFileLogging ? 1 : 0,
           enableVerboseDiagnostics ? 1 : 0,
           enableXamlCapture ? 1 : 0,
           fileLogPath.empty() ? L"<empty>" : fileLogPath.c_str(),
           xamlCaptureDirectory.empty() ? L"<empty>" : xamlCaptureDirectory.c_str());
}

std::wstring GetButtonOrderSetting() {
    AcquireSRWLockShared(&g_settingsLock);
    std::wstring value = g_buttonOrder;
    ReleaseSRWLockShared(&g_settingsLock);
    return value;
}

std::wstring GetSndVolLaunchParameters() {
    AcquireSRWLockShared(&g_settingsLock);
    std::wstring value = g_sndVolLaunchParameters;
    ReleaseSRWLockShared(&g_settingsLock);
    return value;
}

std::wstring GetPercentageFontFamilySetting() {
    AcquireSRWLockShared(&g_settingsLock);
    std::wstring value = g_percentageFontFamily;
    ReleaseSRWLockShared(&g_settingsLock);
    return value.empty() ? std::wstring(L"Segoe UI") : value;
}

bool GetLoadedModulePeIdentity(HMODULE module,
                               DWORD* timeDateStamp,
                               DWORD* sizeOfImage) {
    if (!module || !timeDateStamp || !sizeOfImage) {
        return false;
    }

    // Windhawk's Clang toolchain doesn't support MSVC __try/__except.
    // The module base comes from GetModuleHandle/LoadLibraryEx, so validate the
    // PE headers normally and fail closed if they don't match expectations.
    auto base = reinterpret_cast<const BYTE*>(module);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
        dos->e_lfanew <= 0 ||
        dos->e_lfanew > 0x100000) {
        return false;
    }

    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return false;
    }

    *timeDateStamp = nt->FileHeader.TimeDateStamp;
    *sizeOfImage = nt->OptionalHeader.SizeOfImage;
    return true;
}

bool IsShowBatteryUiSymbolName(PCWSTR symbol) {
    if (!symbol) {
        return false;
    }

    if (_wcsicmp(symbol, L"ShowBatteryUI") == 0) {
        return true;
    }

    const std::wstring_view value(symbol);
    const size_t pos = value.find(L"ShowBatteryUI");
    if (pos == std::wstring_view::npos) {
        return false;
    }

    // Accept a full undecorated signature if Windhawk/DbgHelp emits one, but
    // avoid accidentally matching unrelated helper/local symbols.
    const size_t after = pos + wcslen(L"ShowBatteryUI");
    return (pos == 0 || value[pos - 1] == L' ') &&
           (after == value.size() || value[after] == L'(');
}

bool IsStobjectBatteryStateSymbolName(PCWSTR symbol) {
    if (!symbol) {
        return false;
    }

    const std::wstring_view value(symbol);
    if (_wcsicmp(symbol, L"g_bs") == 0) {
        return true;
    }

    // Public PDBs commonly expose this global as:
    // ?g_bs@@3U_BATTERY_STATE@@A
    return value.find(L"?g_bs@@") != std::wstring_view::npos;
}

bool IsLaunchPowerCplSymbolName(PCWSTR symbol) {
    if (!symbol) {
        return false;
    }

    if (_wcsicmp(symbol, L"LaunchPowerCPL") == 0) {
        return true;
    }

    const std::wstring_view value(symbol);
    const size_t pos = value.find(L"LaunchPowerCPL");
    if (pos == std::wstring_view::npos) {
        return false;
    }

    const size_t after = pos + wcslen(L"LaunchPowerCPL");
    return (pos == 0 || value[pos - 1] == L' ') &&
           (after == value.size() || value[after] == L'(');
}




bool IsClassicClockShowFlyoutSymbolName(PCWSTR symbol) {
    if (!symbol) {
        return false;
    }

    const std::wstring_view value(symbol);
    if (value ==
        L"?ShowFlyout@CTrayClock@@UEAAJPEAUHWND__@@PEBUtagRECT@@@Z") {
        return true;
    }

    // DbgHelp/Windhawk can expose an undecorated signature instead. Keep the
    // match scoped to the class+method and its two native arguments; ambiguity
    // is rejected later by requiring one distinct address.
    return value.find(L"CTrayClock::ShowFlyout") != std::wstring_view::npos &&
           value.find(L"HWND") != std::wstring_view::npos &&
           (value.find(L"RECT") != std::wstring_view::npos ||
            value.find(L"tagRECT") != std::wstring_view::npos);
}

bool IsClassicClockHideFlyoutSymbolName(PCWSTR symbol) {
    if (!symbol) {
        return false;
    }

    const std::wstring_view value(symbol);
    if (value == L"?HideFlyout@CTrayClock@@UEAAJXZ") {
        return true;
    }

    return value.find(L"CTrayClock::HideFlyout") != std::wstring_view::npos;
}

struct StobjectPeView {
    BYTE* base = nullptr;
    const IMAGE_NT_HEADERS64* nt = nullptr;
    const IMAGE_SECTION_HEADER* sections = nullptr;
    WORD sectionCount = 0;
};

bool GetStobjectPeView(HMODULE module, StobjectPeView* view) {
    if (!module || !view) {
        return false;
    }

    auto base = reinterpret_cast<BYTE*>(module);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
        dos->e_lfanew <= 0 ||
        dos->e_lfanew > 0x100000) {
        return false;
    }

    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.SizeOfImage < 0x1000) {
        return false;
    }

    view->base = base;
    view->nt = nt;
    view->sections = IMAGE_FIRST_SECTION(
        const_cast<IMAGE_NT_HEADERS64*>(nt));
    view->sectionCount = nt->FileHeader.NumberOfSections;
    return true;
}

size_t PeSectionVirtualSpan(const IMAGE_SECTION_HEADER& section) {
    return std::max<size_t>(
        section.Misc.VirtualSize,
        section.SizeOfRawData);
}

const IMAGE_SECTION_HEADER* FindPeSectionByName(
        const StobjectPeView& view,
        const char* name) {
    if (!name) {
        return nullptr;
    }

    for (WORD i = 0; i < view.sectionCount; ++i) {
        char sectionName[IMAGE_SIZEOF_SHORT_NAME + 1]{};
        memcpy(sectionName,
               view.sections[i].Name,
               IMAGE_SIZEOF_SHORT_NAME);
        if (strcmp(sectionName, name) == 0) {
            return &view.sections[i];
        }
    }

    return nullptr;
}

const IMAGE_SECTION_HEADER* FindPeSectionForRva(
        const StobjectPeView& view,
        DWORD rva) {
    for (WORD i = 0; i < view.sectionCount; ++i) {
        const auto& section = view.sections[i];
        const size_t span = PeSectionVirtualSpan(section);
        if (rva >= section.VirtualAddress &&
            static_cast<uint64_t>(rva) <
                static_cast<uint64_t>(section.VirtualAddress) + span) {
            return &section;
        }
    }

    return nullptr;
}

bool IsPeRvaRangeValid(
        const StobjectPeView& view,
        DWORD rva,
        size_t size) {
    const uint64_t end =
        static_cast<uint64_t>(rva) + static_cast<uint64_t>(size);
    return end >= rva &&
           end <= view.nt->OptionalHeader.SizeOfImage;
}

bool IsWritableNonExecutableRva(
        const StobjectPeView& view,
        DWORD rva,
        size_t size) {
    const auto* section = FindPeSectionForRva(view, rva);
    if (!section) {
        return false;
    }

    if ((section->Characteristics & IMAGE_SCN_MEM_WRITE) == 0 ||
        (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) {
        return false;
    }

    const uint64_t sectionEnd =
        static_cast<uint64_t>(section->VirtualAddress) +
        PeSectionVirtualSpan(*section);
    const uint64_t rangeEnd =
        static_cast<uint64_t>(rva) + size;
    return rangeEnd >= rva && rangeEnd <= sectionEnd;
}

bool FindContainingRuntimeFunction(
        const StobjectPeView& view,
        DWORD rva,
        DWORD* beginRva,
        DWORD* endRva) {
    if (!beginRva || !endRva) {
        return false;
    }

    const auto& dir =
        view.nt->OptionalHeader
            .DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!dir.VirtualAddress ||
        dir.Size < sizeof(RUNTIME_FUNCTION) ||
        !IsPeRvaRangeValid(view, dir.VirtualAddress, dir.Size)) {
        return false;
    }

    auto functions = reinterpret_cast<const RUNTIME_FUNCTION*>(
        view.base + dir.VirtualAddress);
    const size_t count = dir.Size / sizeof(RUNTIME_FUNCTION);

    for (size_t i = 0; i < count; ++i) {
        const DWORD begin = functions[i].BeginAddress;
        const DWORD end = functions[i].EndAddress;
        if (begin < end && rva >= begin && rva < end) {
            *beginRva = begin;
            *endRva = end;
            return true;
        }
    }

    return false;
}

void PushUniqueRva(std::vector<DWORD>* values, DWORD value) {
    if (!values) {
        return;
    }

    if (std::find(values->begin(), values->end(), value) ==
        values->end()) {
        values->push_back(value);
    }
}

std::vector<DWORD> FindWideLiteralRvas(
        const StobjectPeView& view,
        std::wstring_view literal) {
    std::vector<DWORD> matches;
    if (literal.empty()) {
        return matches;
    }

    const size_t literalBytes =
        literal.size() * sizeof(wchar_t);
    const size_t totalBytes =
        literalBytes + sizeof(wchar_t);

    for (WORD i = 0; i < view.sectionCount; ++i) {
        const auto& section = view.sections[i];

        // String literals belong in readable, non-executable image data.
        if ((section.Characteristics & IMAGE_SCN_MEM_READ) == 0 ||
            (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) {
            continue;
        }

        const size_t span = PeSectionVirtualSpan(section);
        if (span < totalBytes ||
            !IsPeRvaRangeValid(
                view,
                section.VirtualAddress,
                span)) {
            continue;
        }

        const BYTE* bytes =
            view.base + section.VirtualAddress;
        for (size_t offset = 0;
             offset + totalBytes <= span;
             ++offset) {
            if (memcmp(bytes + offset,
                       literal.data(),
                       literalBytes) != 0) {
                continue;
            }

            wchar_t terminator = L'\0';
            memcpy(&terminator,
                   bytes + offset + literalBytes,
                   sizeof(terminator));
            if (terminator != L'\0') {
                continue;
            }

            PushUniqueRva(
                &matches,
                section.VirtualAddress +
                    static_cast<DWORD>(offset));
        }
    }

    return matches;
}

std::vector<DWORD> FindRipRelativeLeaXrefs(
        const StobjectPeView& view,
        DWORD targetRva) {
    std::vector<DWORD> xrefs;
    const auto* text = FindPeSectionByName(view, ".text");
    if (!text) {
        return xrefs;
    }

    const size_t span = PeSectionVirtualSpan(*text);
    if (span < 7 ||
        !IsPeRvaRangeValid(
            view,
            text->VirtualAddress,
            span)) {
        return xrefs;
    }

    const BYTE* bytes =
        view.base + text->VirtualAddress;

    for (size_t offset = 0; offset + 7 <= span; ++offset) {
        const BYTE rex = bytes[offset];
        const BYTE opcode = bytes[offset + 1];
        const BYTE modrm = bytes[offset + 2];

        // REX.W + LEA r64,[RIP+disp32].
        if ((rex & 0xF8) != 0x48 ||
            opcode != 0x8D ||
            (modrm & 0xC7) != 0x05) {
            continue;
        }

        int32_t displacement = 0;
        memcpy(&displacement, bytes + offset + 3,
               sizeof(displacement));

        const int64_t instructionRva =
            static_cast<int64_t>(text->VirtualAddress) +
            static_cast<int64_t>(offset);
        const int64_t referencedRva =
            instructionRva + 7 + displacement;

        if (referencedRva == targetRva) {
            xrefs.push_back(
                static_cast<DWORD>(instructionRva));
        }
    }

    return xrefs;
}

bool FunctionContainsDirectCallTo(
        const StobjectPeView& view,
        DWORD beginRva,
        DWORD endRva,
        DWORD targetRva) {
    if (beginRva >= endRva ||
        !IsPeRvaRangeValid(
            view,
            beginRva,
            static_cast<size_t>(endRva - beginRva))) {
        return false;
    }

    const BYTE* bytes = view.base + beginRva;
    const size_t size = endRva - beginRva;

    for (size_t offset = 0; offset + 5 <= size; ++offset) {
        if (bytes[offset] != 0xE8) {
            continue;
        }

        int32_t displacement = 0;
        memcpy(&displacement, bytes + offset + 1,
               sizeof(displacement));

        const int64_t callRva =
            static_cast<int64_t>(beginRva) +
            static_cast<int64_t>(offset);
        const int64_t calledRva =
            callRva + 5 + displacement;
        if (calledRva == targetRva) {
            return true;
        }
    }

    return false;
}

std::vector<DWORD> FindDirectCallerFunctions(
        const StobjectPeView& view,
        DWORD targetRva) {
    std::vector<DWORD> callers;
    const auto* text = FindPeSectionByName(view, ".text");
    if (!text) {
        return callers;
    }

    const size_t span = PeSectionVirtualSpan(*text);
    if (span < 5 ||
        !IsPeRvaRangeValid(
            view,
            text->VirtualAddress,
            span)) {
        return callers;
    }

    const BYTE* bytes =
        view.base + text->VirtualAddress;

    for (size_t offset = 0; offset + 5 <= span; ++offset) {
        if (bytes[offset] != 0xE8) {
            continue;
        }

        int32_t displacement = 0;
        memcpy(&displacement, bytes + offset + 1,
               sizeof(displacement));

        const int64_t callRva =
            static_cast<int64_t>(text->VirtualAddress) +
            static_cast<int64_t>(offset);
        const int64_t calledRva =
            callRva + 5 + displacement;
        if (calledRva != targetRva) {
            continue;
        }

        DWORD callerBegin = 0;
        DWORD callerEnd = 0;
        if (FindContainingRuntimeFunction(
                view,
                static_cast<DWORD>(callRva),
                &callerBegin,
                &callerEnd)) {
            PushUniqueRva(&callers, callerBegin);
        }
    }

    return callers;
}

void* ResolveShowBatteryUiStructurally(
        HMODULE module,
        const StobjectPeView& view) {
    const auto literals =
        FindWideLiteralRvas(view, L"BatMeterFlyout");
    if (literals.size() != 1) {
        ModLog(L"Symbol-free Battery resolver: expected one BatMeterFlyout literal, got %u",
               static_cast<unsigned>(literals.size()));
        return nullptr;
    }

    const auto xrefs =
        FindRipRelativeLeaXrefs(view, literals.front());

    std::vector<DWORD> xrefFunctions;
    for (DWORD xref : xrefs) {
        DWORD begin = 0;
        DWORD end = 0;
        if (FindContainingRuntimeFunction(
                view, xref, &begin, &end)) {
            PushUniqueRva(&xrefFunctions, begin);
        }
    }

    // Across the harvested 17763/18362/19041/22000/22621/26100
    // generations, the BatMeterFlyout literal is referenced by two related
    // functions. CreateFlyoutWindow is the one which directly calls the other
    // literal-referencing helper.
    std::vector<DWORD> createCandidates;
    for (DWORD candidate : xrefFunctions) {
        DWORD candidateBegin = 0;
        DWORD candidateEnd = 0;
        if (!FindContainingRuntimeFunction(
                view,
                candidate,
                &candidateBegin,
                &candidateEnd)) {
            continue;
        }

        for (DWORD other : xrefFunctions) {
            if (other == candidate) {
                continue;
            }

            if (FunctionContainsDirectCallTo(
                    view,
                    candidateBegin,
                    candidateEnd,
                    other)) {
                PushUniqueRva(
                    &createCandidates,
                    candidateBegin);
            }
        }
    }

    if (createCandidates.size() != 1) {
        ModLog(L"Symbol-free Battery resolver: BatMeterFlyout xrefs=%u functions=%u createCandidates=%u; failing closed",
               static_cast<unsigned>(xrefs.size()),
               static_cast<unsigned>(xrefFunctions.size()),
               static_cast<unsigned>(createCandidates.size()));
        return nullptr;
    }

    const DWORD createFlyoutRva =
        createCandidates.front();
    const auto callers =
        FindDirectCallerFunctions(
            view,
            createFlyoutRva);

    if (callers.size() != 1) {
        ModLog(L"Symbol-free Battery resolver: expected one direct caller of CreateFlyoutWindow RVA=0x%X, got %u; failing closed",
               createFlyoutRva,
               static_cast<unsigned>(callers.size()));
        return nullptr;
    }

    const DWORD showBatteryUiRva =
        callers.front();

    ModLog(L"Symbol-free Battery resolver: BatMeterFlyout RVA=0x%X CreateFlyoutWindow RVA=0x%X ShowBatteryUI RVA=0x%X",
           literals.front(),
           createFlyoutRva,
           showBatteryUiRva);

    return reinterpret_cast<BYTE*>(module) +
           showBatteryUiRva;
}

struct DelayImportDescriptorRva {
    DWORD attributes;
    DWORD dllNameRva;
    DWORD moduleHandleRva;
    DWORD importAddressTableRva;
    DWORD importNameTableRva;
    DWORD boundImportAddressTableRva;
    DWORD unloadInformationTableRva;
    DWORD timeDateStamp;
};

bool AsciiStringEqualsAtRva(
        const StobjectPeView& view,
        DWORD rva,
        const char* expected) {
    if (!expected ||
        !IsPeRvaRangeValid(view, rva, 1)) {
        return false;
    }

    const size_t expectedLength = strlen(expected);
    if (!IsPeRvaRangeValid(
            view,
            rva,
            expectedLength + 1)) {
        return false;
    }

    const char* value =
        reinterpret_cast<const char*>(
            view.base + rva);
    return _strnicmp(
               value,
               expected,
               expectedLength) == 0 &&
           value[expectedLength] == '\0';
}

bool FindDelayImportIatRva(
        const StobjectPeView& view,
        const char* dllName,
        const char* functionName,
        DWORD* iatRva) {
    if (!dllName || !functionName || !iatRva) {
        return false;
    }

    const auto& dir =
        view.nt->OptionalHeader
            .DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if (!dir.VirtualAddress ||
        dir.Size < sizeof(DelayImportDescriptorRva) ||
        !IsPeRvaRangeValid(
            view,
            dir.VirtualAddress,
            dir.Size)) {
        return false;
    }

    auto descriptors =
        reinterpret_cast<const DelayImportDescriptorRva*>(
            view.base + dir.VirtualAddress);
    const size_t descriptorCount =
        dir.Size / sizeof(DelayImportDescriptorRva);

    for (size_t i = 0; i < descriptorCount; ++i) {
        const auto& descriptor = descriptors[i];

        if (descriptor.attributes == 0 &&
            descriptor.dllNameRva == 0 &&
            descriptor.importAddressTableRva == 0 &&
            descriptor.importNameTableRva == 0) {
            break;
        }

        // dlattrRva: descriptor fields are RVAs. Every harvested stobject.dll
        // sample uses this form; reject the legacy VA form rather than guessing.
        if ((descriptor.attributes & 1) == 0 ||
            !AsciiStringEqualsAtRva(
                view,
                descriptor.dllNameRva,
                dllName)) {
            continue;
        }

        if (!descriptor.importAddressTableRva ||
            !descriptor.importNameTableRva) {
            return false;
        }

        for (size_t thunkIndex = 0;
             thunkIndex < 512;
             ++thunkIndex) {
            const uint64_t thunkRva64 =
                static_cast<uint64_t>(
                    descriptor.importNameTableRva) +
                thunkIndex * sizeof(IMAGE_THUNK_DATA64);
            if (thunkRva64 > MAXDWORD ||
                !IsPeRvaRangeValid(
                    view,
                    static_cast<DWORD>(thunkRva64),
                    sizeof(IMAGE_THUNK_DATA64))) {
                return false;
            }

            auto thunk =
                reinterpret_cast<const IMAGE_THUNK_DATA64*>(
                    view.base +
                    static_cast<DWORD>(thunkRva64));
            const ULONGLONG value =
                thunk->u1.AddressOfData;
            if (value == 0) {
                break;
            }

            if (IMAGE_SNAP_BY_ORDINAL64(value)) {
                continue;
            }

            if (value > MAXDWORD ||
                !IsPeRvaRangeValid(
                    view,
                    static_cast<DWORD>(value),
                    sizeof(WORD) + 1)) {
                continue;
            }

            const uint64_t nameRva64 =
                value +
                offsetof(IMAGE_IMPORT_BY_NAME, Name);
            if (nameRva64 > MAXDWORD ||
                !AsciiStringEqualsAtRva(
                    view,
                    static_cast<DWORD>(nameRva64),
                    functionName)) {
                continue;
            }

            const uint64_t slotRva64 =
                static_cast<uint64_t>(
                    descriptor.importAddressTableRva) +
                thunkIndex * sizeof(void*);
            if (slotRva64 > MAXDWORD ||
                !IsPeRvaRangeValid(
                    view,
                    static_cast<DWORD>(slotRva64),
                    sizeof(void*))) {
                return false;
            }

            *iatRva =
                static_cast<DWORD>(slotRva64);
            return true;
        }

        return false;
    }

    return false;
}

std::vector<DWORD> FindIndirectCallSitesToIat(
        const StobjectPeView& view,
        DWORD iatRva) {
    std::vector<DWORD> calls;
    const auto* text = FindPeSectionByName(view, ".text");
    if (!text) {
        return calls;
    }

    const size_t span = PeSectionVirtualSpan(*text);
    if (span < 6 ||
        !IsPeRvaRangeValid(
            view,
            text->VirtualAddress,
            span)) {
        return calls;
    }

    const BYTE* bytes =
        view.base + text->VirtualAddress;

    for (size_t offset = 0; offset + 6 <= span; ++offset) {
        // CALL qword ptr [RIP+disp32]
        if (bytes[offset] != 0xFF ||
            bytes[offset + 1] != 0x15) {
            continue;
        }

        int32_t displacement = 0;
        memcpy(&displacement,
               bytes + offset + 2,
               sizeof(displacement));

        const int64_t callRva =
            static_cast<int64_t>(text->VirtualAddress) +
            static_cast<int64_t>(offset);
        const int64_t referencedRva =
            callRva + 6 + displacement;

        if (referencedRva == iatRva) {
            calls.push_back(
                static_cast<DWORD>(callRva));
        }
    }

    return calls;
}

void* ResolveStobjectBatteryStateStructurally(
        HMODULE module,
        const StobjectPeView& view) {
    DWORD queryBatteryDataIatRva = 0;
    if (!FindDelayImportIatRva(
            view,
            "BatMeter.dll",
            "QueryBatteryData",
            &queryBatteryDataIatRva)) {
        ModLog(L"Symbol-free Battery resolver: couldn't locate BatMeter!QueryBatteryData delay-IAT slot");
        return nullptr;
    }

    const auto callSites =
        FindIndirectCallSitesToIat(
            view,
            queryBatteryDataIatRva);

    std::vector<DWORD> candidates;

    for (DWORD callRva : callSites) {
        DWORD functionBegin = 0;
        DWORD functionEnd = 0;
        if (!FindContainingRuntimeFunction(
                view,
                callRva,
                &functionBegin,
                &functionEnd)) {
            continue;
        }

        const DWORD scanBegin =
            std::max<DWORD>(
                functionBegin,
                callRva > 128 ? callRva - 128 : 0);
        if (scanBegin >= callRva) {
            continue;
        }

        std::vector<DWORD> cbSizeTargets;
        std::vector<DWORD> secondArgumentTargets;

        for (DWORD cursor = scanBegin;
             cursor < callRva;
             ++cursor) {
            const size_t remaining =
                static_cast<size_t>(callRva - cursor);
            const BYTE* p = view.base + cursor;

            // mov dword ptr [rip+disp32], 0x80
            // stobject uses this to write BATTERY_STATE::cbSize.
            if (remaining >= 10 &&
                p[0] == 0xC7 &&
                p[1] == 0x05) {
                int32_t displacement = 0;
                DWORD immediate = 0;
                memcpy(&displacement, p + 2,
                       sizeof(displacement));
                memcpy(&immediate, p + 6,
                       sizeof(immediate));

                if (immediate == 0x80) {
                    const int64_t target =
                        static_cast<int64_t>(cursor) +
                        10 + displacement;
                    if (target >= 0 &&
                        target <= MAXDWORD &&
                        IsWritableNonExecutableRva(
                            view,
                            static_cast<DWORD>(target),
                            0x80)) {
                        PushUniqueRva(
                            &cbSizeTargets,
                            static_cast<DWORD>(target));
                    }
                }
            }

            // lea rdx,[rip+disp32]
            // QueryBatteryData's second argument is BATTERY_STATE*.
            if (remaining >= 7 &&
                p[0] == 0x48 &&
                p[1] == 0x8D &&
                p[2] == 0x15) {
                int32_t displacement = 0;
                memcpy(&displacement, p + 3,
                       sizeof(displacement));

                const int64_t target =
                    static_cast<int64_t>(cursor) +
                    7 + displacement;
                if (target >= 0 &&
                    target <= MAXDWORD &&
                    IsWritableNonExecutableRva(
                        view,
                        static_cast<DWORD>(target),
                        0x80)) {
                    PushUniqueRva(
                        &secondArgumentTargets,
                        static_cast<DWORD>(target));
                }
            }
        }

        for (DWORD candidate : cbSizeTargets) {
            if (std::find(
                    secondArgumentTargets.begin(),
                    secondArgumentTargets.end(),
                    candidate) !=
                secondArgumentTargets.end()) {
                PushUniqueRva(
                    &candidates,
                    candidate);
            }
        }
    }

    if (candidates.size() != 1) {
        ModLog(L"Symbol-free Battery resolver: QueryBatteryData IAT RVA=0x%X callSites=%u batteryStateCandidates=%u; failing closed",
               queryBatteryDataIatRva,
               static_cast<unsigned>(callSites.size()),
               static_cast<unsigned>(candidates.size()));
        return nullptr;
    }

    ModLog(L"Symbol-free Battery resolver: QueryBatteryData IAT RVA=0x%X g_bs/BATTERY_STATE RVA=0x%X",
           queryBatteryDataIatRva,
           candidates.front());

    return reinterpret_cast<BYTE*>(module) +
           candidates.front();
}


void* ResolveLaunchPowerCplStructurally(
        HMODULE module,
        const StobjectPeView& view) {
    const auto literals =
        FindWideLiteralRvas(
            view,
            L"Microsoft.PowerOptions");
    if (literals.size() != 1) {
        ModLog(L"Symbol-free Battery resolver: expected one Microsoft.PowerOptions literal, got %u",
               static_cast<unsigned>(literals.size()));
        return nullptr;
    }

    const auto xrefs =
        FindRipRelativeLeaXrefs(
            view,
            literals.front());

    std::vector<DWORD> functions;
    for (DWORD xref : xrefs) {
        DWORD begin = 0;
        DWORD end = 0;
        if (FindContainingRuntimeFunction(
                view,
                xref,
                &begin,
                &end)) {
            PushUniqueRva(&functions, begin);
        }
    }

    if (functions.size() != 1) {
        ModLog(L"Symbol-free Battery resolver: Microsoft.PowerOptions xrefs=%u functions=%u; LaunchPowerCPL left unresolved",
               static_cast<unsigned>(xrefs.size()),
               static_cast<unsigned>(functions.size()));
        return nullptr;
    }

    const DWORD launchPowerCplRva =
        functions.front();
    ModLog(L"Symbol-free Battery resolver: Microsoft.PowerOptions RVA=0x%X LaunchPowerCPL RVA=0x%X",
           literals.front(),
           launchPowerCplRva);

    return reinterpret_cast<BYTE*>(module) +
           launchPowerCplRva;
}

void* ResolveClassicBatteryFlyoutStructurally(
        HMODULE module) {
    StobjectPeView view{};
    if (!GetStobjectPeView(module, &view)) {
        ModLog(L"Symbol-free Battery resolver: invalid stobject.dll PE view");
        return nullptr;
    }

    void* showBatteryUi =
        ResolveShowBatteryUiStructurally(
            module,
            view);
    void* batteryState =
        ResolveStobjectBatteryStateStructurally(
            module,
            view);
    void* launchPowerCpl =
        ResolveLaunchPowerCplStructurally(
            module,
            view);

    if (!showBatteryUi || !batteryState) {
        return nullptr;
    }

    g_stobjectBatteryState.store(
        batteryState,
        std::memory_order_release);
    if (launchPowerCpl) {
        g_launchPowerCplTarget.store(
            launchPowerCpl,
            std::memory_order_release);
    }

    const auto showRva =
        reinterpret_cast<uintptr_t>(showBatteryUi) -
        reinterpret_cast<uintptr_t>(module);
    const auto stateRva =
        reinterpret_cast<uintptr_t>(batteryState) -
        reinterpret_cast<uintptr_t>(module);
    const auto launchRva =
        launchPowerCpl
            ? reinterpret_cast<uintptr_t>(launchPowerCpl) -
                  reinterpret_cast<uintptr_t>(module)
            : 0;

    ModLog(L"Symbol-free Battery resolver succeeded: ShowBatteryUI RVA=0x%llX g_bs/BATTERY_STATE RVA=0x%llX LaunchPowerCPL=%s0x%llX",
           static_cast<unsigned long long>(showRva),
           static_cast<unsigned long long>(stateRva),
           launchPowerCpl ? L"" : L"unresolved/",
           static_cast<unsigned long long>(launchRva));
    return showBatteryUi;
}

void* ResolveShowBatteryUiBySymbols(HMODULE module) {
    ModLog(L"Resolving stobject!ShowBatteryUI from symbols");

    WH_FIND_SYMBOL findData{};
    HANDLE search = Wh_FindFirstSymbol(module, nullptr, &findData);
    if (!search) {
        ModLog(L"Wh_FindFirstSymbol failed for stobject.dll; classic Battery flyout unavailable");
        return nullptr;
    }

    struct SearchGuard {
        HANDLE handle;
        ~SearchGuard() {
            if (handle) {
                Wh_FindCloseSymbol(handle);
            }
        }
    } guard{search};

    std::vector<void*> distinctTargets;
    std::vector<void*> distinctBatteryStates;
    std::vector<void*> distinctLaunchPowerTargets;
    unsigned rawMatchCount = 0;
    unsigned rawStateMatchCount = 0;
    unsigned rawLaunchPowerMatchCount = 0;

    do {
        if (IsShowBatteryUiSymbolName(findData.symbol)) {
            ++rawMatchCount;
            ModLog(L"stobject ShowBatteryUI symbol candidate: %s at %p",
                   findData.symbol ? findData.symbol : L"<null>",
                   findData.address);

            if (findData.address &&
                std::find(distinctTargets.begin(),
                          distinctTargets.end(),
                          findData.address) == distinctTargets.end()) {
                distinctTargets.push_back(findData.address);
            }
        }

        if (IsStobjectBatteryStateSymbolName(findData.symbol)) {
            ++rawStateMatchCount;
            ModLog(L"stobject g_bs symbol candidate: %s at %p",
                   findData.symbol ? findData.symbol : L"<null>",
                   findData.address);

            if (findData.address &&
                std::find(distinctBatteryStates.begin(),
                          distinctBatteryStates.end(),
                          findData.address) == distinctBatteryStates.end()) {
                distinctBatteryStates.push_back(findData.address);
            }
        }

        if (IsLaunchPowerCplSymbolName(findData.symbol)) {
            ++rawLaunchPowerMatchCount;
            ModLog(L"stobject LaunchPowerCPL symbol candidate: %s at %p",
                   findData.symbol ? findData.symbol : L"<null>",
                   findData.address);

            if (findData.address &&
                std::find(distinctLaunchPowerTargets.begin(),
                          distinctLaunchPowerTargets.end(),
                          findData.address) ==
                    distinctLaunchPowerTargets.end()) {
                distinctLaunchPowerTargets.push_back(findData.address);
            }
        }

    } while (Wh_FindNextSymbol(search, &findData));

    if (distinctTargets.size() != 1 ||
        distinctBatteryStates.size() != 1) {
        ModLog(L"Expected one distinct ShowBatteryUI and g_bs address; got ShowBatteryUI=%u raw/%u distinct, g_bs=%u raw/%u distinct; failing closed",
               rawMatchCount,
               static_cast<unsigned>(distinctTargets.size()),
               rawStateMatchCount,
               static_cast<unsigned>(distinctBatteryStates.size()));
        return nullptr;
    }

    g_stobjectBatteryState.store(distinctBatteryStates.front(),
                                 std::memory_order_release);

    if (distinctLaunchPowerTargets.size() == 1) {
        g_launchPowerCplTarget.store(distinctLaunchPowerTargets.front(),
                                     std::memory_order_release);
    } else if (rawLaunchPowerMatchCount != 0) {
        ModLog(L"LaunchPowerCPL symbol was ambiguous (%u raw/%u distinct); direct Advanced Power Options interception will remain unavailable",
               rawLaunchPowerMatchCount,
               static_cast<unsigned>(distinctLaunchPowerTargets.size()));
    } else {
        ModLog(L"LaunchPowerCPL symbol wasn't found; Battery flyout will still work but its native More power options path will be left untouched");
    }

    void* target = distinctTargets.front();
    const auto rva = reinterpret_cast<uintptr_t>(target) -
                     reinterpret_cast<uintptr_t>(module);
    ModLog(L"Resolved stobject!ShowBatteryUI RVA=0x%llX",
           static_cast<unsigned long long>(rva));
    return target;
}

void* ResolveClassicBatteryFlyoutTargetBlocking() {
    if (void* cached = g_showBatteryUiTarget.load()) {
        return cached;
    }

    HMODULE module = g_stobjectModule.load();
    if (!module) {
        module = GetModuleHandleW(L"stobject.dll");
        if (!module) {
            module = LoadLibraryExW(L"stobject.dll",
                                    nullptr,
                                    LOAD_LIBRARY_SEARCH_SYSTEM32);
        }
        if (!module) {
            ModLog(L"Couldn't load System32\\stobject.dll: error=%lu",
                   GetLastError());
            return nullptr;
        }
        g_stobjectModule.store(module);
    }

    DWORD timestamp = 0;
    DWORD sizeOfImage = 0;
    if (!GetLoadedModulePeIdentity(module, &timestamp, &sizeOfImage)) {
        ModLog(L"Couldn't read stobject.dll PE identity; failing closed");
        return nullptr;
    }

    ModLog(L"stobject.dll identity: timestamp=0x%08X size=0x%X",
           timestamp, sizeOfImage);

    void* target = nullptr;

    // Windows 11 25H2 build 26200.6584, supplied and statically verified.
    // PDB: stobject.pdb 41F4C92C-CD15-6B03-2BE5-DFA9211C4107 age 1.
    if (timestamp == 0x6EDCBE49 && sizeOfImage == 0x00060000) {
        constexpr uintptr_t kShowBatteryUiRva = 0x26614;
        constexpr uintptr_t kLaunchPowerCplRva = 0x21E14;
        constexpr uintptr_t kBatteryStateRva = 0x58780;
        target = reinterpret_cast<BYTE*>(module) + kShowBatteryUiRva;
        g_launchPowerCplTarget.store(
            reinterpret_cast<BYTE*>(module) + kLaunchPowerCplRva,
            std::memory_order_release);
        g_stobjectBatteryState.store(
            reinterpret_cast<BYTE*>(module) + kBatteryStateRva,
            std::memory_order_release);
        ModLog(L"Known stobject.dll fast path: ShowBatteryUI RVA=0x%llX LaunchPowerCPL RVA=0x%llX g_bs RVA=0x%llX",
               static_cast<unsigned long long>(kShowBatteryUiRva),
               static_cast<unsigned long long>(kLaunchPowerCplRva),
               static_cast<unsigned long long>(kBatteryStateRva));
    } else {
        target = ResolveClassicBatteryFlyoutStructurally(module);
        if (!target) {
            ModLog(L"Symbol-free Battery resolver couldn't validate this stobject.dll; falling back to symbols");
            target = ResolveShowBatteryUiBySymbols(module);
        }
    }

    if (target) {
        g_showBatteryUiTarget.store(target);
    }
    return target;
}

bool EnsureBatMeterBackendLoaded() {
    if (g_updateBatteryDataTarget.load(std::memory_order_acquire) &&
        g_queryBatteryDataTarget.load(std::memory_order_acquire)) {
        return true;
    }

    HMODULE module = g_batMeterModule.load(std::memory_order_acquire);
    if (!module) {
        module = GetModuleHandleW(L"BatMeter.dll");
        if (!module) {
            module = LoadLibraryExW(L"BatMeter.dll",
                                    nullptr,
                                    LOAD_LIBRARY_SEARCH_SYSTEM32);
        }
        if (!module) {
            ModLog(L"Couldn't load System32\\BatMeter.dll: error=%lu",
                   GetLastError());
            return false;
        }
        g_batMeterModule.store(module, std::memory_order_release);
    }

    void* update = reinterpret_cast<void*>(
        GetProcAddress(module, "UpdateBatteryData"));
    void* query = reinterpret_cast<void*>(
        GetProcAddress(module, "QueryBatteryData"));
    void* cleanup = reinterpret_cast<void*>(
        GetProcAddress(module, "CleanupBatteryData"));

    if (!update || !query) {
        ModLog(L"BatMeter.dll is missing UpdateBatteryData/QueryBatteryData exports; refusing classic Battery backend initialization");
        return false;
    }

    g_updateBatteryDataTarget.store(update, std::memory_order_release);
    g_queryBatteryDataTarget.store(query, std::memory_order_release);
    g_cleanupBatteryDataTarget.store(cleanup, std::memory_order_release);

    ModLog(L"BatMeter backend resolved: UpdateBatteryData=%p QueryBatteryData=%p CleanupBatteryData=%p",
           update, query, cleanup);
    return true;
}

bool PrimeClassicBatteryStateBlocking(DWORD waitMilliseconds) {
    void* state = g_stobjectBatteryState.load(std::memory_order_acquire);
    if (!state) {
        ModLog(L"Classic Battery state prime unavailable: stobject!g_bs not resolved");
        return false;
    }

    if (!EnsureBatMeterBackendLoaded()) {
        return false;
    }

    // Reverse-confirmed on the supplied 25H2 build:
    // BATTERY_STATE begins with cbSize=0x80, and stobject itself writes 0x80
    // before calling BatMeter!QueryBatteryData.
    *reinterpret_cast<DWORD*>(state) = 0x80;

    using UpdateBatteryData_t = BOOL(WINAPI*)(DWORD*, void*, DWORD);
    auto updateBatteryData =
        reinterpret_cast<UpdateBatteryData_t>(
            g_updateBatteryDataTarget.load(std::memory_order_acquire));

    const BOOL ok = updateBatteryData(nullptr, state, waitMilliseconds);
    ModLog(L"BatMeter UpdateBatteryData(g_bs, wait=%lu) returned %d",
           waitMilliseconds, static_cast<int>(ok));
    return ok != FALSE;
}

bool RefreshClassicBatteryStateFromCache() {
    void* state = g_stobjectBatteryState.load(std::memory_order_acquire);
    void* queryTarget =
        g_queryBatteryDataTarget.load(std::memory_order_acquire);
    if (!state || !queryTarget) {
        return false;
    }

    *reinterpret_cast<DWORD*>(state) = 0x80;

    using QueryBatteryData_t = BOOL(WINAPI*)(DWORD*, void*);
    auto queryBatteryData =
        reinterpret_cast<QueryBatteryData_t>(queryTarget);

    const BOOL ok = queryBatteryData(nullptr, state);
    ModLog(L"BatMeter QueryBatteryData(g_bs) returned %d",
           static_cast<int>(ok));
    return ok != FALSE;
}

DWORD WINAPI BatteryFlyoutResolverThreadProc(void*) {
    ModLog(L"Battery flyout background resolver started");

    void* target = ResolveClassicBatteryFlyoutTargetBlocking();
    const bool statePrimed =
        target && PrimeClassicBatteryStateBlocking(500);

    if (target && statePrimed) {
        g_batteryFlyoutResolveState.store(
            BatteryFlyoutResolveState::Ready,
            std::memory_order_release);
        ModLog(L"Battery flyout prefetch ready: target=%p state=%p",
               target,
               g_stobjectBatteryState.load(std::memory_order_acquire));
    } else {
        g_batteryFlyoutResolveState.store(
            BatteryFlyoutResolveState::Failed,
            std::memory_order_release);
        ModLog(L"Battery flyout background resolver failed; classic Battery click will remain unavailable");
    }

    return 0;
}

void StartBatteryFlyoutSymbolPrefetch() {
    if (!g_showBatteryButton.load(std::memory_order_relaxed) ||
        g_batteryClickAction.load(std::memory_order_relaxed) ==
            BatteryClickAction::None) {
        return;
    }

    BatteryFlyoutResolveState expected = BatteryFlyoutResolveState::NotStarted;
    if (!g_batteryFlyoutResolveState.compare_exchange_strong(
            expected,
            BatteryFlyoutResolveState::Resolving,
            std::memory_order_acq_rel)) {
        return;
    }

    g_batteryResolverThread = CreateThread(
        nullptr,
        0,
        BatteryFlyoutResolverThreadProc,
        nullptr,
        0,
        nullptr);

    if (!g_batteryResolverThread) {
        const DWORD error = GetLastError();
        g_batteryFlyoutResolveState.store(
            BatteryFlyoutResolveState::Failed,
            std::memory_order_release);
        ModLog(L"Couldn't start Battery flyout resolver worker: error=%lu",
               error);
        return;
    }

    ModLog(L"Battery flyout resolver queued immediately; UI thread will never wait for symbols");
}

struct BatMeterFlyoutSearch {
    HWND hwnd = nullptr;
};

BOOL CALLBACK EnumBatMeterFlyoutWindow(HWND hwnd, LPARAM lParam) {
    auto* search = reinterpret_cast<BatMeterFlyoutSearch*>(lParam);
    if (!search || search->hwnd) {
        return FALSE;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId != GetCurrentProcessId()) {
        return TRUE;
    }

    wchar_t className[128]{};
    if (!GetClassNameW(hwnd, className, ARRAYSIZE(className))) {
        return TRUE;
    }

    if (_wcsicmp(className, L"BatMeterFlyout") == 0) {
        search->hwnd = hwnd;
        return FALSE;
    }

    return TRUE;
}

HWND FindBatMeterFlyoutWindow() {
    BatMeterFlyoutSearch search{};

    // stobject creates the flyout synchronously on the caller's UI thread on
    // the verified builds, so search that thread first.
    EnumThreadWindows(GetCurrentThreadId(),
                      EnumBatMeterFlyoutWindow,
                      reinterpret_cast<LPARAM>(&search));
    if (search.hwnd) {
        return search.hwnd;
    }

    // Fallback in case another supported stobject build owns it on a helper
    // thread in the same LogonUI process.
    EnumWindows(EnumBatMeterFlyoutWindow,
                reinterpret_cast<LPARAM>(&search));
    return search.hwnd;
}

bool TryGetXamlElementScreenRect(
        FrameworkElement const& element,
        RECT* screenRect,
        const wchar_t* logPrefix) {
    if (!element || !screenRect) {
        return false;
    }

    if (!logPrefix) {
        logPrefix = L"XAML element";
    }

    try {
        auto window = Window::Current();
        if (!window) {
            ModLog(L"%s anchor: Window::Current is null", logPrefix);
            return false;
        }

        auto root = window.Content().try_as<FrameworkElement>();
        if (!root) {
            ModLog(L"%s anchor: XAML Window has no FrameworkElement root",
                   logPrefix);
            return false;
        }

        const double rootWidth = root.ActualWidth();
        const double rootHeight = root.ActualHeight();
        const double elementWidth = element.ActualWidth();
        const double elementHeight = element.ActualHeight();
        if (rootWidth <= 0.0 || rootHeight <= 0.0 ||
            elementWidth <= 0.0 || elementHeight <= 0.0) {
            ModLog(L"%s anchor: invalid XAML geometry root=%.1fx%.1f button=%.1fx%.1f",
                   logPrefix,
                   rootWidth,
                   rootHeight,
                   elementWidth,
                   elementHeight);
            return false;
        }

        auto transform = element.TransformToVisual(root);
        const auto origin = transform.TransformPoint(
            winrt::Windows::Foundation::Point{0.0f, 0.0f});

        HWND host = GetForegroundWindow();
        DWORD processId = 0;
        if (host) {
            GetWindowThreadProcessId(host, &processId);
        }
        if (!host || processId != GetCurrentProcessId()) {
            host = GetActiveWindow();
            processId = 0;
            if (host) {
                GetWindowThreadProcessId(host, &processId);
            }
        }
        if (!host || processId != GetCurrentProcessId()) {
            ModLog(L"%s anchor: couldn't identify foreground LogonUI host HWND",
                   logPrefix);
            return false;
        }

        RECT client{};
        if (!GetClientRect(host, &client)) {
            ModLog(L"%s anchor: GetClientRect failed error=%lu",
                   logPrefix,
                   GetLastError());
            return false;
        }

        POINT clientOrigin{0, 0};
        if (!ClientToScreen(host, &clientOrigin)) {
            ModLog(L"%s anchor: ClientToScreen failed error=%lu",
                   logPrefix,
                   GetLastError());
            return false;
        }

        const double clientWidth =
            static_cast<double>(client.right - client.left);
        const double clientHeight =
            static_cast<double>(client.bottom - client.top);
        if (clientWidth <= 0.0 || clientHeight <= 0.0) {
            return false;
        }

        const double scaleX = clientWidth / rootWidth;
        const double scaleY = clientHeight / rootHeight;

        const LONG left = clientOrigin.x +
            static_cast<LONG>(std::lround(origin.X * scaleX));
        const LONG top = clientOrigin.y +
            static_cast<LONG>(std::lround(origin.Y * scaleY));
        const LONG right = left +
            static_cast<LONG>(std::lround(elementWidth * scaleX));
        const LONG bottom = top +
            static_cast<LONG>(std::lround(elementHeight * scaleY));

        *screenRect = RECT{left, top, right, bottom};

        ModLog(L"%s anchor rect=(%ld,%ld)-(%ld,%ld) XAML-to-screen scale=%.3fx%.3f",
               logPrefix,
               left,
               top,
               right,
               bottom,
               scaleX,
               scaleY);
        return true;
    } catch (const winrt::hresult_error& e) {
        ModLog(L"%s anchor geometry failed: 0x%08X %s",
               logPrefix,
               static_cast<unsigned>(e.code().value),
               e.message().c_str());
    } catch (...) {
        ModLog(L"%s anchor geometry failed with unknown exception",
               logPrefix);
    }

    return false;
}

bool RepositionWindowToAnchor(
        HWND hwnd,
        const RECT& anchorRect,
        AnchorSide side,
        int offsetX,
        int offsetY,
        const wchar_t* logPrefix) {
    if (!hwnd) {
        return false;
    }

    RECT windowRect{};
    if (!GetWindowRect(hwnd, &windowRect)) {
        ModLog(L"%s anchor: GetWindowRect failed error=%lu",
               logPrefix, GetLastError());
        return false;
    }

    const LONG width = windowRect.right - windowRect.left;
    const LONG height = windowRect.bottom - windowRect.top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    HMONITOR monitor =
        MonitorFromRect(&anchorRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) {
        ModLog(L"%s anchor: couldn't obtain monitor bounds", logPrefix);
        return false;
    }

    const RECT bounds = monitorInfo.rcMonitor;
    const LONG anchorHeight = anchorRect.bottom - anchorRect.top;
    const LONG gap = std::max<LONG>(4, anchorHeight / 6);

    LONG x = anchorRect.right - width;
    LONG yAbove = anchorRect.top - height - gap;
    LONG yBelow = anchorRect.bottom + gap;
    LONG y = yAbove;

    switch (side) {
        case AnchorSide::Above:
            y = yAbove;
            break;
        case AnchorSide::Below:
            y = yBelow;
            break;
        case AnchorSide::Automatic:
        default:
            y = (yAbove >= bounds.top) ? yAbove : yBelow;
            break;
    }

    x += offsetX;
    y += offsetY;

    x = std::clamp(
        x,
        bounds.left,
        std::max(bounds.left, bounds.right - width));
    y = std::clamp(
        y,
        bounds.top,
        std::max(bounds.top, bounds.bottom - height));

    if (!SetWindowPos(
            hwnd,
            nullptr,
            x,
            y,
            0,
            0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                SWP_NOOWNERZORDER)) {
        ModLog(L"%s anchor: SetWindowPos failed error=%lu",
               logPrefix, GetLastError());
        return false;
    }

    ModLog(L"%s repositioned: hwnd=%p side=%s offset=(%d,%d) pos=(%ld,%ld) size=%ldx%ld",
           logPrefix,
           hwnd,
           AnchorSideName(side),
           offsetX,
           offsetY,
           x,
           y,
           width,
           height);
    return true;
}

bool GetCurrentClockAnchorRect(RECT* rect) {
    if (!rect ||
        !g_clockAnchorValid.load(std::memory_order_acquire)) {
        return false;
    }

    AcquireSRWLockShared(&g_clockAnchorLock);
    *rect = g_clockAnchorRect;
    ReleaseSRWLockShared(&g_clockAnchorLock);
    return true;
}

void StoreClockAnchorRect(const RECT& anchorRect) {
    AcquireSRWLockExclusive(&g_clockAnchorLock);
    g_clockAnchorRect = anchorRect;
    ReleaseSRWLockExclusive(&g_clockAnchorLock);
    g_clockAnchorValid.store(true, std::memory_order_release);
}

bool IsClassicClockFlyoutWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
        return false;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId != GetCurrentProcessId()) {
        return false;
    }

    if (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) {
        return false;
    }

    wchar_t className[128]{};
    return GetClassNameW(hwnd, className, ARRAYSIZE(className)) &&
           _wcsicmp(className, L"ClockFlyoutWindow") == 0;
}

bool RepositionClassicClockWindow(HWND hwnd, const wchar_t* reason) {
    if (!IsClassicClockFlyoutWindow(hwnd)) {
        return false;
    }

    ModLog(L"Classic Clock flyout window %s: hwnd=%p",
           reason ? reason : L"discovered",
           hwnd);

    if (g_clockFlyoutPlacement.load(std::memory_order_relaxed) !=
        ClockPlacementMode::AnchorToButton) {
        return true;
    }

    RECT anchorRect{};
    if (!GetCurrentClockAnchorRect(&anchorRect)) {
        ModLog(L"Classic Clock flyout anchor requested but Clock button geometry isn't cached");
        return true;
    }

    return RepositionWindowToAnchor(
        hwnd,
        anchorRect,
        g_clockFlyoutAnchorSide.load(std::memory_order_relaxed),
        g_clockFlyoutOffsetX.load(std::memory_order_relaxed),
        g_clockFlyoutOffsetY.load(std::memory_order_relaxed),
        L"Classic Clock flyout");
}

BOOL CALLBACK EnumAndRepositionClassicClockWindow(HWND hwnd, LPARAM) {
    RepositionClassicClockWindow(hwnd, L"enumerated");
    return TRUE;
}

void RepositionExistingClassicClockWindows() {
    if (!g_clockAnchorValid.load(std::memory_order_acquire)) {
        return;
    }
    EnumWindows(EnumAndRepositionClassicClockWindow, 0);
}

void CALLBACK ClassicClockWinEventProc(
        HWINEVENTHOOK,
        DWORD event,
        HWND hwnd,
        LONG idObject,
        LONG idChild,
        DWORD,
        DWORD) {
    if (g_unloading.load(std::memory_order_acquire) ||
        event != EVENT_OBJECT_SHOW ||
        idObject != OBJID_WINDOW ||
        idChild != CHILDID_SELF ||
        !hwnd) {
        return;
    }

    RepositionClassicClockWindow(hwnd, L"shown");
}

DWORD WINAPI ClassicClockWindowMonitorThreadProc(void*) {
    const DWORD processId = GetCurrentProcessId();
    HWINEVENTHOOK hook = SetWinEventHook(
        EVENT_OBJECT_SHOW,
        EVENT_OBJECT_SHOW,
        nullptr,
        ClassicClockWinEventProc,
        processId,
        0,
        WINEVENT_OUTOFCONTEXT);

    if (!hook) {
        ModLog(L"Classic Clock monitor: SetWinEventHook failed error=%lu",
               GetLastError());
        return 0;
    }

    ModLog(L"Classic Clock window monitor started (same-process ClockFlyoutWindow SHOW events)");

    RepositionExistingClassicClockWindows();

    while (true) {
        DWORD wait = MsgWaitForMultipleObjects(
            1,
            &g_stopEvent,
            FALSE,
            INFINITE,
            QS_ALLINPUT);

        if (wait == WAIT_OBJECT_0) {
            break;
        }

        if (wait == WAIT_OBJECT_0 + 1) {
            MSG msg{};
            while (PeekMessageW(
                    &msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            ModLog(L"Classic Clock monitor: MsgWaitForMultipleObjects failed error=%lu",
                   GetLastError());
            break;
        }
    }

    UnhookWinEvent(hook);
    ModLog(L"Classic Clock window monitor stopped");
    return 0;
}

bool EnsureClassicClockWindowMonitorStarted() {
    if (g_clockWindowMonitorThread) {
        return true;
    }

    HANDLE thread = CreateThread(
        nullptr,
        0,
        ClassicClockWindowMonitorThreadProc,
        nullptr,
        0,
        nullptr);
    if (!thread) {
        ModLog(L"Classic Clock monitor: CreateThread failed error=%lu",
               GetLastError());
        return false;
    }

    g_clockWindowMonitorThread = thread;
    return true;
}

void UpdateClassicClockFlyoutAnchorRect(const RECT& anchorRect) {
    StoreClockAnchorRect(anchorRect);

    if (EnsureClassicClockWindowMonitorStarted()) {
        // If CTrayClock reused an existing helper window, reposition it now;
        // newly shown windows are handled by the WinEvent hook.
        RepositionExistingClassicClockWindows();
    }
}

bool RepositionClassicBatteryFlyout(const RECT& anchorRect) {
    HWND flyout = FindBatMeterFlyoutWindow();
    if (!flyout) {
        ModLog(L"Battery flyout anchor: couldn't find BatMeterFlyout HWND");
        return false;
    }

    return RepositionWindowToAnchor(
        flyout,
        anchorRect,
        g_batteryFlyoutAnchorSide.load(std::memory_order_relaxed),
        g_batteryFlyoutOffsetX.load(std::memory_order_relaxed),
        g_batteryFlyoutOffsetY.load(std::memory_order_relaxed),
        L"Battery flyout");
}

using LaunchPowerCPL_t = HRESULT(WINAPI*)();
LaunchPowerCPL_t g_launchPowerCplOriginal = nullptr;

thread_local bool g_advancedPowerMainDialogPositioned = false;

bool GetCurrentBatteryAnchorRect(RECT* rect) {
    if (!rect ||
        !g_batteryAnchorValid.load(std::memory_order_acquire)) {
        return false;
    }

    AcquireSRWLockShared(&g_batteryAnchorLock);
    *rect = g_batteryAnchorRect;
    ReleaseSRWLockShared(&g_batteryAnchorLock);
    return true;
}


LRESULT CALLBACK AdvancedPowerCbtHookProc(
        int code,
        WPARAM wParam,
        LPARAM lParam) {
    if (code == HCBT_ACTIVATE &&
        !g_advancedPowerMainDialogPositioned &&
        g_advancedPowerPlacement.load(std::memory_order_relaxed) ==
            AdvancedPowerPlacementMode::AnchorToBatteryButton) {
        HWND hwnd = reinterpret_cast<HWND>(wParam);

        wchar_t className[64]{};
        if (hwnd &&
            GetClassNameW(hwnd, className, ARRAYSIZE(className)) &&
            _wcsicmp(className, L"#32770") == 0 &&
            !(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD)) {
            RECT anchorRect{};
            if (GetCurrentBatteryAnchorRect(&anchorRect)) {
                wchar_t title[256]{};
                GetWindowTextW(hwnd, title, ARRAYSIZE(title));
                ModLog(L"Advanced Power Options dialog activated: hwnd=%p title=%s",
                       hwnd,
                       title[0] ? title : L"<none>");

                if (RepositionWindowToAnchor(
                        hwnd,
                        anchorRect,
                        g_advancedPowerAnchorSide.load(
                            std::memory_order_relaxed),
                        g_advancedPowerOffsetX.load(
                            std::memory_order_relaxed),
                        g_advancedPowerOffsetY.load(
                            std::memory_order_relaxed),
                        L"Advanced Power Options")) {
                    g_advancedPowerMainDialogPositioned = true;
                }
            } else {
                ModLog(L"Advanced Power Options anchor requested but Battery button geometry isn't cached");
            }
        }
    }

    return CallNextHookEx(nullptr, code, wParam, lParam);
}

DWORD WINAPI AdvancedPowerOptionsThreadProc(void*) {
    g_advancedPowerDialogActive.store(true, std::memory_order_release);

    HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitializeCom =
        SUCCEEDED(comHr);

    wchar_t systemDir[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDir, ARRAYSIZE(systemDir));
    if (!length || length >= ARRAYSIZE(systemDir)) {
        ModLog(L"Advanced Power Options: GetSystemDirectoryW failed error=%lu",
               GetLastError());
        if (uninitializeCom) {
            CoUninitialize();
        }
        g_advancedPowerDialogActive.store(false, std::memory_order_release);
        return 0;
    }

    std::wstring path = systemDir;
    if (!path.empty() && path.back() != L'\\') {
        path.push_back(L'\\');
    }
    path += L"powercfg.cpl";

    HMODULE module = LoadLibraryExW(
        path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        ModLog(L"Advanced Power Options: couldn't load %s error=%lu",
               path.c_str(), GetLastError());
        if (uninitializeCom) {
            CoUninitialize();
        }
        g_advancedPowerDialogActive.store(false, std::memory_order_release);
        return 0;
    }

    using CPlApplet_t =
        LONG(CALLBACK*)(HWND, UINT, LPARAM, LPARAM);
    auto cplApplet = reinterpret_cast<CPlApplet_t>(
        GetProcAddress(module, "CPlApplet"));
    if (!cplApplet) {
        ModLog(L"Advanced Power Options: powercfg.cpl has no CPlApplet export");
        FreeLibrary(module);
        if (uninitializeCom) {
            CoUninitialize();
        }
        g_advancedPowerDialogActive.store(false, std::memory_order_release);
        return 0;
    }

    constexpr UINT kCplInit = 1;
    constexpr UINT kCplGetCount = 2;
    constexpr UINT kCplStop = 6;
    constexpr UINT kCplExit = 7;
    constexpr UINT kCplStartWParms = 10;

    LONG initResult = cplApplet(nullptr, kCplInit, 0, 0);
    LONG count = initResult ? cplApplet(nullptr, kCplGetCount, 0, 0) : 0;

    ModLog(L"Advanced Power Options: CPlApplet init=%ld count=%ld",
           initResult, count);

    g_advancedPowerMainDialogPositioned = false;
    HHOOK placementHook = nullptr;
    if (g_advancedPowerPlacement.load(std::memory_order_relaxed) ==
            AdvancedPowerPlacementMode::AnchorToBatteryButton) {
        placementHook = SetWindowsHookExW(
            WH_CBT,
            AdvancedPowerCbtHookProc,
            nullptr,
            GetCurrentThreadId());
        if (!placementHook) {
            ModLog(L"Advanced Power Options: couldn't install CBT placement hook error=%lu",
                   GetLastError());
        }
    }

    if (initResult && count > 0) {
        // Equivalent intent to the classic "control powercfg.cpl,,1" path,
        // but without control.exe / Explorer's Control Panel COM namespace.
        wchar_t startParameters[] = L"1";
        LONG result = cplApplet(
            nullptr,
            kCplStartWParms,
            0,
            reinterpret_cast<LPARAM>(startParameters));
        ModLog(L"Advanced Power Options: CPL_STARTWPARMSW(\"1\") returned %ld",
               result);

        cplApplet(nullptr, kCplStop, 0, 0);
    }

    if (placementHook) {
        UnhookWindowsHookEx(placementHook);
        placementHook = nullptr;
    }

    if (initResult) {
        cplApplet(nullptr, kCplExit, 0, 0);
    }

    FreeLibrary(module);

    if (uninitializeCom) {
        CoUninitialize();
    }

    g_advancedPowerDialogActive.store(false, std::memory_order_release);
    ModLog(L"Advanced Power Options host thread exited");
    return 0;
}

bool LaunchAdvancedPowerOptionsDirect() {
    bool expected = false;
    if (!g_advancedPowerDialogActive.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        ModLog(L"Advanced Power Options is already active; duplicate launch ignored");
        return true;
    }

    // The worker sets this again on entry; pre-setting it closes the race
    // between rapid repeated clicks and thread startup.
    HANDLE thread = CreateThread(
        nullptr,
        0,
        AdvancedPowerOptionsThreadProc,
        nullptr,
        0,
        nullptr);
    if (!thread) {
        g_advancedPowerDialogActive.store(false, std::memory_order_release);
        ModLog(L"Advanced Power Options: CreateThread failed error=%lu",
               GetLastError());
        return false;
    }

    if (g_advancedPowerThread) {
        CloseHandle(g_advancedPowerThread);
    }
    g_advancedPowerThread = thread;

    ModLog(L"Advanced Power Options direct host queued");
    return true;
}



constexpr GUID kClsidTrayClock{
    0xA323554A, 0x0FE1, 0x4E49,
    {0xAE, 0xE1, 0x67, 0x22, 0x46, 0x5D, 0x79, 0x9F}};
constexpr GUID kIidIUnknown{
    0x00000000, 0x0000, 0x0000,
    {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
constexpr GUID kIidIClassFactory{
    0x00000001, 0x0000, 0x0000,
    {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

struct ClassicClockKnownAbi {
    DWORD timestamp;
    DWORD sizeOfImage;
    uintptr_t showFlyoutRva;
    uintptr_t hideFlyoutRva;
};

// Harvested from the user's Winbindex + Microsoft public-PDB corpus on
// 2026-10-16. Every PDB-backed image below independently proved:
//   ShowFlyout: HRESULT CTrayClock::ShowFlyout(HWND, const RECT*)
//   HideFlyout: HRESULT CTrayClock::HideFlyout()
// and CComObject<CTrayClock>'s live vtable uses slots 3/4 for those methods.
// This is a fast-path table, not an OS-build allowlist: an unknown image can
// still be accepted by the exact-symbol fallback below.
constexpr ClassicClockKnownAbi kClassicClockKnownAbis[] = {
    {0x012177C2, 0x00043000, 0x0001F970, 0x0001F8B0}, // Winbindex/PDB corpus image
    {0x02D7C7F1, 0x00080000, 0x00017310, 0x000173F0}, // 10.0.17134.1967 (WinBuild.160101.0800) | releases 17134.1967..17134.2208
    {0x066B34FB, 0x00043000, 0x0001F4E0, 0x0001F420}, // 10.0.26100.8972 (WinBuild.160101.0800) | releases 26200.8973..26200.9168
    {0x07DD4C71, 0x00043000, 0x0001F970, 0x0001F8B0}, // Winbindex/PDB corpus image
    {0x09263304, 0x0003E000, 0x0001AFB0, 0x0001AEF0}, // 10.0.26100.1 (WinBuild.160101.0800)
    {0x0CB30F4C, 0x00043000, 0x00020870, 0x000207D0}, // 10.0.29648.1000 (WinBuild.160101.0800)
    {0x0F20FFBE, 0x00044000, 0x00020C50, 0x00020BA0}, // 10.0.29576.1000 (WinBuild.160101.0800)
    {0x1093FA18, 0x00043000, 0x0001F860, 0x0001F7A0}, // Winbindex/PDB corpus image
    {0x1391FA73, 0x00043000, 0x0001F490, 0x0001F3D0}, // Winbindex/PDB corpus image
    {0x14FE4B2A, 0x00043000, 0x0001F490, 0x0001F3D0}, // Winbindex/PDB corpus image
    {0x154ED539, 0x00043000, 0x0001FA80, 0x0001F9C0}, // Winbindex/PDB corpus image
    {0x1CAD6D9F, 0x00043000, 0x0001F180, 0x0001F0C0}, // 10.0.26100.8115 (WinBuild.160101.0800) | releases 26200.8116..26200.8246
    {0x21FB73D9, 0x00043000, 0x0001F870, 0x0001F7B0}, // Winbindex/PDB corpus image
    {0x23616D4C, 0x00043000, 0x0001F6C0, 0x0001F600}, // Winbindex/PDB corpus image
    {0x267B73CD, 0x00042000, 0x0001B700, 0x0001B640}, // 10.0.28000.1 (WinBuild.160101.0800)
    {0x28E40AEE, 0x00043000, 0x0001FDE0, 0x0001FD20}, // Winbindex/PDB corpus image
    {0x2A4B394A, 0x00043000, 0x0001F6B0, 0x0001F5F0}, // Winbindex/PDB corpus image | releases 28000.2179..28000.2269
    {0x2CF33BED, 0x00043000, 0x0001F960, 0x0001F8A0}, // Winbindex/PDB corpus image
    {0x2FBD65BB, 0x0003E000, 0x0001AFB0, 0x0001AEF0}, // 10.0.26100.2 (WinBuild.160101.0800)
    {0x3139252E, 0x00043000, 0x0001F6C0, 0x0001F600}, // Winbindex/PDB corpus image
    {0x3219FD15, 0x0003D000, 0x0001C600, 0x0001C5C0}, // 10.0.22000.653 (WinBuild.160101.0800) | releases 22000.1042..22000.978
    {0x37ACE983, 0x00043000, 0x0001F4F0, 0x0001F430}, // Winbindex/PDB corpus image
    {0x3CA54C49, 0x00042000, 0x0001E350, 0x0001E290}, // Winbindex/PDB corpus image
    {0x44055AEF, 0x00043000, 0x0001F870, 0x0001F7B0}, // Winbindex/PDB corpus image
    {0x4771E5F0, 0x00043000, 0x0001F6B0, 0x0001F5F0}, // Winbindex/PDB corpus image
    {0x4A36C14C, 0x0003E000, 0x0001AFB0, 0x0001AEF0}, // 10.0.26100.1150 (WinBuild.160101.0800) | releases 26100.1150..26100.4066
    {0x4E512056, 0x00043000, 0x0001F860, 0x0001F7A0}, // Winbindex/PDB corpus image
    {0x503C5CDD, 0x00043000, 0x0001F0D0, 0x0001F010}, // 10.0.26100.7705 (WinBuild.160101.0800) | releases 26200.7705..26200.8039
    {0x50FCCCD9, 0x00043000, 0x0001F4E0, 0x0001F420}, // Winbindex/PDB corpus image
    {0x54F34ECA, 0x00043000, 0x0001F960, 0x0001F8A0}, // Winbindex/PDB corpus image
    {0x559F39CA, 0x0008A000, 0x000193C0, 0x00019360}, // 10.0.10240.16384 (th1.150709-1700)
    {0x5632D6CB, 0x0008A000, 0x00019410, 0x000193B0}, // 10.0.10586.0 (th2_release.151029-1700)
    {0x5789997C, 0x00089000, 0x00019CF0, 0x00019C20}, // 10.0.14393.0 (rs1_release.160715-1616) | releases 14393.351
    {0x5819BF7D, 0x00089000, 0x00019CF0, 0x00019C20}, // 10.0.14393.447 (rs1_release_inmarket.161102-0100) | releases 14393.1066..14393.970
    {0x58568FB7, 0x00043000, 0x0001F4F0, 0x0001F430}, // Winbindex/PDB corpus image
    {0x59FEBCF1, 0x00043000, 0x0001F880, 0x0001F7C0}, // Winbindex/PDB corpus image
    {0x5AE3F977, 0x00089000, 0x00019CF0, 0x00019C20}, // 10.0.14393.2248 (rs1_release.180427-1804) | releases 14393.2248
    {0x5AE409F3, 0x00089000, 0x00019CF0, 0x00019C20}, // 10.0.14393.2273 (rs1_release_1.180427-1811) | releases 14393.2273..14393.4104
    {0x5FF78DB1, 0x00089000, 0x00019E50, 0x00019D80}, // 10.0.14393.4169 (rs1_release.210107-1130) | releases 14393.4169..14393.8594
    {0x5FF7B340, 0x0008A000, 0x000194E0, 0x00019480}, // 10.0.10240.18818 (th1.210107-1259) | releases 10240.18818..10240.21161
    {0x60C1DD41, 0x00041000, 0x0001DCA0, 0x0001DBF0}, // 10.0.29570.1000 (WinBuild.160101.0800)
    {0x62070CE5, 0x00044000, 0x00020B80, 0x00020AE0}, // 10.0.29680.1000 (WinBuild.160101.0800)
    {0x640BCFA4, 0x00043000, 0x0001F4F0, 0x0001F430}, // 10.0.26100.8737 (WinBuild.160101.0800) | releases 26200.8737..26200.8894
    {0x649E599F, 0x00044000, 0x00020B80, 0x00020AE0}, // 10.0.29671.1000 (WinBuild.160101.0800)
    {0x67D06C0B, 0x00043000, 0x0001F0D0, 0x0001F010}, // 10.0.26100.7462 (WinBuild.160101.0800) | releases 26200.7462..26200.7628
    {0x6931005C, 0x00089000, 0x0001A2E0, 0x0001A210}, // 10.0.14393.8688 (rs1_release.251203-1653) | releases 14393.8688..14393.8783
    {0x696F029A, 0x00089000, 0x0001A3E0, 0x0001A310}, // 10.0.14393.8864 (rs1_release.260119-1756) | releases 14393.8868
    {0x69973019, 0x00043000, 0x0001F760, 0x0001F6A0}, // Winbindex/PDB corpus image
    {0x69A2C55B, 0x00089000, 0x0001A010, 0x00019F40}, // 10.0.14393.8957 (rs1_release.260227-2344) | releases 14393.8957..14393.9612
    {0x70468557, 0x00043000, 0x0001F4F0, 0x0001F430}, // Winbindex/PDB corpus image
    {0x73FA4462, 0x00087000, 0x00017060, 0x00017140}, // 10.0.15063.1155 (WinBuild.160101.0800) | releases 15063.1155..15063.2584
    {0x7AE5CA7D, 0x00081000, 0x00017B00, 0x00017BE0}, // 10.0.17763.1 (WinBuild.160101.0800)
    {0x7EF940FA, 0x00042000, 0x0001F280, 0x0001F1E0}, // 10.0.29591.1000 (WinBuild.160101.0800)
    {0x80916474, 0x00043000, 0x0001F520, 0x0001F470}, // Winbindex/PDB corpus image
    {0x854C0581, 0x00041000, 0x0001DC10, 0x0001DB50}, // 10.0.26100.7309 (WinBuild.160101.0800) | releases 26200.7309
    {0x89CA2E05, 0x00044000, 0x00020C50, 0x00020BA0}, // 10.0.29580.1000 (WinBuild.160101.0800)
    {0x8DBCBC02, 0x00043000, 0x0001F4B0, 0x0001F3F0}, // Winbindex/PDB corpus image
    {0x920C3473, 0x00043000, 0x0001F190, 0x0001F0E0}, // Winbindex/PDB corpus image
    {0x92EAEC8A, 0x00044000, 0x00020CB0, 0x00020C00}, // 10.0.29585.1000 (WinBuild.160101.0800)
    {0x93FA5C65, 0x00043000, 0x0001F740, 0x0001F680}, // Winbindex/PDB corpus image | releases 28000.1643..28000.1836
    {0x958A4DEF, 0x00042000, 0x0001B700, 0x0001B640}, // 10.0.28000.4 (WinBuild.160101.0800)
    {0x9803536A, 0x00043000, 0x0001F520, 0x0001F470}, // Winbindex/PDB corpus image
    {0x9A9E5DB6, 0x00043000, 0x0001F4B0, 0x0001F3F0}, // 10.0.26100.8521 (WinBuild.160101.0800) | releases 26200.8524..26200.8655
    {0x9AC32E6B, 0x00043000, 0x0001F6B0, 0x0001F5F0}, // Winbindex/PDB corpus image
    {0x9C0271AA, 0x00043000, 0x00020860, 0x000207C0}, // 10.0.29639.1000 (WinBuild.160101.0800)
    {0x9C370828, 0x00042000, 0x0001F280, 0x0001F1E0}, // 10.0.29599.1000 (WinBuild.160101.0800)
    {0x9CA94ED6, 0x00043000, 0x0001F4F0, 0x0001F430}, // Winbindex/PDB corpus image
    {0x9CC121F3, 0x00044000, 0x00020680, 0x000205E0}, // 10.0.29610.1000 (WinBuild.160101.0800)
    {0x9D1DD251, 0x00043000, 0x0001F880, 0x0001F7C0}, // Winbindex/PDB corpus image
    {0xA0331F41, 0x00046000, 0x000234F0, 0x00023450}, // 10.0.29661.1000 (WinBuild.160101.0800)
    {0xA127BB06, 0x00043000, 0x0001F510, 0x0001F460}, // Winbindex/PDB corpus image
    {0xA418FF2A, 0x00043000, 0x0001F6F0, 0x0001F630}, // Winbindex/PDB corpus image
    {0xA6859CB1, 0x00043000, 0x0001FB00, 0x0001FA50}, // Winbindex/PDB corpus image
    {0xA7FBE053, 0x00041000, 0x0001B140, 0x0001B080}, // 10.0.26100.4202 (WinBuild.160101.0800) | releases 26100.4202..26100.4946
    {0xA99A52D0, 0x00043000, 0x0001F880, 0x0001F7C0}, // Winbindex/PDB corpus image | releases 26300.9550..26300.9711
    {0xAAF0E3F8, 0x00043000, 0x0001F520, 0x0001F470}, // Winbindex/PDB corpus image
    {0xAB0D93B0, 0x00043000, 0x0001F740, 0x0001F680}, // Winbindex/PDB corpus image | releases 28000.1575
    {0xAC433026, 0x00043000, 0x0001F510, 0x0001F460}, // Winbindex/PDB corpus image
    {0xAD1F219C, 0x00043000, 0x0001F870, 0x0001F7B0}, // Winbindex/PDB corpus image
    {0xB1B77351, 0x00043000, 0x0001F520, 0x0001F470}, // Winbindex/PDB corpus image
    {0xB88D250E, 0x00044000, 0x00020680, 0x000205E0}, // 10.0.29617.1000 (WinBuild.160101.0800)
    {0xB9D7FACF, 0x00043000, 0x0001F6C0, 0x0001F600}, // Winbindex/PDB corpus image | releases 28000.2340..28000.2525
    {0xB9EDC1D6, 0x00044000, 0x00020010, 0x0001FF50}, // Winbindex/PDB corpus image
    {0xC27E95AE, 0x00043000, 0x0001F6F0, 0x0001F630}, // Winbindex/PDB corpus image
    {0xC4BF919A, 0x00080000, 0x00016AF0, 0x00016BD0}, // 10.0.16299.248 (WinBuild.160101.0800) | releases 16299.1004..16299.967
    {0xC5265894, 0x00043000, 0x0001F4E0, 0x0001F420}, // Winbindex/PDB corpus image
    {0xC86792E9, 0x00044000, 0x00020B80, 0x00020AE0}, // 10.0.29667.1000 (WinBuild.160101.0800)
    {0xC8F0793A, 0x00043000, 0x0001F780, 0x0001F6C0}, // Winbindex/PDB corpus image
    {0xC9815001, 0x00044000, 0x00020010, 0x0001FF50}, // Winbindex/PDB corpus image | releases 28000.2804..28000.2956
    {0xCC29F83F, 0x00043000, 0x0001F880, 0x0001F7C0}, // Winbindex/PDB corpus image
    {0xCDE8D1D6, 0x00043000, 0x0001FDE0, 0x0001FD20}, // Winbindex/PDB corpus image
    {0xCF4A1705, 0x00043000, 0x0001F760, 0x0001F6A0}, // Winbindex/PDB corpus image
    {0xD1FBCB8D, 0x00043000, 0x0001F490, 0x0001F3D0}, // 10.0.26100.8328 (WinBuild.160101.0800) | releases 26200.8328..26200.8457
    {0xD484B073, 0x00041000, 0x0001B180, 0x0001B0C0}, // 10.0.26100.7019 (WinBuild.160101.0800) | releases 26200.7019..26200.7171
    {0xD6E993F2, 0x00043000, 0x0001F870, 0x0001F7B0}, // 10.0.26100.9278 (WinBuild.160101.0800) | releases 26200.9278..26200.9457
    {0xD817823B, 0x00044000, 0x00020680, 0x000205E0}, // 10.0.29613.1000 (WinBuild.160101.0800)
    {0xD89BBA24, 0x00043000, 0x0001F4B0, 0x0001F3F0}, // Winbindex/PDB corpus image
    {0xD964156E, 0x00087000, 0x00017120, 0x00017200}, // 10.0.15063.2614 (WinBuild.160101.0800) | releases 15063.2614..15063.2679
    {0xDE77D438, 0x00041000, 0x0001B180, 0x0001B0C0}, // 10.0.26100.5074 (WinBuild.160101.0800) | releases 26100.5074..26200.6901
    {0xE075CA54, 0x00043000, 0x0001F6F0, 0x0001F630}, // Winbindex/PDB corpus image | releases 28000.2608..28000.2704
    {0xE116CC89, 0x00042000, 0x0001F280, 0x0001F1E0}, // 10.0.29595.1000 (WinBuild.160101.0800)
    {0xE5438C1F, 0x00043000, 0x0001FA80, 0x0001F9C0}, // Winbindex/PDB corpus image | releases 28000.3086..28000.3231
    {0xE5EEEEB8, 0x00043000, 0x00020860, 0x000207C0}, // 10.0.29634.1000 (WinBuild.160101.0800)
    {0xE7C8486B, 0x00044000, 0x00020B40, 0x00020AA0}, // 10.0.29683.1000 (WinBuild.160101.0800)
    {0xE8748923, 0x00043000, 0x0001F720, 0x0001F660}, // Winbindex/PDB corpus image
    {0xEB312945, 0x0003F000, 0x00018ED0, 0x00018FE0}, // 10.0.19041.746 (WinBuild.160101.0800) | releases 19041.1023..19045.3516
    {0xED133ABC, 0x00043000, 0x0001F720, 0x0001F660}, // Winbindex/PDB corpus image
    {0xF1A7051D, 0x00043000, 0x0001F6B0, 0x0001F5F0}, // Winbindex/PDB corpus image
    {0xFE990A8F, 0x00043000, 0x0001F6F0, 0x0001F630}, // Winbindex/PDB corpus image
    {0xFF6EE179, 0x00080000, 0x00016AF0, 0x00016BD0}, // 10.0.16299.15 (WinBuild.160101.0800)
};

bool LookupKnownClassicClockAbi(
        DWORD timestamp,
        DWORD sizeOfImage,
        uintptr_t* showFlyoutRva,
        uintptr_t* hideFlyoutRva) {
    if (!showFlyoutRva || !hideFlyoutRva) {
        return false;
    }

    for (const auto& entry : kClassicClockKnownAbis) {
        if (entry.timestamp == timestamp &&
            entry.sizeOfImage == sizeOfImage) {
            *showFlyoutRva = entry.showFlyoutRva;
            *hideFlyoutRva = entry.hideFlyoutRva;
            return true;
        }
    }
    return false;
}

bool ResolveClassicClockAbiBySymbols(
        HMODULE module,
        uintptr_t* showFlyoutRva,
        uintptr_t* hideFlyoutRva) {
    if (!module || !showFlyoutRva || !hideFlyoutRva) {
        return false;
    }

    ModLog(L"Classic Clock: resolving CTrayClock ShowFlyout/HideFlyout from symbols");

    WH_FIND_SYMBOL findData{};
    HANDLE search = Wh_FindFirstSymbol(module, nullptr, &findData);
    if (!search) {
        ModLog(L"Classic Clock: Wh_FindFirstSymbol failed for timedate.cpl; failing closed");
        return false;
    }

    struct SearchGuard {
        HANDLE handle;
        ~SearchGuard() {
            if (handle) {
                Wh_FindCloseSymbol(handle);
            }
        }
    } guard{search};

    std::vector<void*> showTargets;
    std::vector<void*> hideTargets;
    unsigned rawShowMatches = 0;
    unsigned rawHideMatches = 0;

    do {
        if (IsClassicClockShowFlyoutSymbolName(findData.symbol)) {
            ++rawShowMatches;
            if (findData.address &&
                std::find(showTargets.begin(), showTargets.end(),
                          findData.address) == showTargets.end()) {
                showTargets.push_back(findData.address);
            }
        }
        if (IsClassicClockHideFlyoutSymbolName(findData.symbol)) {
            ++rawHideMatches;
            if (findData.address &&
                std::find(hideTargets.begin(), hideTargets.end(),
                          findData.address) == hideTargets.end()) {
                hideTargets.push_back(findData.address);
            }
        }
    } while (Wh_FindNextSymbol(search, &findData));

    if (showTargets.size() != 1 || hideTargets.size() != 1) {
        ModLog(L"Classic Clock: symbol resolver ambiguous/missing ShowFlyout=%u raw/%u distinct HideFlyout=%u raw/%u distinct; failing closed",
               rawShowMatches,
               static_cast<unsigned>(showTargets.size()),
               rawHideMatches,
               static_cast<unsigned>(hideTargets.size()));
        return false;
    }

    DWORD timestamp = 0;
    DWORD sizeOfImage = 0;
    if (!GetLoadedModulePeIdentity(module, &timestamp, &sizeOfImage)) {
        return false;
    }

    const auto base = reinterpret_cast<uintptr_t>(module);
    const auto show = reinterpret_cast<uintptr_t>(showTargets.front());
    const auto hide = reinterpret_cast<uintptr_t>(hideTargets.front());
    if (show < base || hide < base ||
        show >= base + sizeOfImage || hide >= base + sizeOfImage) {
        ModLog(L"Classic Clock: resolved symbols are outside timedate.cpl; failing closed");
        return false;
    }

    *showFlyoutRva = show - base;
    *hideFlyoutRva = hide - base;
    ModLog(L"Classic Clock: symbol fallback resolved ShowFlyout RVA=0x%llX HideFlyout RVA=0x%llX",
           static_cast<unsigned long long>(*showFlyoutRva),
           static_cast<unsigned long long>(*hideFlyoutRva));
    return true;
}

bool ResolveClassicClockAbiBlocking() {
    wchar_t systemDir[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDir, ARRAYSIZE(systemDir));
    if (!length || length >= ARRAYSIZE(systemDir)) {
        ModLog(L"Classic Clock: GetSystemDirectoryW failed during ABI prefetch error=%lu",
               GetLastError());
        return false;
    }

    std::wstring path = systemDir;
    if (!path.empty() && path.back() != L'\\') {
        path.push_back(L'\\');
    }
    path += L"timedate.cpl";

    HMODULE module = GetModuleHandleW(path.c_str());
    bool loadedByUs = false;
    if (!module) {
        module = LoadLibraryExW(path.c_str(), nullptr,
                                LOAD_LIBRARY_SEARCH_SYSTEM32);
        loadedByUs = module != nullptr;
    }
    if (!module) {
        ModLog(L"Classic Clock: ABI prefetch couldn't load timedate.cpl error=%lu",
               GetLastError());
        return false;
    }

    DWORD timestamp = 0;
    DWORD sizeOfImage = 0;
    uintptr_t showRva = 0;
    uintptr_t hideRva = 0;
    bool ok = GetLoadedModulePeIdentity(module, &timestamp, &sizeOfImage);
    if (!ok) {
        ModLog(L"Classic Clock: ABI prefetch couldn't read timedate.cpl identity");
    } else if (LookupKnownClassicClockAbi(
                   timestamp, sizeOfImage, &showRva, &hideRva)) {
        ok = showRva < sizeOfImage && hideRva < sizeOfImage;
        if (ok) {
            ModLog(L"Classic Clock: corpus fast path matched timedate.cpl timestamp=0x%08X size=0x%X ShowFlyout=0x%llX HideFlyout=0x%llX",
                   timestamp,
                   sizeOfImage,
                   static_cast<unsigned long long>(showRva),
                   static_cast<unsigned long long>(hideRva));
        }
    } else {
        ModLog(L"Classic Clock: timedate.cpl timestamp=0x%08X size=0x%X not in tested corpus; trying symbols",
               timestamp, sizeOfImage);
        ok = ResolveClassicClockAbiBySymbols(module, &showRva, &hideRva);
    }

    if (loadedByUs) {
        FreeLibrary(module);
    }

    if (!ok || !showRva || !hideRva) {
        return false;
    }

    g_classicClockShowFlyoutRva.store(showRva, std::memory_order_release);
    g_classicClockHideFlyoutRva.store(hideRva, std::memory_order_release);
    g_classicClockResolvedTimestamp.store(timestamp, std::memory_order_release);
    g_classicClockResolvedSizeOfImage.store(sizeOfImage, std::memory_order_release);
    return true;
}

DWORD WINAPI ClassicClockResolverThreadProc(void*) {
    ModLog(L"Classic Clock: background ABI resolver started");
    if (ResolveClassicClockAbiBlocking()) {
        g_classicClockAbiResolveState.store(
            ClassicClockAbiResolveState::Ready,
            std::memory_order_release);
        ModLog(L"Classic Clock: ABI prefetch ready");
    } else {
        g_classicClockAbiResolveState.store(
            ClassicClockAbiResolveState::Failed,
            std::memory_order_release);
        ModLog(L"Classic Clock: ABI prefetch failed; flyout will fail closed");
    }
    return 0;
}

void StartClassicClockAbiPrefetch() {
    if (!g_showClockButton.load(std::memory_order_relaxed) ||
        g_clockClickAction.load(std::memory_order_relaxed) !=
            ClockClickAction::ClassicFlyout) {
        return;
    }

    ClassicClockAbiResolveState expected =
        ClassicClockAbiResolveState::NotStarted;
    if (!g_classicClockAbiResolveState.compare_exchange_strong(
            expected,
            ClassicClockAbiResolveState::Resolving,
            std::memory_order_acq_rel)) {
        return;
    }

    g_classicClockResolverThread = CreateThread(
        nullptr, 0, ClassicClockResolverThreadProc, nullptr, 0, nullptr);
    if (!g_classicClockResolverThread) {
        const DWORD error = GetLastError();
        g_classicClockAbiResolveState.store(
            ClassicClockAbiResolveState::Failed,
            std::memory_order_release);
        ModLog(L"Classic Clock: couldn't start ABI resolver worker error=%lu",
               error);
        return;
    }

    ModLog(L"Classic Clock: ABI resolver queued; XAML UI thread will never wait for symbols");
}

bool GetCurrentLogonUiHostHwnd(HWND* hostOut) {
    if (!hostOut) {
        return false;
    }
    *hostOut = nullptr;

    HWND host = GetForegroundWindow();
    DWORD processId = 0;
    if (host) {
        GetWindowThreadProcessId(host, &processId);
    }
    if (!host || processId != GetCurrentProcessId()) {
        host = GetActiveWindow();
        processId = 0;
        if (host) {
            GetWindowThreadProcessId(host, &processId);
        }
    }
    if (!host || processId != GetCurrentProcessId()) {
        return false;
    }

    *hostOut = host;
    return true;
}

IUnknown* ResolveClassicClockObjectLocked() {
    if (g_trayClockObject) {
        return g_trayClockObject;
    }
    if (g_classicClockResolveAttempted) {
        return nullptr;
    }
    g_classicClockResolveAttempted = true;

    wchar_t systemDir[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDir, ARRAYSIZE(systemDir));
    if (!length || length >= ARRAYSIZE(systemDir)) {
        ModLog(L"Classic Clock: GetSystemDirectoryW failed error=%lu",
               GetLastError());
        return nullptr;
    }

    std::wstring path = systemDir;
    if (!path.empty() && path.back() != L'\\') {
        path.push_back(L'\\');
    }
    path += L"timedate.cpl";

    HMODULE module = GetModuleHandleW(path.c_str());
    bool loadedByUs = false;
    if (!module) {
        module = LoadLibraryExW(
            path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        loadedByUs = module != nullptr;
    }
    if (!module) {
        ModLog(L"Classic Clock: couldn't load %s error=%lu",
               path.c_str(), GetLastError());
        return nullptr;
    }

    DWORD timestamp = 0;
    DWORD sizeOfImage = 0;
    if (!GetLoadedModulePeIdentity(module, &timestamp, &sizeOfImage)) {
        ModLog(L"Classic Clock: couldn't read timedate.cpl PE identity; failing closed");
        if (loadedByUs) { FreeLibrary(module); }
        return nullptr;
    }

    ModLog(L"Classic Clock: timedate.cpl identity timestamp=0x%08X size=0x%X",
           timestamp, sizeOfImage);

    const auto abiState =
        g_classicClockAbiResolveState.load(std::memory_order_acquire);
    if (abiState == ClassicClockAbiResolveState::NotStarted) {
        StartClassicClockAbiPrefetch();
    }
    if (g_classicClockAbiResolveState.load(std::memory_order_acquire) !=
        ClassicClockAbiResolveState::Ready) {
        ModLog(L"Classic Clock: ABI resolver is not ready; refusing to block LogonUI UI thread and failing this click closed");
        if (loadedByUs) { FreeLibrary(module); }
        return nullptr;
    }

    const uintptr_t verifiedShowFlyoutRva =
        g_classicClockShowFlyoutRva.load(std::memory_order_acquire);
    const uintptr_t verifiedHideFlyoutRva =
        g_classicClockHideFlyoutRva.load(std::memory_order_acquire);
    const DWORD resolvedTimestamp =
        g_classicClockResolvedTimestamp.load(std::memory_order_acquire);
    const DWORD resolvedSizeOfImage =
        g_classicClockResolvedSizeOfImage.load(std::memory_order_acquire);

    if (!verifiedShowFlyoutRva || !verifiedHideFlyoutRva ||
        resolvedTimestamp != timestamp ||
        resolvedSizeOfImage != sizeOfImage ||
        verifiedShowFlyoutRva >= sizeOfImage ||
        verifiedHideFlyoutRva >= sizeOfImage) {
        ModLog(L"Classic Clock: prefetched ABI doesn't match the currently loaded timedate.cpl; failing closed");
        if (loadedByUs) { FreeLibrary(module); }
        return nullptr;
    }

    using DllGetClassObject_t =
        HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, LPVOID*);
    auto dllGetClassObject = reinterpret_cast<DllGetClassObject_t>(
        GetProcAddress(module, "DllGetClassObject"));
    if (!dllGetClassObject) {
        ModLog(L"Classic Clock: timedate.cpl has no DllGetClassObject export");
        if (loadedByUs) { FreeLibrary(module); }
        return nullptr;
    }

    IClassFactory* factory = nullptr;
    HRESULT hr = dllGetClassObject(
        kClsidTrayClock,
        kIidIClassFactory,
        reinterpret_cast<void**>(&factory));
    if (FAILED(hr) || !factory) {
        ModLog(L"Classic Clock: DllGetClassObject(CLSID_TrayClock) failed 0x%08X",
               static_cast<unsigned>(hr));
        if (loadedByUs) { FreeLibrary(module); }
        return nullptr;
    }

    IUnknown* object = nullptr;
    hr = factory->CreateInstance(
        nullptr,
        kIidIUnknown,
        reinterpret_cast<void**>(&object));
    factory->Release();
    if (FAILED(hr) || !object) {
        ModLog(L"Classic Clock: CTrayClock CreateInstance failed 0x%08X",
               static_cast<unsigned>(hr));
        if (loadedByUs) { FreeLibrary(module); }
        return nullptr;
    }

    void** vtable = *reinterpret_cast<void***>(object);
    auto base = reinterpret_cast<BYTE*>(module);
    if (!vtable ||
        vtable[3] != base + verifiedShowFlyoutRva ||
        vtable[4] != base + verifiedHideFlyoutRva) {
        ModLog(L"Classic Clock: CTrayClock vtable validation failed show=%p hide=%p expected=%p/%p",
               vtable ? vtable[3] : nullptr,
               vtable ? vtable[4] : nullptr,
               base + verifiedShowFlyoutRva,
               base + verifiedHideFlyoutRva);
        object->Release();
        if (loadedByUs) { FreeLibrary(module); }
        return nullptr;
    }

    g_timeDateModule = module;
    g_trayClockObject = object;
    ModLog(L"Classic Clock: verified CTrayClock backend ready object=%p ShowFlyout=RVA 0x%llX HideFlyout=RVA 0x%llX",
           object,
           static_cast<unsigned long long>(verifiedShowFlyoutRva),
           static_cast<unsigned long long>(verifiedHideFlyoutRva));
    return object;
}

bool ShowClassicClockFlyout(Controls::Button const& clockButton) {
    if (!clockButton) {
        return false;
    }

    RECT anchor{};
    if (!TryGetXamlElementScreenRect(
            clockButton, &anchor, L"Classic Clock flyout")) {
        return false;
    }

    HWND host = nullptr;
    if (!GetCurrentLogonUiHostHwnd(&host)) {
        ModLog(L"Classic Clock: couldn't identify LogonUI host HWND");
        return false;
    }

    if (g_clockFlyoutPlacement.load(std::memory_order_relaxed) ==
            ClockPlacementMode::AnchorToButton) {
        UpdateClassicClockFlyoutAnchorRect(anchor);
    } else {
        StoreClockAnchorRect(anchor);
    }

    AcquireSRWLockExclusive(&g_classicClockBackendLock);
    IUnknown* object = ResolveClassicClockObjectLocked();
    if (!object) {
        ReleaseSRWLockExclusive(&g_classicClockBackendLock);
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(object);
    using ShowFlyout_t = HRESULT(STDMETHODCALLTYPE*)(
        IUnknown*, HWND, const RECT*);
    auto showFlyout = reinterpret_cast<ShowFlyout_t>(vtable[3]);
    const HRESULT hr = showFlyout(object, host, &anchor);
    ReleaseSRWLockExclusive(&g_classicClockBackendLock);

    ModLog(L"Classic Clock: CTrayClock::ShowFlyout host=%p rect=(%ld,%ld)-(%ld,%ld) returned 0x%08X",
           host,
           anchor.left, anchor.top, anchor.right, anchor.bottom,
           static_cast<unsigned>(hr));

    if (SUCCEEDED(hr) &&
        g_clockFlyoutPlacement.load(std::memory_order_relaxed) ==
            ClockPlacementMode::AnchorToButton) {
        // ShowFlyout creates/activates ClockFlyoutWindow on timedate.cpl's
        // helper thread. The SHOW hook is the primary path; this immediate
        // enumeration also catches builds where the window becomes visible
        // before WinEvent delivery is pumped.
        RepositionExistingClassicClockWindows();
    }

    return SUCCEEDED(hr);
}

void ShutdownClassicClockBackend() {
    AcquireSRWLockExclusive(&g_classicClockBackendLock);

    if (g_trayClockObject) {
        void** vtable = *reinterpret_cast<void***>(g_trayClockObject);
        if (vtable && g_timeDateModule) {
            const uintptr_t hideFlyoutRva =
                g_classicClockHideFlyoutRva.load(std::memory_order_acquire);
            auto expected = hideFlyoutRva
                ? reinterpret_cast<BYTE*>(g_timeDateModule) + hideFlyoutRva
                : nullptr;
            if (expected && vtable[4] == expected) {
                using HideFlyout_t = HRESULT(STDMETHODCALLTYPE*)(IUnknown*);
                auto hideFlyout =
                    reinterpret_cast<HideFlyout_t>(vtable[4]);
                const HRESULT hr = hideFlyout(g_trayClockObject);
                ModLog(L"Classic Clock: CTrayClock::HideFlyout during cleanup returned 0x%08X",
                       static_cast<unsigned>(hr));
            }
        }
        g_trayClockObject->Release();
        g_trayClockObject = nullptr;
    }

    // Keep timedate.cpl mapped until LogonUI exits. Its flyout owns a helper
    // thread and Windows-side callbacks; avoiding FreeLibrary here prevents a
    // module-lifetime race during hot-disable while still releasing our COM
    // object and closing the visible flyout.
    g_timeDateModule = nullptr;
    g_classicClockResolveAttempted = false;

    ReleaseSRWLockExclusive(&g_classicClockBackendLock);
}

thread_local bool g_dateTimeMainDialogPositioned = false;

LRESULT CALLBACK DateTimeCbtHookProc(
        int code,
        WPARAM wParam,
        LPARAM lParam) {
    if (code == HCBT_ACTIVATE &&
        !g_dateTimeMainDialogPositioned &&
        g_clockDateTimePlacement.load(std::memory_order_relaxed) ==
            ClockPlacementMode::AnchorToButton) {
        HWND hwnd = reinterpret_cast<HWND>(wParam);

        wchar_t className[64]{};
        if (hwnd &&
            GetClassNameW(hwnd, className, ARRAYSIZE(className)) &&
            _wcsicmp(className, L"#32770") == 0 &&
            !(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD)) {
            RECT anchorRect{};
            if (GetCurrentClockAnchorRect(&anchorRect)) {
                wchar_t title[256]{};
                GetWindowTextW(hwnd, title, ARRAYSIZE(title));
                ModLog(L"Date and Time dialog activated: hwnd=%p title=%s",
                       hwnd,
                       title[0] ? title : L"<none>");

                if (RepositionWindowToAnchor(
                        hwnd,
                        anchorRect,
                        g_clockDateTimeAnchorSide.load(
                            std::memory_order_relaxed),
                        g_clockDateTimeOffsetX.load(
                            std::memory_order_relaxed),
                        g_clockDateTimeOffsetY.load(
                            std::memory_order_relaxed),
                        L"Date and Time")) {
                    g_dateTimeMainDialogPositioned = true;
                }
            } else {
                ModLog(L"Date and Time anchor requested but Clock button geometry isn't cached");
            }
        }
    }

    return CallNextHookEx(nullptr, code, wParam, lParam);
}

DWORD WINAPI DateTimeDialogThreadProc(void*) {
    g_dateTimeDialogActive.store(true, std::memory_order_release);

    HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitializeCom = SUCCEEDED(comHr);

    wchar_t systemDir[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDir, ARRAYSIZE(systemDir));
    if (!length || length >= ARRAYSIZE(systemDir)) {
        ModLog(L"Date and Time: GetSystemDirectoryW failed error=%lu",
               GetLastError());
        if (uninitializeCom) {
            CoUninitialize();
        }
        g_dateTimeDialogActive.store(false, std::memory_order_release);
        return 0;
    }

    std::wstring path = systemDir;
    if (!path.empty() && path.back() != L'\\') {
        path.push_back(L'\\');
    }
    path += L"timedate.cpl";

    HMODULE module = LoadLibraryExW(
        path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        ModLog(L"Date and Time: couldn't load %s error=%lu",
               path.c_str(), GetLastError());
        if (uninitializeCom) {
            CoUninitialize();
        }
        g_dateTimeDialogActive.store(false, std::memory_order_release);
        return 0;
    }

    using CPlApplet_t =
        LONG(CALLBACK*)(HWND, UINT, LPARAM, LPARAM);
    auto cplApplet = reinterpret_cast<CPlApplet_t>(
        GetProcAddress(module, "CPlApplet"));
    if (!cplApplet) {
        ModLog(L"Date and Time: timedate.cpl has no CPlApplet export");
        FreeLibrary(module);
        if (uninitializeCom) {
            CoUninitialize();
        }
        g_dateTimeDialogActive.store(false, std::memory_order_release);
        return 0;
    }

    constexpr UINT kCplInit = 1;
    constexpr UINT kCplGetCount = 2;
    constexpr UINT kCplStop = 6;
    constexpr UINT kCplExit = 7;
    constexpr UINT kCplStartWParms = 10;

    LONG initResult = cplApplet(nullptr, kCplInit, 0, 0);
    LONG count = initResult ? cplApplet(nullptr, kCplGetCount, 0, 0) : 0;
    ModLog(L"Date and Time: CPlApplet init=%ld count=%ld",
           initResult, count);

    g_dateTimeMainDialogPositioned = false;
    HHOOK placementHook = nullptr;
    if (g_clockDateTimePlacement.load(std::memory_order_relaxed) ==
            ClockPlacementMode::AnchorToButton) {
        placementHook = SetWindowsHookExW(
            WH_CBT,
            DateTimeCbtHookProc,
            nullptr,
            GetCurrentThreadId());
        if (!placementHook) {
            ModLog(L"Date and Time: couldn't install CBT placement hook error=%lu",
                   GetLastError());
        }
    }

    if (initResult && count > 0) {
        wchar_t startParameters[] = L"0";
        LONG result = cplApplet(
            nullptr,
            kCplStartWParms,
            0,
            reinterpret_cast<LPARAM>(startParameters));
        ModLog(L"Date and Time: CPL_STARTWPARMSW(\"0\") returned %ld",
               result);
        cplApplet(nullptr, kCplStop, 0, 0);
    }

    if (placementHook) {
        UnhookWindowsHookEx(placementHook);
        placementHook = nullptr;
    }

    if (initResult) {
        cplApplet(nullptr, kCplExit, 0, 0);
    }

    FreeLibrary(module);

    if (uninitializeCom) {
        CoUninitialize();
    }

    g_dateTimeDialogActive.store(false, std::memory_order_release);
    ModLog(L"Date and Time host thread exited");
    return 0;
}

bool LaunchClassicDateTimeDialog(
        FrameworkElement const& anchorElement) {
    if (g_clockClickAction.load(std::memory_order_relaxed) !=
        ClockClickAction::DateTimeDialog) {
        return false;
    }

    if (g_clockDateTimePlacement.load(std::memory_order_relaxed) ==
            ClockPlacementMode::AnchorToButton) {
        RECT anchor{};
        if (TryGetXamlElementScreenRect(
                anchorElement, &anchor, L"Date and Time")) {
            StoreClockAnchorRect(anchor);
        } else {
            ModLog(L"Date and Time anchor requested but Clock button screen geometry wasn't available; keeping Windows placement");
        }
    }

    bool expected = false;
    if (!g_dateTimeDialogActive.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        ModLog(L"Date and Time is already active; duplicate launch ignored");
        return true;
    }

    HANDLE thread = CreateThread(
        nullptr,
        0,
        DateTimeDialogThreadProc,
        nullptr,
        0,
        nullptr);
    if (!thread) {
        g_dateTimeDialogActive.store(false, std::memory_order_release);
        ModLog(L"Date and Time: CreateThread failed error=%lu",
               GetLastError());
        return false;
    }

    if (g_dateTimeDialogThread) {
        CloseHandle(g_dateTimeDialogThread);
    }
    g_dateTimeDialogThread = thread;
    return true;
}

std::wstring FormatClockText(bool showSeconds, bool showDate) {
    SYSTEMTIME localTime{};
    GetLocalTime(&localTime);

    wchar_t timeBuffer[128]{};
    const DWORD timeFlags = showSeconds ? 0 : TIME_NOSECONDS;
    const int timeCount = GetTimeFormatEx(
        LOCALE_NAME_USER_DEFAULT,
        timeFlags,
        &localTime,
        nullptr,
        timeBuffer,
        ARRAYSIZE(timeBuffer));

    std::wstring result;
    if (timeCount > 0) {
        result = timeBuffer;
    } else {
        wchar_t fallbackTime[32]{};
        if (showSeconds) {
            swprintf_s(
                fallbackTime, L"%02u:%02u:%02u",
                localTime.wHour, localTime.wMinute, localTime.wSecond);
        } else {
            swprintf_s(
                fallbackTime, L"%02u:%02u",
                localTime.wHour, localTime.wMinute);
        }
        result = fallbackTime;
    }

    if (!showDate) {
        return result;
    }

    wchar_t dateBuffer[128]{};
    const int dateCount = GetDateFormatEx(
        LOCALE_NAME_USER_DEFAULT,
        DATE_SHORTDATE,
        &localTime,
        nullptr,
        dateBuffer,
        ARRAYSIZE(dateBuffer),
        nullptr);

    result.push_back(L'\n');
    if (dateCount > 0) {
        result += dateBuffer;
    } else {
        wchar_t fallbackDate[32]{};
        swprintf_s(
            fallbackDate, L"%04u-%02u-%02u",
            localTime.wYear, localTime.wMonth, localTime.wDay);
        result += fallbackDate;
    }

    return result;
}

HRESULT WINAPI LaunchPowerCPL_Hook() {
    if (g_unloading.load(std::memory_order_acquire)) {
        return g_launchPowerCplOriginal ? g_launchPowerCplOriginal() : E_FAIL;
    }

    const auto action =
        g_morePowerOptionsAction.load(std::memory_order_relaxed);

    if (action == MorePowerOptionsAction::WindowsDefault) {
        if (g_launchPowerCplOriginal) {
            return g_launchPowerCplOriginal();
        }
        return E_FAIL;
    }

    return LaunchAdvancedPowerOptionsDirect() ? S_OK : E_FAIL;
}

bool EnsureLaunchPowerCplHookInstalled() {
    if (g_morePowerOptionsAction.load(std::memory_order_relaxed) ==
        MorePowerOptionsAction::WindowsDefault) {
        return true;
    }

    if (g_launchPowerCplHookInstalled.load(std::memory_order_acquire)) {
        return true;
    }

    void* target =
        g_launchPowerCplTarget.load(std::memory_order_acquire);
    if (!target) {
        ModLog(L"LaunchPowerCPL hook unavailable: target not resolved yet");
        return false;
    }

    bool expected = false;
    if (!g_launchPowerCplHookInstalled.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return true;
    }

    if (!Wh_SetFunctionHook(
            target,
            reinterpret_cast<void*>(LaunchPowerCPL_Hook),
            reinterpret_cast<void**>(&g_launchPowerCplOriginal))) {
        g_launchPowerCplHookInstalled.store(false,
                                            std::memory_order_release);
        ModLog(L"Wh_SetFunctionHook failed for stobject!LaunchPowerCPL");
        return false;
    }

    Wh_ApplyHookOperations();
    ModLog(L"Hooked stobject!LaunchPowerCPL -> direct powercfg.cpl host");
    return true;
}

std::wstring FormatPercentageText(
        int percent,
        bool volume,
        bool muted = false);

void SetPercentageLabelText(
        Controls::TextBlock const& label,
        const std::wstring& value,
        bool featureEnabled);

constexpr UINT kBatteryUiRefreshMessage = WM_APP + 0x431;
constexpr UINT kBatteryUiPercentMessage = WM_APP + 0x432;
constexpr wchar_t kBatteryUiBridgeClass[] =
    L"Windhawk.LogonUI.Tweaks.BatteryUiBridge.v2";
constexpr wchar_t kBatteryNotifyClass[] =
    L"Windhawk.LogonUI.Tweaks.BatteryNotify.v2";

const GUID kGuidAcDcPowerSource =
    {0x5D3E9A59, 0xE9D5, 0x4B00,
     {0xA6, 0xBD, 0xFF, 0x34, 0xFF, 0x51, 0x65, 0x48}};
const GUID kGuidBatteryPercentageRemaining =
    {0xA7AD8041, 0xB45A, 0x4CAE,
     {0x87, 0xA3, 0xEE, 0xCB, 0xB4, 0x68, 0xA9, 0xE1}};

void QueueBatteryPercentUiUpdate(BYTE percent) {
    HWND bridge = g_batteryUiBridgeWindow.load(std::memory_order_acquire);
    if (bridge) {
        PostMessageW(
            bridge,
            kBatteryUiPercentMessage,
            static_cast<WPARAM>(percent),
            0);
    }
}

void ApplyBatteryPercentageToCurrentLabel(BYTE percent) {
    auto label = g_batteryPercentText.get();
    if (!label) {
        return;
    }

    try {
        const bool enabled =
            g_showBatteryPercentage.load(std::memory_order_relaxed);
        const std::wstring value =
            (percent <= 100)
                ? FormatPercentageText(
                      static_cast<int>(percent), false, false)
                : std::wstring{};
        SetPercentageLabelText(label, value, enabled);
    } catch (...) {
    }
}

void QueueBatteryFlyoutUiRefresh() {
    if (g_batteryFlyoutRefresh.load(std::memory_order_relaxed) !=
        BatteryFlyoutRefreshMode::EventDriven) {
        return;
    }

    HWND bridge = g_batteryUiBridgeWindow.load(std::memory_order_acquire);
    if (bridge) {
        PostMessageW(bridge, kBatteryUiRefreshMessage, 0, 0);
    }
}

bool EnsurePrivateProcessWindowClass(
        const wchar_t* className, WNDPROC wndProc) {
    if (!className || !*className || !wndProc) {
        return false;
    }

    HINSTANCE owner = GetModuleHandleW(nullptr);
    WNDCLASSEXW existing{};
    existing.cbSize = sizeof(existing);
    if (GetClassInfoExW(owner, className, &existing)) {
        if (existing.lpfnWndProc == wndProc) {
            return true;
        }

        // Never accept ERROR_CLASS_ALREADY_EXISTS blindly. A previous hot-
        // loaded mod DLL can be gone while the process-local class still owns
        // its old WNDPROC. Reusing that class causes USER32/CFG to jump into
        // the unloaded DLL (or into unrelated code at the same remapped RVA).
        ModLog(L"Window class %s exists with a stale/different WNDPROC; attempting safe unregister before reuse",
               className);
        if (!UnregisterClassW(className, owner)) {
            ModLog(L"Window class %s couldn't be safely replaced (UnregisterClass error=%lu); refusing to create a window with a stale callback",
                   className, GetLastError());
            return false;
        }
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = owner;
    wc.lpszClassName = className;
    if (!RegisterClassExW(&wc)) {
        ModLog(L"RegisterClassEx failed for %s: %lu",
               className, GetLastError());
        return false;
    }
    return true;
}

void UnregisterPrivateProcessWindowClass(
        const wchar_t* className, WNDPROC wndProc) {
    if (!className || !*className || !wndProc) {
        return;
    }

    HINSTANCE owner = GetModuleHandleW(nullptr);
    WNDCLASSEXW existing{};
    existing.cbSize = sizeof(existing);
    if (!GetClassInfoExW(owner, className, &existing)) {
        return;
    }

    // Don't unregister someone else's class if a future Windows/mod change
    // happens to reuse the name.
    if (existing.lpfnWndProc != wndProc) {
        ModLog(L"Refusing to unregister window class %s because its WNDPROC isn't ours",
               className);
        return;
    }

    if (!UnregisterClassW(className, owner)) {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_DOES_NOT_EXIST) {
            ModLog(L"UnregisterClass failed for %s: %lu", className, error);
        }
    } else {
        ModLog(L"Unregistered private window class %s", className);
    }
}

LRESULT CALLBACK BatteryUiBridgeWndProc(
        HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == kBatteryUiPercentMessage) {
        ApplyBatteryPercentageToCurrentLabel(
            static_cast<BYTE>(wParam & 0xFF));
        return 0;
    }

    if (message == kBatteryUiRefreshMessage) {
        if (g_batteryFlyoutRefresh.load(std::memory_order_relaxed) !=
            BatteryFlyoutRefreshMode::EventDriven) {
            return 0;
        }

        HWND flyout = FindBatMeterFlyoutWindow();
        if (!flyout || !IsWindowVisible(flyout)) {
            return 0;
        }

        void* showTarget =
            g_showBatteryUiTarget.load(std::memory_order_acquire);
        if (!showTarget) {
            ModLog(L"Battery live refresh skipped: stobject!ShowBatteryUI isn't resolved");
            return 0;
        }

        // Important: don't call stobject!UpdateFlyoutUI directly here.
        // It is a low-level population routine invoked by Flyout_UpdateUI
        // after the native hide/cleanup path. Re-entering it against an
        // already-populated flyout appends duplicate controls and grows the
        // window. ShowBatteryUI performs the genuine hide + rebuild sequence.
        using ShowBatteryUI_t = HRESULT(WINAPI*)();
        auto showBatteryUI =
            reinterpret_cast<ShowBatteryUI_t>(showTarget);
        const HRESULT hr = showBatteryUI();

        RECT anchorRect{};
        if (g_batteryFlyoutPlacement.load(std::memory_order_relaxed) ==
                BatteryFlyoutPlacementMode::AnchorToButton &&
            GetCurrentBatteryAnchorRect(&anchorRect)) {
            RepositionClassicBatteryFlyout(anchorRect);
        }

        ModLog(L"Battery flyout event-refresh rebuilt native UI; ShowBatteryUI returned 0x%08X",
               static_cast<unsigned>(hr));
        return 0;
    }

    if (message == WM_NCDESTROY) {
        if (g_batteryUiBridgeWindow.load() == hwnd) {
            g_batteryUiBridgeWindow.store(nullptr);
        }
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND EnsureBatteryUiBridgeWindow() {
    if (HWND existing =
            g_batteryUiBridgeWindow.load(std::memory_order_acquire)) {
        return existing;
    }

    if (!EnsurePrivateProcessWindowClass(
            kBatteryUiBridgeClass, BatteryUiBridgeWndProc)) {
        ModLog(L"Battery UI bridge class unavailable; UI bridge disabled for this mod instance");
        return nullptr;
    }

    HWND hwnd = CreateWindowExW(
        0,
        kBatteryUiBridgeClass,
        L"",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (!hwnd) {
        ModLog(L"Battery UI bridge CreateWindowEx failed: %lu",
               GetLastError());
        return nullptr;
    }

    g_batteryUiBridgeWindow.store(hwnd, std::memory_order_release);
    ModLog(L"Battery UI bridge created on LogonUI UI thread=%lu",
           GetCurrentThreadId());
    return hwnd;
}

constexpr UINT_PTR kBatteryRefreshTimerId = 1;

void HandleBatteryEventAfterDebounce() {
    if (g_showBatteryPercentage.load(std::memory_order_relaxed)) {
        SYSTEM_POWER_STATUS status{};
        if (GetSystemPowerStatus(&status)) {
            QueueBatteryPercentUiUpdate(status.BatteryLifePercent);
        }
    }

    if (g_batteryFlyoutRefresh.load(std::memory_order_relaxed) !=
        BatteryFlyoutRefreshMode::EventDriven) {
        return;
    }

    HWND flyout = FindBatMeterFlyoutWindow();
    if (!flyout || !IsWindowVisible(flyout)) {
        return;
    }

    // QueryBatteryData only copies BatMeter's current cache. Force a fresh
    // BatMeter read once after the Windows event, then rebuild the genuine
    // classic flyout on the LogonUI UI thread.
    if (!PrimeClassicBatteryStateBlocking(1000)) {
        ModLog(L"Battery live refresh skipped: fresh UpdateBatteryData failed");
        return;
    }

    QueueBatteryFlyoutUiRefresh();
}

LRESULT CALLBACK BatteryNotifyWndProc(
        HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_POWERBROADCAST) {
        if (wParam == PBT_APMPOWERSTATUSCHANGE ||
            wParam == PBT_POWERSETTINGCHANGE) {
            const bool needsBatteryEvent =
                g_showBatteryPercentage.load(std::memory_order_relaxed) ||
                g_batteryFlyoutRefresh.load(std::memory_order_relaxed) ==
                    BatteryFlyoutRefreshMode::EventDriven;
            if (needsBatteryEvent) {
                const DWORD delay =
                    g_batteryEventDebounceDelayMs.load(
                        std::memory_order_relaxed);

                if (delay == 0) {
                    HandleBatteryEventAfterDebounce();
                } else {
                    // Reusing the same timer ID coalesces bursts of AC/DC +
                    // percentage notifications into one fresh-data rebuild.
                    if (!SetTimer(
                            hwnd,
                            kBatteryRefreshTimerId,
                            delay,
                            nullptr)) {
                        ModLog(L"Battery event debounce SetTimer failed error=%lu; handling immediately",
                               GetLastError());
                        HandleBatteryEventAfterDebounce();
                    }
                }
            }
        }
        return TRUE;
    }

    if (message == WM_TIMER &&
        wParam == kBatteryRefreshTimerId) {
        KillTimer(hwnd, kBatteryRefreshTimerId);
        HandleBatteryEventAfterDebounce();
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

DWORD WINAPI BatteryNotificationThreadProc(void*) {
    if (!EnsurePrivateProcessWindowClass(
            kBatteryNotifyClass, BatteryNotifyWndProc)) {
        ModLog(L"Battery notification class unavailable; watcher disabled for this mod instance");
        return 0;
    }

    // Hidden top-level window: unlike HWND_MESSAGE, it can also receive the
    // general PBT_APMPOWERSTATUSCHANGE broadcast in addition to registered
    // power-setting notifications.
    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kBatteryNotifyClass,
        L"",
        WS_POPUP,
        0, 0, 0, 0,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (!hwnd) {
        ModLog(L"Battery notification CreateWindowEx failed: %lu",
               GetLastError());
        UnregisterPrivateProcessWindowClass(
            kBatteryNotifyClass, BatteryNotifyWndProc);
        return 0;
    }

    HPOWERNOTIFY acNotify =
        RegisterPowerSettingNotification(
            hwnd, &kGuidAcDcPowerSource, DEVICE_NOTIFY_WINDOW_HANDLE);
    HPOWERNOTIFY percentNotify =
        RegisterPowerSettingNotification(
            hwnd, &kGuidBatteryPercentageRemaining,
            DEVICE_NOTIFY_WINDOW_HANDLE);

    ModLog(L"Battery notification watcher started (event-driven)");

    while (true) {
        DWORD wait = MsgWaitForMultipleObjects(
            1, &g_stopEvent, FALSE, INFINITE, QS_ALLINPUT);

        if (wait == WAIT_OBJECT_0) {
            break;
        }

        if (wait == WAIT_OBJECT_0 + 1) {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            break;
        }
    }

    if (acNotify) {
        UnregisterPowerSettingNotification(acNotify);
    }
    if (percentNotify) {
        UnregisterPowerSettingNotification(percentNotify);
    }
    DestroyWindow(hwnd);
    UnregisterPrivateProcessWindowClass(
        kBatteryNotifyClass, BatteryNotifyWndProc);

    ModLog(L"Battery notification watcher stopped");
    return 0;
}

void EnsureBatteryNotificationWatcherStarted() {
    const bool needed =
        g_showBatteryButton.load(std::memory_order_relaxed) &&
        (g_showBatteryPercentage.load(std::memory_order_relaxed) ||
         g_batteryFlyoutRefresh.load(std::memory_order_relaxed) ==
             BatteryFlyoutRefreshMode::EventDriven);

    if (!needed || g_batteryNotificationThread) {
        return;
    }

    HANDLE thread = CreateThread(
        nullptr, 0, BatteryNotificationThreadProc, nullptr, 0, nullptr);
    if (!thread) {
        ModLog(L"Battery notification watcher CreateThread failed: %lu",
               GetLastError());
        return;
    }

    g_batteryNotificationThread = thread;
}

bool ShowClassicBatteryFlyout(FrameworkElement const& anchorElement) {
    if (g_batteryClickAction.load(std::memory_order_relaxed) ==
        BatteryClickAction::None) {
        ModLog(L"Battery click ignored by setting");
        return false;
    }

    RECT anchorRect{};
    const bool shouldAnchor =
        g_batteryFlyoutPlacement.load(std::memory_order_relaxed) ==
        BatteryFlyoutPlacementMode::AnchorToButton;
    const bool haveAnchorRect =
        shouldAnchor &&
        TryGetXamlElementScreenRect(anchorElement, &anchorRect, L"Battery flyout");

    if (haveAnchorRect) {
        AcquireSRWLockExclusive(&g_batteryAnchorLock);
        g_batteryAnchorRect = anchorRect;
        ReleaseSRWLockExclusive(&g_batteryAnchorLock);
        g_batteryAnchorValid.store(true, std::memory_order_release);
    }

    void* target = g_showBatteryUiTarget.load(std::memory_order_acquire);
    if (!target) {
        const auto state =
            g_batteryFlyoutResolveState.load(std::memory_order_acquire);

        if (state == BatteryFlyoutResolveState::NotStarted) {
            StartBatteryFlyoutSymbolPrefetch();
            ModLog(L"Classic Battery flyout target not ready yet; background resolver started");
        } else if (state == BatteryFlyoutResolveState::Resolving) {
            ModLog(L"Classic Battery flyout target is still resolving in background; click ignored without blocking LogonUI");
        } else if (state == BatteryFlyoutResolveState::Failed) {
            ModLog(L"Classic Battery flyout unavailable: background resolver failed");
        } else {
            ModLog(L"Classic Battery flyout target state is Ready but target pointer is null; refusing call");
        }
        return false;
    }

    // Refresh the stobject-owned BATTERY_STATE from BatMeter's cache before
    // constructing/updating the native flyout. The expensive first acquisition
    // already happened on the background prefetch worker.
    if (!RefreshClassicBatteryStateFromCache()) {
        ModLog(L"Classic Battery flyout state refresh failed; refusing to show stale/zeroed UI");
        return false;
    }

    if (g_batteryFlyoutRefresh.load(std::memory_order_relaxed) ==
        BatteryFlyoutRefreshMode::EventDriven) {
        EnsureBatteryUiBridgeWindow();
        EnsureBatteryNotificationWatcherStarted();
    }

    EnsureLaunchPowerCplHookInstalled();

    using ShowBatteryUI_t = HRESULT(WINAPI*)();
    auto showBatteryUI =
        reinterpret_cast<ShowBatteryUI_t>(target);

    // The target is only used after an exact-build identity match or a
    // verified structural resolution or unique symbol resolution. Avoid MSVC-only SEH syntax so this compiles
    // under Windhawk's Clang toolchain.
    HRESULT hr = showBatteryUI();

    ModLog(L"stobject!ShowBatteryUI returned 0x%08X",
           static_cast<unsigned>(hr));

    if (SUCCEEDED(hr) && shouldAnchor) {
        if (haveAnchorRect) {
            RepositionClassicBatteryFlyout(anchorRect);
        } else {
            ModLog(L"Battery flyout anchor requested but button screen geometry wasn't available; keeping Windows placement");
        }
    }

    return SUCCEEDED(hr);
}

bool IsSndVolProcessId(DWORD processId) {
    if (!processId) {
        return false;
    }

    HANDLE process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        processId);
    if (!process) {
        return false;
    }

    wchar_t imagePath[1024]{};
    DWORD imagePathLength = ARRAYSIZE(imagePath);
    const BOOL ok = QueryFullProcessImageNameW(
        process,
        0,
        imagePath,
        &imagePathLength);
    CloseHandle(process);

    if (!ok || !imagePathLength) {
        return false;
    }

    const wchar_t* fileName = wcsrchr(imagePath, L'\\');
    fileName = fileName ? fileName + 1 : imagePath;
    return _wcsicmp(fileName, L"SndVol.exe") == 0;
}

bool IsUsableSndVolTopLevelWindow(HWND hwnd, DWORD* processIdOut = nullptr) {
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
        return false;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (!IsSndVolProcessId(processId)) {
        return false;
    }

    wchar_t className[64]{};
    if (!GetClassNameW(hwnd, className, ARRAYSIZE(className)) ||
        _wcsicmp(className, L"#32770") != 0 ||
        (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD)) {
        return false;
    }

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect) ||
        rect.right <= rect.left || rect.bottom <= rect.top) {
        return false;
    }

    if (processIdOut) {
        *processIdOut = processId;
    }
    return true;
}

bool GetCurrentSndVolAnchorRect(RECT* rect) {
    if (!rect ||
        !g_sndVolAnchorValid.load(std::memory_order_acquire)) {
        return false;
    }

    AcquireSRWLockShared(&g_sndVolAnchorLock);
    *rect = g_sndVolAnchorRect;
    ReleaseSRWLockShared(&g_sndVolAnchorLock);
    return true;
}


bool RepositionSndVolWindow(HWND hwnd, const wchar_t* reason) {
    DWORD processId = 0;
    if (!IsUsableSndVolTopLevelWindow(hwnd, &processId)) {
        return false;
    }

    wchar_t className[128]{};
    wchar_t title[256]{};
    GetClassNameW(hwnd, className, ARRAYSIZE(className));
    GetWindowTextW(hwnd, title, ARRAYSIZE(title));

    const bool isMixer = title[0] != L'\0';

    ModLog(L"SndVol %s window %s: pid=%lu hwnd=%p class=%s title=%s",
           isMixer ? L"mixer" : L"compact",
           reason ? reason : L"discovered",
           processId,
           hwnd,
           className[0] ? className : L"<none>",
           title[0] ? title : L"<none>");

    const auto placement =
        isMixer
            ? g_sndVolMixerPlacement.load(std::memory_order_relaxed)
            : g_sndVolPlacement.load(std::memory_order_relaxed);
    if (placement != SndVolPlacementMode::AnchorToButton) {
        return true;
    }

    RECT anchorRect{};
    if (!GetCurrentSndVolAnchorRect(&anchorRect)) {
        // Tracking succeeded. Placement can happen later when an anchor exists.
        return true;
    }

    return RepositionWindowToAnchor(
        hwnd,
        anchorRect,
        isMixer
            ? g_sndVolMixerAnchorSide.load(std::memory_order_relaxed)
            : g_sndVolAnchorSide.load(std::memory_order_relaxed),
        isMixer
            ? g_sndVolMixerOffsetX.load(std::memory_order_relaxed)
            : g_sndVolOffsetX.load(std::memory_order_relaxed),
        isMixer
            ? g_sndVolMixerOffsetY.load(std::memory_order_relaxed)
            : g_sndVolOffsetY.load(std::memory_order_relaxed),
        isMixer ? L"SndVol mixer" : L"SndVol compact");
}

BOOL CALLBACK EnumAndRepositionSndVolWindow(HWND hwnd, LPARAM) {
    RepositionSndVolWindow(hwnd, L"enumerated");
    return TRUE;
}

void RepositionExistingSndVolWindows() {
    if (!g_sndVolAnchorValid.load(std::memory_order_acquire)) {
        return;
    }
    EnumWindows(EnumAndRepositionSndVolWindow, 0);
}

void CALLBACK SndVolWinEventProc(
        HWINEVENTHOOK,
        DWORD event,
        HWND hwnd,
        LONG idObject,
        LONG idChild,
        DWORD,
        DWORD) {
    if (g_unloading.load(std::memory_order_acquire) ||
        event != EVENT_OBJECT_SHOW ||
        idObject != OBJID_WINDOW ||
        idChild != CHILDID_SELF ||
        !hwnd) {
        return;
    }

    RepositionSndVolWindow(hwnd, L"shown");
}

DWORD WINAPI SndVolWindowMonitorThreadProc(void*) {
    HWINEVENTHOOK hook = SetWinEventHook(
        EVENT_OBJECT_SHOW,
        EVENT_OBJECT_SHOW,
        nullptr,
        SndVolWinEventProc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    if (!hook) {
        ModLog(L"SndVol monitor: SetWinEventHook failed error=%lu",
               GetLastError());
        return 0;
    }

    ModLog(L"SndVol window monitor started (event-driven SHOW only)");

    RepositionExistingSndVolWindows();

    while (true) {
        DWORD wait = MsgWaitForMultipleObjects(
            1,
            &g_stopEvent,
            FALSE,
            INFINITE,
            QS_ALLINPUT);

        if (wait == WAIT_OBJECT_0) {
            break;
        }

        if (wait == WAIT_OBJECT_0 + 1) {
            MSG msg{};
            while (PeekMessageW(
                    &msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            ModLog(L"SndVol monitor: MsgWaitForMultipleObjects failed error=%lu",
                   GetLastError());
            break;
        }
    }

    UnhookWinEvent(hook);
    ModLog(L"SndVol window monitor stopped");
    return 0;
}

bool EnsureSndVolWindowMonitorStarted() {
    if (g_sndVolWindowMonitorThread) {
        return true;
    }

    HANDLE thread = CreateThread(
        nullptr,
        0,
        SndVolWindowMonitorThreadProc,
        nullptr,
        0,
        nullptr);
    if (!thread) {
        ModLog(L"SndVol monitor: CreateThread failed error=%lu",
               GetLastError());
        return false;
    }

    g_sndVolWindowMonitorThread = thread;
    return true;
}

void UpdateSndVolAnchorRect(const RECT& anchorRect) {
    AcquireSRWLockExclusive(&g_sndVolAnchorLock);
    g_sndVolAnchorRect = anchorRect;
    ReleaseSRWLockExclusive(&g_sndVolAnchorLock);

    g_sndVolAnchorValid.store(true, std::memory_order_release);

    if (EnsureSndVolWindowMonitorStarted()) {
        // Handles relay/single-instance behavior: if this click activates an
        // already-existing SndVol process/window, move that existing window
        // immediately. Future compact/mixer windows are caught by WinEvent.
        RepositionExistingSndVolWindows();
    }
}

bool HandleVolumeClickBeforeLaunch() {
    if (g_volumeClickAction.load(std::memory_order_relaxed) ==
        VolumeClickAction::None) {
        ModLog(L"Volume click ignored by setting");
        return true;
    }

    return false;
}

bool LaunchNativeSndVol(FrameworkElement const& anchorElement) {
    if (HandleVolumeClickBeforeLaunch()) {
        return true;
    }
    wchar_t systemDir[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDir, ARRAYSIZE(systemDir));
    if (!length || length >= ARRAYSIZE(systemDir)) {
        ModLog(L"SndVol launch failed: GetSystemDirectoryW error=%lu",
               GetLastError());
        return false;
    }

    std::wstring applicationPath = systemDir;
    if (!applicationPath.empty() && applicationPath.back() != L'\\') {
        applicationPath.push_back(L'\\');
    }
    applicationPath += L"SndVol.exe";

    const std::wstring parameters = GetSndVolLaunchParameters();
    std::wstring commandLine = L"\"" + applicationPath + L"\"";
    if (!parameters.empty()) {
        commandLine += L" ";
        commandLine += parameters;
    }

    RECT anchorRect{};
    const bool shouldAnchor =
        g_sndVolPlacement.load(std::memory_order_relaxed) ==
            SndVolPlacementMode::AnchorToButton ||
        g_sndVolMixerPlacement.load(std::memory_order_relaxed) ==
            SndVolPlacementMode::AnchorToButton;
    const bool haveAnchorRect =
        shouldAnchor &&
        TryGetXamlElementScreenRect(anchorElement, &anchorRect, L"SndVol");

    std::vector<wchar_t> mutableCommandLine(commandLine.begin(),
                                             commandLine.end());
    mutableCommandLine.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(applicationPath.c_str(),
                        mutableCommandLine.data(),
                        nullptr,
                        nullptr,
                        FALSE,
                        CREATE_UNICODE_ENVIRONMENT,
                        nullptr,
                        nullptr,
                        &si,
                        &pi)) {
        ModLog(L"SndVol launch failed: error=%lu command=%s",
               GetLastError(), commandLine.c_str());
        return false;
    }

    ModLog(L"SndVol launched: pid=%lu parameters=%s",
           pi.dwProcessId,
           parameters.empty() ? L"<empty>" : parameters.c_str());
    CloseHandle(pi.hThread);

    if (shouldAnchor && haveAnchorRect) {
        UpdateSndVolAnchorRect(anchorRect);
    } else if (shouldAnchor && !haveAnchorRect) {
        ModLog(L"SndVol anchor requested but Volume button screen geometry wasn't available; keeping Windows placement");
    }

    CloseHandle(pi.hProcess);
    return true;
}

std::wstring NormalizeButtonOrderToken(std::wstring token) {
    auto isSpace = [](wchar_t ch) {
        return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
    };

    while (!token.empty() && isSpace(token.front())) token.erase(token.begin());
    while (!token.empty() && isSpace(token.back())) token.pop_back();

    for (auto& ch : token) {
        if (ch >= L'A' && ch <= L'Z') ch = ch - L'A' + L'a';
    }

    std::wstring compact;
    compact.reserve(token.size());
    for (wchar_t ch : token) {
        if (ch != L' ' && ch != L'-' && ch != L'_') compact.push_back(ch);
    }

    if (compact == L"ime" || compact == L"imemode" ||
        compact == L"imeindicator") return L"ime";
    if (compact == L"input" || compact == L"language" ||
        compact == L"languages" || compact == L"keyboard" ||
        compact == L"locale") return L"input";
    if (compact == L"networksignin" || compact == L"netsignin")
        return L"network-signin";
    if (compact == L"disconnect" || compact == L"networkdisconnect")
        return L"disconnect";
    if (compact == L"network" || compact == L"net") return L"network";
    if (compact == L"accessibility" || compact == L"easeofaccess" ||
        compact == L"ease" || compact == L"a11y") return L"accessibility";
    if (compact == L"battery") return L"battery";
    if (compact == L"volume" || compact == L"audio" || compact == L"speaker") return L"volume";
    if (compact == L"clock" || compact == L"time" || compact == L"datetime") return L"clock";
    if (compact == L"power" || compact == L"poweroptions") return L"power";

    if (compact.rfind(L"native", 0) == 0 && compact.size() > 6) {
        bool digits = true;
        for (size_t i = 6; i < compact.size(); ++i) {
            if (compact[i] < L'0' || compact[i] > L'9') {
                digits = false;
                break;
            }
        }
        if (digits) return compact;
    }

    return {};
}

std::vector<std::wstring> ParseButtonOrderSetting(const std::wstring& text) {
    std::vector<std::wstring> result;
    std::wstring token;

    auto trim = [](std::wstring value) {
        auto isSpace = [](wchar_t ch) {
            return ch == L' ' || ch == L'\t' ||
                   ch == L'\r' || ch == L'\n';
        };
        while (!value.empty() && isSpace(value.front())) {
            value.erase(value.begin());
        }
        while (!value.empty() && isSpace(value.back())) {
            value.pop_back();
        }
        return value;
    };

    auto compactLower = [](std::wstring value) {
        std::wstring compact;
        compact.reserve(value.size());
        for (wchar_t ch : value) {
            if (ch >= L'A' && ch <= L'Z') {
                ch = ch - L'A' + L'a';
            }
            if (ch != L' ' && ch != L'\t' &&
                ch != L'-' && ch != L'_') {
                compact.push_back(ch);
            }
        }
        return compact;
    };

    auto flush = [&]() {
        std::wstring raw = trim(token);
        token.clear();
        if (raw.empty()) {
            return;
        }

        // Be forgiving of users pasting a YAML-style key into Windhawk's
        // string value, e.g. "buttonOrder: battery,volume,...".
        const size_t colon = raw.find(L':');
        if (colon != std::wstring::npos) {
            const std::wstring prefix =
                compactLower(trim(raw.substr(0, colon)));
            if (prefix == L"buttonorder") {
                ModLog(L"Button-order parser: ignored accidental 'buttonOrder:' prefix");
                raw = trim(raw.substr(colon + 1));
                if (raw.empty()) {
                    return;
                }
            }
        }

        auto normalized = NormalizeButtonOrderToken(raw);
        if (normalized.empty()) {
            ModLog(L"Button-order parser: ignoring unknown token: %s",
                   raw.c_str());
            return;
        }

        if (std::find(result.begin(), result.end(), normalized) !=
            result.end()) {
            ModLog(L"Button-order parser: ignoring duplicate token: %s",
                   normalized.c_str());
            return;
        }

        result.push_back(std::move(normalized));
    };

    for (wchar_t ch : text) {
        if (ch == L',' || ch == L';' || ch == L'>' || ch == L'|') {
            flush();
        } else {
            token.push_back(ch);
        }
    }
    flush();
    return result;
}

using OnLoaded_t = void(*)(void* pThis, void* sender, void* args);
OnLoaded_t g_OnLoaded_Original = nullptr;


constexpr wchar_t kVolumeGlyphMute = L'\uE74F';
constexpr wchar_t kVolumeGlyphDisabledFluent = L'\uEA85'; // VolumeDisabled (Segoe Fluent Icons)
constexpr wchar_t kVolumeGlyph0 = L'\uE992';
constexpr wchar_t kVolumeGlyph1 = L'\uE993';
constexpr wchar_t kVolumeGlyph2 = L'\uE994';
constexpr wchar_t kVolumeGlyph3 = L'\uE995';

constexpr UINT kSndVolGroupMute = 120;
constexpr UINT kSndVolGroup0 = 121;
constexpr UINT kSndVolGroup1 = 122;
constexpr UINT kSndVolGroup2 = 123;
constexpr UINT kSndVolGroup3 = 124;
constexpr UINT kSndVolGroupNoEndpoint = 125;

namespace WRL = Microsoft::WRL;
namespace Core = winrt::Windows::UI::Core;
namespace System = winrt::Windows::System;
namespace Streams = winrt::Windows::Storage::Streams;


#pragma pack(push, 2)
struct NativeGrpIconDir {
    WORD reserved;
    WORD type;
    WORD count;
};

struct NativeGrpIconDirEntry {
    BYTE width;
    BYTE height;
    BYTE colorCount;
    BYTE reserved;
    WORD planes;
    WORD bitCount;
    DWORD bytesInRes;
    WORD iconId;
};
#pragma pack(pop)

void AppendWord(std::vector<uint8_t>& out, WORD value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void AppendDword(std::vector<uint8_t>& out, DWORD value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

bool BuildSndVolGroupIco(UINT groupId,
                        SndVolResourceSource source,
                        std::vector<uint8_t>& icoBytes,
                        std::wstring* resourcePathOut = nullptr) {
    wchar_t windowsDir[MAX_PATH]{};
    const UINT windowsDirLen =
        GetWindowsDirectoryW(windowsDir, ARRAYSIZE(windowsDir));
    if (!windowsDirLen || windowsDirLen >= ARRAYSIZE(windowsDir)) {
        return false;
    }

    std::wstring munPath = windowsDir;
    munPath += L"\\SystemResources\\SndVolSSO.dll.mun";

    std::wstring dllPath = windowsDir;
    dllPath += L"\\System32\\SndVolSSO.dll";

    // Three deliberately different resource-source modes:
    //
    // WindowsAutomatic:
    //   Open the canonical SndVolSSO.dll resource container and let the
    //   Windows resource loader resolve an associated resource file. This is
    //   the closest match to normal Win32 resource loading and avoids this mod
    //   making a DLL-vs-MUN choice itself.
    // DirectDll / DirectMun:
    //   Open exactly the requested container. The caller generation-gates
    //   these diagnostic modes so DirectDll is only used where icons are
    //   embedded in the legacy DLL and DirectMun only where the MUN exists.
    std::wstring resourcePath;
    switch (source) {
        case SndVolResourceSource::WindowsAutomatic:
        case SndVolResourceSource::DirectDll:
            resourcePath = dllPath;
            break;
        case SndVolResourceSource::DirectMun:
            resourcePath = munPath;
            break;
    }

    HMODULE resourceModule = LoadLibraryExW(
        resourcePath.c_str(),
        nullptr,
        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!resourceModule) {
        return false;
    }

    HRSRC probe = FindResourceW(
        resourceModule, MAKEINTRESOURCEW(groupId), RT_GROUP_ICON);
    if (!probe) {
        FreeLibrary(resourceModule);
        return false;
    }

    struct ModuleGuard {
        HMODULE module;
        ~ModuleGuard() {
            if (module) {
                FreeLibrary(module);
            }
        }
    } moduleGuard{resourceModule};

    HRSRC groupResource =
        FindResourceW(resourceModule, MAKEINTRESOURCEW(groupId), RT_GROUP_ICON);
    if (!groupResource) {
        return false;
    }

    HGLOBAL groupHandle = LoadResource(resourceModule, groupResource);
    const DWORD groupSize = SizeofResource(resourceModule, groupResource);
    const auto* groupData =
        static_cast<const uint8_t*>(LockResource(groupHandle));
    if (!groupHandle || !groupData || groupSize < sizeof(NativeGrpIconDir)) {
        return false;
    }

    const auto* groupDir =
        reinterpret_cast<const NativeGrpIconDir*>(groupData);
    if (groupDir->reserved != 0 || groupDir->type != 1 ||
        groupDir->count == 0) {
        return false;
    }

    const size_t entriesBytes =
        static_cast<size_t>(groupDir->count) *
        sizeof(NativeGrpIconDirEntry);
    if (sizeof(NativeGrpIconDir) + entriesBytes > groupSize) {
        return false;
    }

    const auto* groupEntries =
        reinterpret_cast<const NativeGrpIconDirEntry*>(
            groupData + sizeof(NativeGrpIconDir));

    struct IconPayload {
        NativeGrpIconDirEntry entry;
        const uint8_t* data;
        DWORD size;
    };

    std::vector<IconPayload> payloads;
    payloads.reserve(groupDir->count);

    for (WORD i = 0; i < groupDir->count; ++i) {
        HRSRC iconResource = FindResourceW(
            resourceModule,
            MAKEINTRESOURCEW(groupEntries[i].iconId),
            RT_ICON);
        if (!iconResource) {
            return false;
        }

        HGLOBAL iconHandle = LoadResource(resourceModule, iconResource);
        const DWORD iconSize = SizeofResource(resourceModule, iconResource);
        const auto* iconData =
            static_cast<const uint8_t*>(LockResource(iconHandle));
        if (!iconHandle || !iconData || iconSize == 0) {
            return false;
        }

        payloads.push_back(
            IconPayload{groupEntries[i], iconData, iconSize});
    }

    // RT_GROUP_ICON stores resource IDs instead of ICO file offsets.
    // Rebuild only the standard ICO container around the original RT_ICON
    // payloads. The native Windows artwork remains byte-for-byte unchanged.
    const DWORD directorySize =
        6u + static_cast<DWORD>(payloads.size()) * 16u;
    DWORD imageOffset = directorySize;

    size_t totalSize = directorySize;
    for (const auto& payload : payloads) {
        totalSize += payload.size;
    }

    icoBytes.clear();
    icoBytes.reserve(totalSize);

    AppendWord(icoBytes, 0);
    AppendWord(icoBytes, 1);
    AppendWord(icoBytes, static_cast<WORD>(payloads.size()));

    for (const auto& payload : payloads) {
        icoBytes.push_back(payload.entry.width);
        icoBytes.push_back(payload.entry.height);
        icoBytes.push_back(payload.entry.colorCount);
        icoBytes.push_back(payload.entry.reserved);
        AppendWord(icoBytes, payload.entry.planes);
        AppendWord(icoBytes, payload.entry.bitCount);
        AppendDword(icoBytes, payload.size);
        AppendDword(icoBytes, imageOffset);
        imageOffset += payload.size;
    }

    for (const auto& payload : payloads) {
        icoBytes.insert(
            icoBytes.end(), payload.data, payload.data + payload.size);
    }

    if (resourcePathOut) {
        *resourcePathOut = std::move(resourcePath);
    }
    return true;
}

Media::Imaging::BitmapImage LoadSndVolGroupBitmap(
    UINT groupId,
    SndVolResourceSource source) {
    std::vector<uint8_t> icoBytes;
    std::wstring resourcePath;
    if (!BuildSndVolGroupIco(groupId, source, icoBytes, &resourcePath)) {
        ModLog(L"Couldn't load SndVolSSO icon group %u using source mode %d",
               groupId,
               static_cast<int>(source));
        return nullptr;
    }

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, icoBytes.size());
    if (!memory) {
        return nullptr;
    }

    void* bytes = GlobalLock(memory);
    if (!bytes) {
        GlobalFree(memory);
        return nullptr;
    }
    CopyMemory(bytes, icoBytes.data(), icoBytes.size());
    GlobalUnlock(memory);

    WRL::ComPtr<IStream> stream;
    HRESULT hr =
        CreateStreamOnHGlobal(memory, TRUE, stream.GetAddressOf());
    if (FAILED(hr)) {
        GlobalFree(memory);
        return nullptr;
    }

    HMODULE shcore = GetModuleHandleW(L"shcore.dll");
    if (!shcore) {
        shcore = LoadLibraryW(L"shcore.dll");
    }
    if (!shcore) {
        return nullptr;
    }

    using CreateRandomAccessStreamOverStream_t =
        HRESULT(WINAPI*)(IStream*, DWORD, REFIID, void**);
    auto createRandomAccessStream =
        reinterpret_cast<CreateRandomAccessStreamOverStream_t>(
            GetProcAddress(shcore, "CreateRandomAccessStreamOverStream"));
    if (!createRandomAccessStream) {
        return nullptr;
    }

    const auto winrtIid =
        winrt::guid_of<Streams::IRandomAccessStream>();
    const GUID& iid =
        reinterpret_cast<const GUID&>(winrtIid);

    void* randomAccessRaw = nullptr;
    hr = createRandomAccessStream(
        stream.Get(), 0, iid, &randomAccessRaw);
    if (FAILED(hr) || !randomAccessRaw) {
        ModLog(
            L"CreateRandomAccessStreamOverStream failed for "
            L"SndVolSSO group %u: 0x%08X",
            groupId,
            static_cast<unsigned>(hr));
        return nullptr;
    }

    Streams::IRandomAccessStream randomAccessStream{
        randomAccessRaw, winrt::take_ownership_from_abi};

    try {
        Media::Imaging::BitmapImage bitmap;
        bitmap.SetSource(randomAccessStream);
        ModLog(L"Loaded native SndVolSSO icon group %u from %s",
               groupId,
               resourcePath.c_str());
        return bitmap;
    } catch (const winrt::hresult_error& e) {
        ModLog(
            L"BitmapImage::SetSource failed for SndVolSSO group %u: "
            L"0x%08X %s",
            groupId,
            static_cast<unsigned>(e.code().value),
            e.message().c_str());
    } catch (...) {
        ModLog(
            L"BitmapImage::SetSource failed for SndVolSSO group %u",
            groupId);
    }

    return nullptr;
}

struct NativeSndVolVisuals {
    Controls::Image image{nullptr};
    Media::Imaging::BitmapImage muted{nullptr};
    Media::Imaging::BitmapImage zero{nullptr};
    Media::Imaging::BitmapImage low{nullptr};
    Media::Imaging::BitmapImage medium{nullptr};
    Media::Imaging::BitmapImage high{nullptr};
    Media::Imaging::BitmapImage noEndpoint{nullptr};

    bool Complete() const {
        return image && muted && zero && low && medium && high && noEndpoint;
    }
};

struct VolumeMonitorState;
void QueueEndpointRebind(std::shared_ptr<VolumeMonitorState> const& state);

struct AudioCallbackInFlightGuard {
    AudioCallbackInFlightGuard() {
        g_audioCallbacksInFlight.fetch_add(1, std::memory_order_acq_rel);
    }
    ~AudioCallbackInFlightGuard() {
        g_audioCallbacksInFlight.fetch_sub(1, std::memory_order_acq_rel);
    }
};

class EndpointVolumeCallback final
    : public IAudioEndpointVolumeCallback,
      public IAgileObject {
public:
    explicit EndpointVolumeCallback(std::weak_ptr<VolumeMonitorState> state)
        : state_(std::move(state)) {
        g_audioCallbackObjectsAlive.fetch_add(1, std::memory_order_acq_rel);
    }

    ~EndpointVolumeCallback() {
        g_audioCallbackObjectsAlive.fetch_sub(1, std::memory_order_acq_rel);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        AudioCallbackInFlightGuard guard;
        if (!object) {
            return E_POINTER;
        }
        *object = nullptr;
        if (riid == kIidIUnknown ||
            riid == __uuidof(IAudioEndpointVolumeCallback)) {
            *object = static_cast<IAudioEndpointVolumeCallback*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == __uuidof(IAgileObject)) {
            *object = static_cast<IAgileObject*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        AudioCallbackInFlightGuard guard;
        return ++refCount_;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        AudioCallbackInFlightGuard guard;
        const ULONG remaining = --refCount_;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE OnNotify(
        PAUDIO_VOLUME_NOTIFICATION_DATA data) override;

private:
    std::atomic<ULONG> refCount_{1};
    std::weak_ptr<VolumeMonitorState> state_;
};

class DefaultDeviceNotificationCallback final
    : public IMMNotificationClient,
      public IAgileObject {
public:
    explicit DefaultDeviceNotificationCallback(
        std::weak_ptr<VolumeMonitorState> state)
        : state_(std::move(state)) {
        g_audioCallbackObjectsAlive.fetch_add(1, std::memory_order_acq_rel);
    }

    ~DefaultDeviceNotificationCallback() {
        g_audioCallbackObjectsAlive.fetch_sub(1, std::memory_order_acq_rel);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        AudioCallbackInFlightGuard guard;
        if (!object) {
            return E_POINTER;
        }
        *object = nullptr;
        if (riid == kIidIUnknown ||
            riid == __uuidof(IMMNotificationClient)) {
            *object = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == __uuidof(IAgileObject)) {
            *object = static_cast<IAgileObject*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        AudioCallbackInFlightGuard guard;
        return ++refCount_;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        AudioCallbackInFlightGuard guard;
        const ULONG remaining = --refCount_;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override;
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override;
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override;
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(
        EDataFlow flow,
        ERole role,
        LPCWSTR) override;
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(
        LPCWSTR,
        const PROPERTYKEY) override {
        AudioCallbackInFlightGuard guard;
        return S_OK;
    }

private:
    std::atomic<ULONG> refCount_{1};
    std::weak_ptr<VolumeMonitorState> state_;
};

constexpr PROPERTYKEY kPkeyDeviceFriendlyName = {
    {0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}},
    14
};

std::wstring ReadEndpointFriendlyName(IMMDevice* device) {
    if (!device) {
        return {};
    }

    WRL::ComPtr<IPropertyStore> store;
    HRESULT hr = device->OpenPropertyStore(STGM_READ, &store);
    if (FAILED(hr) || !store) {
        return {};
    }

    PROPVARIANT value{};
    hr = store->GetValue(kPkeyDeviceFriendlyName, &value);
    std::wstring result;
    if (SUCCEEDED(hr) && value.vt == VT_LPWSTR && value.pwszVal) {
        result.assign(value.pwszVal);
    }
    PropVariantClear(&value);
    return result;
}

std::wstring ReadEndpointId(IMMDevice* device) {
    if (!device) {
        return {};
    }

    LPWSTR rawId = nullptr;
    const HRESULT hr = device->GetId(&rawId);
    if (FAILED(hr) || !rawId) {
        return {};
    }

    std::wstring result(rawId);
    CoTaskMemFree(rawId);
    return result;
}

struct WheelDeltaAccumulator {
    int remainder = 0;
    ULONGLONG lastEventTick = 0;
    bool highResolutionStream = false;
};

struct WheelInputResult {
    int rawDelta = 0;
    int effectiveDelta = 0;
    int steps = 0;
    int remainder = 0;
    int deltaPerStep = WHEEL_DELTA;
    bool highResolution = false;
};

bool TryConsumeVerticalWheelInput(
        UIElement const& relativeTo,
        Input::PointerRoutedEventArgs const& args,
        bool reverse,
        WheelDeltaAccumulator& accumulator,
        WheelInputResult* result) {
    if (!relativeTo || !result) {
        return false;
    }

    int rawDelta = 0;
    bool horizontal = false;
    try {
        auto point = args.GetCurrentPoint(relativeTo);
        auto properties = point.Properties();
        rawDelta = properties.MouseWheelDelta();
        horizontal = properties.IsHorizontalMouseWheel();
    } catch (...) {
        return false;
    }

    if (horizontal || rawDelta == 0) {
        return false;
    }

    const int effectiveDelta = reverse ? -rawDelta : rawDelta;
    const ULONGLONG now = GetTickCount64();

    // XAML doesn't expose a reliable "precision touchpad" source flag for
    // these routed wheel events. Both a touchpad and a mouse are represented
    // through MouseWheelDelta. Treat a burst as high-resolution when any
    // packet isn't an exact WHEEL_DELTA (120) multiple. Keep that
    // classification sticky for the short gesture, then reset after idle so a
    // later physical mouse-wheel detent uses the normal per-control step.
    constexpr ULONGLONG kWheelGestureResetMs = 400;
    const bool newGesture =
        accumulator.lastEventTick == 0 ||
        now - accumulator.lastEventTick > kWheelGestureResetMs;
    if (newGesture) {
        accumulator.remainder = 0;
        accumulator.highResolutionStream = false;
    }

    const int absDelta =
        effectiveDelta == INT_MIN ? INT_MAX : std::abs(effectiveDelta);
    if (absDelta % WHEEL_DELTA != 0) {
        accumulator.highResolutionStream = true;
    }
    accumulator.lastEventTick = now;

    // A traditional mouse wheel keeps the canonical 120-unit detent. For a
    // high-resolution stream, decouple output granularity from sensitivity:
    // e.g. 1% every 30 delta units gives smooth 1% changes without requiring
    // four times as much finger travel as a normal wheel detent.
    const int deltaPerStep =
        accumulator.highResolutionStream
            ? g_precisionScrollDeltaPerStep.load(
                  std::memory_order_relaxed)
            : WHEEL_DELTA;

    const long long total =
        static_cast<long long>(accumulator.remainder) +
        static_cast<long long>(effectiveDelta);
    const int steps =
        static_cast<int>(total / static_cast<long long>(deltaPerStep));
    accumulator.remainder =
        static_cast<int>(total % static_cast<long long>(deltaPerStep));

    result->rawDelta = rawDelta;
    result->effectiveDelta = effectiveDelta;
    result->steps = steps;
    result->remainder = accumulator.remainder;
    result->deltaPerStep = deltaPerStep;
    result->highResolution = accumulator.highResolutionStream;
    return true;
}

struct VolumeMonitorState {
    Controls::TextBlock icon{nullptr};
    Controls::TextBlock percentText{nullptr};
    Controls::Button button{nullptr};
    Controls::ToolTip toolTip{nullptr};
    hstring endpointName;
    std::wstring endpointId;
    NativeSndVolVisuals nativeSndVol{};
    System::DispatcherQueue dispatcherQueue{nullptr};
    Core::CoreDispatcher coreDispatcher{nullptr};
    VolumeUiHandoffMode requestedHandoff = VolumeUiHandoffMode::Automatic;
    VolumeUiHandoffMode activeHandoff = VolumeUiHandoffMode::Automatic;
    DWORD uiThreadId = 0;
    HWND legacyUiWindow = nullptr;
    std::atomic<unsigned> legacyPendingVolume{0};
    std::atomic<bool> legacyVolumePostPending{false};
    std::atomic<unsigned> lastQueuedSignature{0xFFFFFFFFu};
    WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    WRL::ComPtr<IAudioEndpointVolume> endpointVolume;
    WRL::ComPtr<IAudioEndpointVolumeCallback> endpointCallback;
    WRL::ComPtr<IMMNotificationClient> deviceCallback;
    wchar_t lastGlyph = 0;
    int lastPercent = -1;
    bool lastMuted = false;
    HRESULT lastError = S_OK;
    bool hasReading = false;
    UINT lastNativeGroup = 0;
    std::atomic<bool> stopping{false};
    std::atomic<bool> cleanupDone{false};
    std::atomic<bool> rebindPending{false};
    winrt::Windows::Foundation::IInspectable wheelHandler{nullptr};
    bool wheelHandlerAttached = false;
    winrt::Windows::Foundation::IInspectable middleClickHandler{nullptr};
    bool middleClickHandlerAttached = false;
    WheelDeltaAccumulator wheelAccumulator{};
    int mutedUnmuteProgressSteps = 0;
    int mutedUnmuteDirection = 0;
    wchar_t noEndpointGlyph = kVolumeGlyphDisabledFluent;
};

HRESULT RebindDefaultPlaybackEndpoint(
    std::shared_ptr<VolumeMonitorState> const& state);

constexpr UINT kLegacyVolumeUiReadingMessage = WM_APP + 0x421;
constexpr UINT kLegacyVolumeUiRebindMessage = WM_APP + 0x422;
constexpr wchar_t kLegacyVolumeUiWindowClass[] =
    L"Windhawk.LogonUI.Tweaks.VolumeUiBridge.v2";

LRESULT CALLBACK LegacyVolumeUiWndProc(HWND hwnd, UINT message,
                                       WPARAM wParam, LPARAM lParam);
HWND CreateLegacyVolumeUiWindow(
    std::shared_ptr<VolumeMonitorState> const& state);

Media::Imaging::BitmapImage NativeSndVolBitmapForGroup(
    VolumeMonitorState const& state,
    UINT groupId) {
    switch (groupId) {
        case kSndVolGroupMute:
            return state.nativeSndVol.muted;
        case kSndVolGroup0:
            return state.nativeSndVol.zero;
        case kSndVolGroup1:
            return state.nativeSndVol.low;
        case kSndVolGroup2:
            return state.nativeSndVol.medium;
        case kSndVolGroup3:
            return state.nativeSndVol.high;
        case kSndVolGroupNoEndpoint:
            return state.nativeSndVol.noEndpoint;
        default:
            return nullptr;
    }
}

UINT ChooseNativeSndVolGroup(float scalar, bool muted) {
    if (muted) {
        return kSndVolGroupMute;
    }

    // Match the native SndVolSSO.dll tray-icon logic exactly:
    // percentage = trunc((scalar * 100.0f) + 0.5f)
    //   0      -> group 121
    //   1..32  -> group 122
    //   33..65 -> group 123
    //   66..100-> group 124
    int percent = static_cast<int>((scalar * 100.0f) + 0.5f);
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }

    if (percent < 1) {
        return kSndVolGroup0;
    }
    if (percent < 33) {
        return kSndVolGroup1;
    }
    if (percent < 66) {
        return kSndVolGroup2;
    }
    return kSndVolGroup3;
}

void UpdateVolumeToolTip(std::shared_ptr<VolumeMonitorState> const& state,
                         int percent,
                         bool noEndpoint) {
    if (!state || !state->toolTip) {
        return;
    }

    try {
        if (noEndpoint) {
            state->toolTip.Content(box_value(hstring(L"No audio output device is installed")));
            return;
        }

        std::wstring text;
        if (!state->endpointName.empty()) {
            text.assign(state->endpointName.c_str(), state->endpointName.size());
        } else {
            text = L"Volume";
        }
        text += L": ";
        text += std::to_wstring(percent);
        text += L"%";
        state->toolTip.Content(box_value(hstring(text)));
    } catch (...) {
    }
}

void UpdateVolumeAutomationName(
    std::shared_ptr<VolumeMonitorState> const& state,
    int percent,
    bool noEndpoint,
    bool muted) {
    if (!state || !state->button) {
        return;
    }

    try {
        std::wstring text;
        if (noEndpoint) {
            text = L"No audio output device is installed";
        } else {
            if (!state->endpointName.empty()) {
                text.assign(state->endpointName.c_str(), state->endpointName.size());
            } else {
                text = L"Volume";
            }
            text += L": ";
            text += std::to_wstring(percent);
            text += L"%";
            if (muted) {
                text += L" (muted)";
            }
        }

        Automation::AutomationProperties::SetName(
            state->button, hstring(text));
    } catch (...) {
    }
}

void ApplyNoPlaybackEndpoint(std::shared_ptr<VolumeMonitorState> const& state) {
    if (!state || state->stopping.load() || !state->icon) {
        return;
    }

    UpdateVolumeToolTip(state, -1, true);
    UpdateVolumeAutomationName(state, -1, true, false);

    if (state->percentText) {
        SetPercentageLabelText(state->percentText, L"", false);
    }

    if (state->nativeSndVol.Complete()) {
        state->icon.Visibility(Visibility::Collapsed);
        state->nativeSndVol.image.Visibility(Visibility::Visible);
        if (state->lastNativeGroup != kSndVolGroupNoEndpoint) {
            state->nativeSndVol.image.Source(state->nativeSndVol.noEndpoint);
        }

        if (!state->hasReading ||
            state->lastNativeGroup != kSndVolGroupNoEndpoint ||
            state->lastPercent != -1) {
            ModLog(L"Volume state: no playback endpoint nativeGroup=%u",
                   kSndVolGroupNoEndpoint);
        }

        state->lastNativeGroup = kSndVolGroupNoEndpoint;
        state->lastGlyph = 0;
        state->lastPercent = -1;
        state->lastMuted = false;
        state->hasReading = true;
        return;
    }

    // Modern Win11 path: native Fluent disabled-volume glyph.
    state->icon.Visibility(Visibility::Visible);
    const wchar_t glyph = state->noEndpointGlyph;
    if (!state->hasReading ||
        state->lastGlyph != glyph ||
        state->lastPercent != -1) {
        wchar_t text[2]{glyph, L'\0'};
        state->icon.Text(text);
        ModLog(L"Volume state: no playback endpoint glyph=U+%04X",
               static_cast<unsigned>(glyph));
        state->lastGlyph = glyph;
        state->lastNativeGroup = 0;
        state->lastPercent = -1;
        state->lastMuted = false;
        state->hasReading = true;
    }
}

wchar_t ChooseVolumeGlyph(float scalar, bool muted) {
    if (muted) {
        return kVolumeGlyphMute;
    }

    // Keep the font renderer on the same state boundaries as native
    // SndVolSSO: round to an integer percentage, then 0 / 1..32 /
    // 33..65 / 66..100.
    int percent = static_cast<int>((scalar * 100.0f) + 0.5f);
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }

    if (percent < 1) {
        return kVolumeGlyph0;
    }
    if (percent < 33) {
        return kVolumeGlyph1;
    }
    if (percent < 66) {
        return kVolumeGlyph2;
    }
    return kVolumeGlyph3;
}

void ApplyVolumeReading(std::shared_ptr<VolumeMonitorState> const& state,
                        float scalar,
                        bool muted) {
    if (!state || state->stopping.load() || !state->icon) {
        return;
    }

    if (scalar < 0.0f) {
        scalar = 0.0f;
    } else if (scalar > 1.0f) {
        scalar = 1.0f;
    }

    const int percent =
        static_cast<int>(std::lround(static_cast<double>(scalar) * 100.0));

    UpdateVolumeToolTip(state, percent, false);
    UpdateVolumeAutomationName(state, percent, false, muted);

    if (state->percentText) {
        SetPercentageLabelText(
            state->percentText,
            FormatPercentageText(percent, true, muted),
            g_showVolumePercentage.load(std::memory_order_relaxed));
    }

    if (state->nativeSndVol.Complete()) {
        const UINT groupId = ChooseNativeSndVolGroup(scalar, muted);
        state->icon.Visibility(Visibility::Collapsed);
        state->nativeSndVol.image.Visibility(Visibility::Visible);
        if (state->lastNativeGroup != groupId) {
            auto bitmap = NativeSndVolBitmapForGroup(*state, groupId);
            if (bitmap) {
                state->nativeSndVol.image.Source(bitmap);
            }
        }

        if (!state->hasReading ||
            state->lastNativeGroup != groupId ||
            state->lastPercent != percent ||
            state->lastMuted != muted) {
            ModLog(L"Volume state: %d%% mute=%d nativeGroup=%u",
                   percent,
                   muted ? 1 : 0,
                   groupId);
        }

        state->lastNativeGroup = groupId;
        state->lastGlyph = 0;
        state->lastPercent = percent;
        state->lastMuted = muted;
        state->hasReading = true;
        return;
    }

    const wchar_t glyph = ChooseVolumeGlyph(scalar, muted);
    state->icon.Visibility(Visibility::Visible);

    if (!state->hasReading ||
        state->lastGlyph != glyph ||
        state->lastPercent != percent ||
        state->lastMuted != muted) {
        wchar_t text[2]{glyph, L'\0'};
        state->icon.Text(text);

        ModLog(L"Volume state: %d%% mute=%d glyph=U+%04X",
               percent,
               muted ? 1 : 0,
               static_cast<unsigned>(glyph));

        state->lastGlyph = glyph;
        state->lastNativeGroup = 0;
        state->lastPercent = percent;
        state->lastMuted = muted;
        state->hasReading = true;
    }
}

void QueueVolumeReading(std::shared_ptr<VolumeMonitorState> const& state,
                        float scalar,
                        bool muted) {
    if (!state || state->stopping.load() ||
        g_unloading.load(std::memory_order_acquire)) {
        return;
    }

    if (scalar < 0.0f) {
        scalar = 0.0f;
    } else if (scalar > 1.0f) {
        scalar = 1.0f;
    }

    const int percent = static_cast<int>(
        std::lround(static_cast<double>(scalar) * 100.0));
    const unsigned signature =
        static_cast<unsigned>(percent) | (muted ? 0x100u : 0u);
    if (state->lastQueuedSignature.exchange(signature) == signature) {
        return;
    }

    if (GetCurrentThreadId() == state->uiThreadId) {
        ApplyVolumeReading(state, scalar, muted);
        return;
    }

    std::weak_ptr<VolumeMonitorState> weak = state;

    switch (state->activeHandoff) {
        case VolumeUiHandoffMode::DispatcherQueue: {
            if (!state->dispatcherQueue) {
                ModLog(L"Volume UI update dropped: DispatcherQueue explicitly selected but unavailable");
                return;
            }
            try {
                if (state->dispatcherQueue.TryEnqueue(
                        [weak, scalar, muted]() {
                            if (auto locked = weak.lock()) {
                                ApplyVolumeReading(locked, scalar, muted);
                            }
                        })) {
                    return;
                }
                ModLog(L"Volume DispatcherQueue rejected UI update");
            } catch (const winrt::hresult_error& e) {
                ModLog(L"Volume DispatcherQueue failed: 0x%08X %s",
                       static_cast<unsigned>(e.code().value),
                       e.message().c_str());
            } catch (...) {
                ModLog(L"Volume DispatcherQueue failed: unknown exception");
            }
            return;
        }

        case VolumeUiHandoffMode::CoreDispatcher: {
            if (!state->coreDispatcher) {
                ModLog(L"Volume UI update dropped: CoreDispatcher explicitly selected but unavailable");
                return;
            }
            try {
                // Do not install a Completed delegate here. A completion
                // callback can outlive the XAML surface during hot-unload even
                // after the actual UI work has run. The queued work itself is
                // fenced during Wh_ModBeforeUninit.
                state->coreDispatcher.TryRunAsync(
                    Core::CoreDispatcherPriority::Normal,
                    [weak, scalar, muted]() {
                        if (auto locked = weak.lock()) {
                            ApplyVolumeReading(locked, scalar, muted);
                        }
                    });
            } catch (const winrt::hresult_error& e) {
                ModLog(L"Volume CoreDispatcher failed: 0x%08X %s",
                       static_cast<unsigned>(e.code().value),
                       e.message().c_str());
            } catch (...) {
                ModLog(L"Volume CoreDispatcher failed: unknown exception");
            }
            return;
        }

        case VolumeUiHandoffMode::LegacyMessageWindow: {
            state->legacyPendingVolume.store(signature);
            if (!state->legacyUiWindow) {
                ModLog(L"Volume UI update dropped: legacy message-window handoff selected but unavailable");
                return;
            }
            if (!state->legacyVolumePostPending.exchange(true)) {
                if (!PostMessageW(state->legacyUiWindow,
                                  kLegacyVolumeUiReadingMessage,
                                  0,
                                  0)) {
                    state->legacyVolumePostPending.store(false);
                    ModLog(L"Volume legacy UI bridge PostMessage failed: %lu",
                           GetLastError());
                }
            }
            return;
        }

        case VolumeUiHandoffMode::Automatic:
        default:
            // activeHandoff is resolved once on the XAML UI thread when the
            // monitor starts, so Automatic should never remain active here.
            ModLog(L"Volume UI update dropped: automatic handoff was not resolved");
            return;
    }
}

HRESULT QueryEndpointVolume(IAudioEndpointVolume* endpoint,
                            float* scalar,
                            bool* muted) {
    if (!endpoint || !scalar || !muted) {
        return E_POINTER;
    }

    BOOL isMuted = FALSE;
    HRESULT hr = endpoint->GetMute(&isMuted);
    if (FAILED(hr)) {
        return hr;
    }

    float level = 0.0f;
    hr = endpoint->GetMasterVolumeLevelScalar(&level);
    if (FAILED(hr)) {
        return hr;
    }

    *scalar = level;
    *muted = isMuted != FALSE;
    return S_OK;
}

HRESULT RebindDefaultPlaybackEndpoint(
    std::shared_ptr<VolumeMonitorState> const& state) {
    if (!state || state->stopping.load() || !state->enumerator) {
        return E_ABORT;
    }

    WRL::ComPtr<IMMDevice> device;
    HRESULT hr = state->enumerator->GetDefaultAudioEndpoint(
        eRender, eMultimedia, &device);
    if (FAILED(hr)) {
        hr = state->enumerator->GetDefaultAudioEndpoint(
            eRender, eConsole, &device);
        if (FAILED(hr)) {
            if (state->endpointVolume && state->endpointCallback) {
                state->endpointVolume->UnregisterControlChangeNotify(
                    state->endpointCallback.Get());
            }
            state->endpointVolume.Reset();
            state->endpointName = {};
            state->endpointId.clear();

            if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
                ApplyNoPlaybackEndpoint(state);
            }
            return hr;
        }
    }

    const std::wstring newEndpointId = ReadEndpointId(device.Get());

    // Windows can issue several device/default-role notifications that all
    // still resolve to the exact same render endpoint. Avoid tearing down and
    // recreating the endpoint callback in that case. First verify that the
    // existing endpoint object is still healthy; if it isn't, fall through to
    // a real rebind.
    if (state->endpointVolume &&
        !state->endpointId.empty() &&
        !newEndpointId.empty() &&
        state->endpointId == newEndpointId) {
        float scalar = 0.0f;
        bool muted = false;
        const HRESULT queryHr =
            QueryEndpointVolume(state->endpointVolume.Get(),
                                &scalar,
                                &muted);
        if (SUCCEEDED(queryHr)) {
            ApplyVolumeReading(state, scalar, muted);
            ModLog(L"Default playback endpoint notification resolved to the already-bound endpoint; rebind skipped");
            return S_FALSE;
        }

        ModLog(L"Already-bound playback endpoint failed health check 0x%08X; forcing rebind",
               static_cast<unsigned>(queryHr));
    }

    if (state->endpointVolume && state->endpointCallback) {
        state->endpointVolume->UnregisterControlChangeNotify(
            state->endpointCallback.Get());
    }
    state->endpointVolume.Reset();
    state->endpointName = {};
    state->endpointId.clear();

    try {
        const std::wstring friendlyName =
            ReadEndpointFriendlyName(device.Get());
        if (!friendlyName.empty()) {
            state->endpointName = hstring(friendlyName.c_str());
            ModLog(L"Volume endpoint friendly name: %s",
                   state->endpointName.c_str());
        } else {
            ModLog(L"Volume endpoint friendly name unavailable; tooltip will use generic label");
        }
    } catch (...) {
        state->endpointName = {};
    }

    hr = device->Activate(__uuidof(IAudioEndpointVolume),
                          CLSCTX_INPROC_SERVER,
                          nullptr,
                          reinterpret_cast<void**>(
                              state->endpointVolume.GetAddressOf()));
    if (FAILED(hr)) {
        return hr;
    }

    hr = state->endpointVolume->RegisterControlChangeNotify(
        state->endpointCallback.Get());
    if (FAILED(hr)) {
        state->endpointVolume.Reset();
        return hr;
    }

    state->endpointId = newEndpointId;

    float scalar = 0.0f;
    bool muted = false;
    hr = QueryEndpointVolume(state->endpointVolume.Get(), &scalar, &muted);
    if (SUCCEEDED(hr)) {
        ApplyVolumeReading(state, scalar, muted);
    }

    ModLog(L"Live Volume endpoint bound (event-driven)%s",
           state->endpointId.empty()
               ? L""
               : L" with stable device identity");
    return hr;
}

LRESULT CALLBACK LegacyVolumeUiWndProc(HWND hwnd, UINT message,
                                       WPARAM wParam, LPARAM lParam) {
    auto* holder = reinterpret_cast<std::shared_ptr<VolumeMonitorState>*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        holder = reinterpret_cast<std::shared_ptr<VolumeMonitorState>*>(
            create ? create->lpCreateParams : nullptr);
        if (!holder) {
            return FALSE;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(holder));
        return TRUE;
    }

    if (message == kLegacyVolumeUiReadingMessage) {
        if (holder && *holder) {
            auto state = *holder;
            // Clear first, then read the newest coalesced value. If another
            // callback races in after this point it will post a fresh message.
            state->legacyVolumePostPending.store(false);
            const unsigned signature = state->legacyPendingVolume.load();
            const int percent = static_cast<int>(signature & 0xFFu);
            const bool muted = (signature & 0x100u) != 0;
            if (!state->stopping.load()) {
                ApplyVolumeReading(state,
                                   static_cast<float>(percent) / 100.0f,
                                   muted);
            }
        }
        return 0;
    }

    if (message == kLegacyVolumeUiRebindMessage) {
        if (holder && *holder) {
            auto state = *holder;
            state->rebindPending.store(false);
            if (!state->stopping.load()) {
                HRESULT hr = RebindDefaultPlaybackEndpoint(state);
                if (FAILED(hr)) {
                    ModLog(L"Volume endpoint rebind failed: 0x%08X",
                           static_cast<unsigned>(hr));
                } else if (hr == S_FALSE) {
                    ModLog(L"Default playback endpoint unchanged; existing live Volume binding kept");
                } else {
                    ModLog(L"Default playback endpoint changed; live Volume rebound");
                }
            }
        }
        return 0;
    }

    if (message == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }

    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete holder;
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND CreateLegacyVolumeUiWindow(
    std::shared_ptr<VolumeMonitorState> const& state) {
    if (!state) {
        return nullptr;
    }

    if (!EnsurePrivateProcessWindowClass(
            kLegacyVolumeUiWindowClass, LegacyVolumeUiWndProc)) {
        ModLog(L"Volume legacy UI bridge class unavailable; legacy handoff disabled for this mod instance");
        return nullptr;
    }

    auto* holder = new (std::nothrow)
        std::shared_ptr<VolumeMonitorState>(state);
    if (!holder) {
        ModLog(L"Volume legacy UI bridge couldn't allocate state holder");
        return nullptr;
    }

    HWND hwnd = CreateWindowExW(
        0,
        kLegacyVolumeUiWindowClass,
        L"",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        GetModuleHandleW(nullptr),
        holder);
    if (!hwnd) {
        const DWORD error = GetLastError();
        delete holder;
        ModLog(L"Volume legacy UI bridge CreateWindowEx failed: %lu", error);
        return nullptr;
    }

    return hwnd;
}

void QueueEndpointRebind(std::shared_ptr<VolumeMonitorState> const& state) {
    if (!state || state->stopping.load() ||
        g_unloading.load(std::memory_order_acquire)) {
        return;
    }

    bool expected = false;
    if (!state->rebindPending.compare_exchange_strong(expected, true)) {
        return;
    }

    std::weak_ptr<VolumeMonitorState> weak = state;
    auto work = [weak]() {
        if (auto locked = weak.lock()) {
            locked->rebindPending.store(false);
            if (locked->stopping.load()) {
                return;
            }
            HRESULT hr = RebindDefaultPlaybackEndpoint(locked);
            if (FAILED(hr)) {
                    ModLog(L"Volume endpoint rebind failed: 0x%08X",
                           static_cast<unsigned>(hr));
                } else if (hr == S_FALSE) {
                    ModLog(L"Default playback endpoint unchanged; existing live Volume binding kept");
                } else {
                    ModLog(L"Default playback endpoint changed; live Volume rebound");
                }
        }
    };

    if (GetCurrentThreadId() == state->uiThreadId) {
        work();
        return;
    }

    switch (state->activeHandoff) {
        case VolumeUiHandoffMode::DispatcherQueue:
            if (state->dispatcherQueue) {
                try {
                    if (state->dispatcherQueue.TryEnqueue(work)) {
                        return;
                    }
                } catch (...) {
                }
            }
            break;

        case VolumeUiHandoffMode::CoreDispatcher:
            if (state->coreDispatcher) {
                try {
                    // As above, avoid a separate Completed delegate whose
                    // lifetime can extend past mod hot-unload.
                    state->coreDispatcher.TryRunAsync(
                        Core::CoreDispatcherPriority::Normal, work);
                    return;
                } catch (...) {
                }
            }
            break;

        case VolumeUiHandoffMode::LegacyMessageWindow:
            if (state->legacyUiWindow &&
                PostMessageW(state->legacyUiWindow,
                             kLegacyVolumeUiRebindMessage,
                             0,
                             0)) {
                return;
            }
            if (state->legacyUiWindow) {
                ModLog(L"Volume legacy UI bridge rebind PostMessage failed: %lu",
                       GetLastError());
            }
            break;

        case VolumeUiHandoffMode::Automatic:
        default:
            break;
    }

    state->rebindPending.store(false);
    ModLog(L"Default playback endpoint change dropped: selected UI handoff unavailable or failed");
}

HRESULT STDMETHODCALLTYPE EndpointVolumeCallback::OnNotify(
    PAUDIO_VOLUME_NOTIFICATION_DATA data) {
    AudioCallbackInFlightGuard guard;
    if (!data) {
        return E_INVALIDARG;
    }

    if (auto state = state_.lock()) {
        QueueVolumeReading(state,
                           data->fMasterVolume,
                           data->bMuted != FALSE);
    } else {
        ModLog(L"Volume endpoint callback fired after monitor state expired");
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DefaultDeviceNotificationCallback::OnDeviceStateChanged(
    LPCWSTR, DWORD) {
    AudioCallbackInFlightGuard guard;
    if (auto state = state_.lock()) {
        QueueEndpointRebind(state);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DefaultDeviceNotificationCallback::OnDeviceAdded(
    LPCWSTR) {
    AudioCallbackInFlightGuard guard;
    if (auto state = state_.lock()) {
        QueueEndpointRebind(state);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DefaultDeviceNotificationCallback::OnDeviceRemoved(
    LPCWSTR) {
    AudioCallbackInFlightGuard guard;
    if (auto state = state_.lock()) {
        QueueEndpointRebind(state);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DefaultDeviceNotificationCallback::OnDefaultDeviceChanged(
    EDataFlow flow,
    ERole role,
    LPCWSTR) {
    AudioCallbackInFlightGuard guard;
    if (flow == eRender &&
        (role == eMultimedia || role == eConsole)) {
        if (auto state = state_.lock()) {
            QueueEndpointRebind(state);
        }
    }
    return S_OK;
}

bool GetVariantByte(const VARIANT& value, int* result) {
    if (!result) {
        return false;
    }

    switch (value.vt) {
        case VT_UI1:
            *result = value.bVal;
            return true;
        case VT_I1:
            *result = static_cast<unsigned char>(value.cVal);
            return true;
        case VT_UI2:
            *result = value.uiVal;
            return true;
        case VT_I2:
            *result = value.iVal;
            return true;
        case VT_UI4:
            *result = static_cast<int>(value.ulVal);
            return true;
        case VT_I4:
            *result = value.lVal;
            return true;
        default:
            return false;
    }
}

bool GetVariantString(const VARIANT& value, std::wstring* result) {
    if (!result || value.vt != VT_BSTR || !value.bstrVal) {
        return false;
    }

    *result = value.bstrVal;
    return true;
}

HRESULT ConnectBrightnessWmi(
        winrt::com_ptr<IWbemServices>& services) {
    services = nullptr;

    CLSID clsidWbemLocator{};
    HRESULT hr = CLSIDFromString(
        L"{4590F811-1D3A-11D0-891F-00AA004B2E24}",
        &clsidWbemLocator);
    if (FAILED(hr)) {
        return hr;
    }

    winrt::com_ptr<IWbemLocator> locator;
    hr = CoCreateInstance(
        clsidWbemLocator,
        nullptr,
        CLSCTX_INPROC_SERVER,
        __uuidof(IWbemLocator),
        locator.put_void());
    if (FAILED(hr)) {
        return hr;
    }

    BSTR namespaceName = SysAllocString(L"ROOT\\WMI");
    if (!namespaceName) {
        return E_OUTOFMEMORY;
    }

    hr = locator->ConnectServer(
        namespaceName,
        nullptr,
        nullptr,
        nullptr,
        WBEM_FLAG_CONNECT_USE_MAX_WAIT,
        nullptr,
        nullptr,
        services.put());
    SysFreeString(namespaceName);
    if (FAILED(hr)) {
        services = nullptr;
        return hr;
    }

    // LogonUI can already have COM security initialized. Setting the proxy
    // blanket is sufficient for a local ROOT\\WMI call and avoids disturbing
    // process-wide COM security state.
    hr = CoSetProxyBlanket(
        services.get(),
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        nullptr,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE);
    return hr;
}

HRESULT ExecBrightnessWmiQuery(
        IWbemServices* services,
        const wchar_t* query,
        winrt::com_ptr<IEnumWbemClassObject>& enumerator) {
    if (!services || !query) {
        return E_INVALIDARG;
    }

    BSTR language = SysAllocString(L"WQL");
    BSTR queryText = SysAllocString(query);
    if (!language || !queryText) {
        if (language) SysFreeString(language);
        if (queryText) SysFreeString(queryText);
        return E_OUTOFMEMORY;
    }

    HRESULT hr = services->ExecQuery(
        language,
        queryText,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr,
        enumerator.put());

    SysFreeString(queryText);
    SysFreeString(language);
    return hr;
}

struct WmiBrightnessTarget {
    std::wstring instanceName;
    std::wstring methodPath;
    int currentPercent = 0;
};

bool QueryActiveBrightnessTargetsWmi(
        IWbemServices* services,
        std::vector<WmiBrightnessTarget>* targets) {
    if (!services || !targets) {
        return false;
    }

    targets->clear();

    struct BrightnessRow {
        std::wstring instanceName;
        int currentPercent = 0;
    };
    struct MethodRow {
        std::wstring instanceName;
        std::wstring path;
    };

    std::vector<BrightnessRow> brightnessRows;
    std::vector<MethodRow> methodRows;

    winrt::com_ptr<IEnumWbemClassObject> brightnessEnumerator;
    HRESULT hr = ExecBrightnessWmiQuery(
        services,
        L"SELECT InstanceName, CurrentBrightness "
        L"FROM WmiMonitorBrightness WHERE Active=TRUE",
        brightnessEnumerator);
    if (FAILED(hr) || !brightnessEnumerator) {
        ModLog(L"Battery brightness WMI query failed: 0x%08X",
               static_cast<unsigned>(hr));
        return false;
    }

    for (;;) {
        winrt::com_ptr<IWbemClassObject> object;
        ULONG returned = 0;
        hr = brightnessEnumerator->Next(2000, 1, object.put(), &returned);
        if (FAILED(hr)) {
            ModLog(L"Battery brightness WMI enumeration failed: 0x%08X",
                   static_cast<unsigned>(hr));
            return false;
        }
        if (returned == 0 || !object) {
            break;
        }

        VARIANT instanceName{};
        VARIANT currentBrightness{};
        VariantInit(&instanceName);
        VariantInit(&currentBrightness);

        const HRESULT instanceHr =
            object->Get(L"InstanceName", 0, &instanceName, nullptr, nullptr);
        const HRESULT brightnessHr =
            object->Get(L"CurrentBrightness", 0, &currentBrightness, nullptr, nullptr);

        std::wstring name;
        int percent = 0;
        const bool ok =
            SUCCEEDED(instanceHr) &&
            SUCCEEDED(brightnessHr) &&
            GetVariantString(instanceName, &name) &&
            GetVariantByte(currentBrightness, &percent);

        VariantClear(&currentBrightness);
        VariantClear(&instanceName);

        if (!ok) {
            ModLog(L"Battery brightness WMI skipped an active monitor with incomplete InstanceName/CurrentBrightness data");
            continue;
        }

        brightnessRows.push_back(
            {std::move(name), std::clamp(percent, 0, 100)});
    }

    if (brightnessRows.empty()) {
        ModLog(L"Battery brightness WMI found no active monitor");
        return false;
    }

    winrt::com_ptr<IEnumWbemClassObject> methodEnumerator;
    hr = ExecBrightnessWmiQuery(
        services,
        L"SELECT * FROM WmiMonitorBrightnessMethods WHERE Active=TRUE",
        methodEnumerator);
    if (FAILED(hr) || !methodEnumerator) {
        ModLog(L"Battery brightness WMI methods query failed: 0x%08X",
               static_cast<unsigned>(hr));
        return false;
    }

    for (;;) {
        winrt::com_ptr<IWbemClassObject> object;
        ULONG returned = 0;
        hr = methodEnumerator->Next(2000, 1, object.put(), &returned);
        if (FAILED(hr)) {
            ModLog(L"Battery brightness WMI methods enumeration failed: 0x%08X",
                   static_cast<unsigned>(hr));
            return false;
        }
        if (returned == 0 || !object) {
            break;
        }

        VARIANT instanceName{};
        VARIANT path{};
        VariantInit(&instanceName);
        VariantInit(&path);

        const HRESULT instanceHr =
            object->Get(L"InstanceName", 0, &instanceName, nullptr, nullptr);
        const HRESULT pathHr =
            object->Get(L"__PATH", 0, &path, nullptr, nullptr);

        std::wstring name;
        std::wstring objectPath;
        const bool ok =
            SUCCEEDED(instanceHr) &&
            SUCCEEDED(pathHr) &&
            GetVariantString(instanceName, &name) &&
            GetVariantString(path, &objectPath);

        VariantClear(&path);
        VariantClear(&instanceName);

        if (!ok) {
            ModLog(L"Battery brightness WMI skipped an active method instance with incomplete InstanceName/__PATH data");
            continue;
        }

        methodRows.push_back(
            {std::move(name), std::move(objectPath)});
    }

    for (const auto& brightness : brightnessRows) {
        const MethodRow* matchingMethod = nullptr;
        for (const auto& method : methodRows) {
            if (_wcsicmp(
                    brightness.instanceName.c_str(),
                    method.instanceName.c_str()) == 0) {
                matchingMethod = &method;
                break;
            }
        }

        // Some firmware exposes a single usable pair but formats InstanceName
        // slightly differently between the value/method classes. Preserve the
        // historical single-panel behavior in that unambiguous case.
        if (!matchingMethod &&
            brightnessRows.size() == 1 &&
            methodRows.size() == 1) {
            matchingMethod = &methodRows.front();
            ModLog(L"Battery brightness WMI single-monitor InstanceName mismatch; using the only active method instance");
        }

        if (!matchingMethod) {
            ModLog(L"Battery brightness WMI has no method instance matching monitor=%s",
                   brightness.instanceName.c_str());
            continue;
        }

        targets->push_back({
            brightness.instanceName,
            matchingMethod->path,
            brightness.currentPercent,
        });
    }

    if (targets->empty()) {
        ModLog(L"Battery brightness WMI found active monitors but no usable value/method InstanceName pairs");
        return false;
    }

    ModLog(L"Battery brightness WMI paired %u active monitor(s) by InstanceName",
           static_cast<unsigned>(targets->size()));
    return true;
}

bool SetBrightnessWmiTarget(
        IWbemServices* services,
        const std::wstring& methodPath,
        int targetPercent) {
    if (!services || methodPath.empty()) {
        return false;
    }

    BSTR className =
        SysAllocString(L"WmiMonitorBrightnessMethods");
    if (!className) {
        return false;
    }

    winrt::com_ptr<IWbemClassObject> classObject;
    HRESULT hr = services->GetObject(
        className,
        0,
        nullptr,
        classObject.put(),
        nullptr);
    SysFreeString(className);
    if (FAILED(hr) || !classObject) {
        ModLog(L"Battery brightness WMI class lookup failed: 0x%08X",
               static_cast<unsigned>(hr));
        return false;
    }

    winrt::com_ptr<IWbemClassObject> inputSignature;
    hr = classObject->GetMethod(
        L"WmiSetBrightness",
        0,
        inputSignature.put(),
        nullptr);
    if (FAILED(hr) || !inputSignature) {
        ModLog(L"Battery brightness WMI GetMethod failed: 0x%08X",
               static_cast<unsigned>(hr));
        return false;
    }

    winrt::com_ptr<IWbemClassObject> inputParameters;
    hr = inputSignature->SpawnInstance(
        0, inputParameters.put());
    if (FAILED(hr) || !inputParameters) {
        ModLog(L"Battery brightness WMI SpawnInstance failed: 0x%08X",
               static_cast<unsigned>(hr));
        return false;
    }

    VARIANT timeout{};
    VariantInit(&timeout);
    // WMI Automation represents CIM uint32 as VT_I4.
    timeout.vt = VT_I4;
    timeout.lVal = 0;

    VARIANT brightness{};
    VariantInit(&brightness);
    // CIM uint8 maps directly to VT_UI1.
    brightness.vt = VT_UI1;
    brightness.bVal =
        static_cast<UCHAR>(std::clamp(targetPercent, 0, 100));

    hr = inputParameters->Put(
        L"Timeout", 0, &timeout, 0);
    if (FAILED(hr)) {
        ModLog(L"Battery brightness WMI Timeout parameter setup failed: 0x%08X (vt=VT_I4)",
               static_cast<unsigned>(hr));
        return false;
    }

    hr = inputParameters->Put(
        L"Brightness", 0, &brightness, 0);
    if (FAILED(hr)) {
        ModLog(L"Battery brightness WMI Brightness parameter setup failed: 0x%08X (vt=VT_UI1 target=%d%%)",
               static_cast<unsigned>(hr),
               std::clamp(targetPercent, 0, 100));
        return false;
    }

    BSTR objectPath = SysAllocString(methodPath.c_str());
    BSTR methodName = SysAllocString(L"WmiSetBrightness");
    if (!objectPath || !methodName) {
        if (objectPath) SysFreeString(objectPath);
        if (methodName) SysFreeString(methodName);
        return false;
    }

    winrt::com_ptr<IWbemClassObject> outputParameters;
    hr = services->ExecMethod(
        objectPath,
        methodName,
        0,
        nullptr,
        inputParameters.get(),
        outputParameters.put(),
        nullptr);
    SysFreeString(methodName);
    SysFreeString(objectPath);

    if (FAILED(hr)) {
        ModLog(L"Battery brightness WMI ExecMethod failed: 0x%08X",
               static_cast<unsigned>(hr));
        return false;
    }

    if (outputParameters) {
        VARIANT returnValue{};
        VariantInit(&returnValue);
        if (SUCCEEDED(outputParameters->Get(
                L"ReturnValue",
                0,
                &returnValue,
                nullptr,
                nullptr))) {
            int methodResult = 0;
            if (GetVariantByte(returnValue, &methodResult) &&
                methodResult != 0) {
                ModLog(L"Battery brightness WMI method returned %d",
                       methodResult);
                VariantClear(&returnValue);
                return false;
            }
        }
        VariantClear(&returnValue);
    }

    return true;
}

bool AdjustBrightnessLegacyIoctl(int percentDelta) {
    HANDLE lcd = CreateFileW(
        L"\\\\.\\LCD",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (lcd == INVALID_HANDLE_VALUE) {
        ModLog(L"Battery brightness legacy backend unavailable error=%lu",
               GetLastError());
        return false;
    }

    DisplayBrightnessIoctl current{};
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(
            lcd,
            IOCTL_VIDEO_QUERY_DISPLAY_BRIGHTNESS,
            nullptr,
            0,
            &current,
            sizeof(current),
            &bytesReturned,
            nullptr)) {
        ModLog(L"Battery brightness legacy query failed error=%lu",
               GetLastError());
        CloseHandle(lcd);
        return false;
    }

    SYSTEM_POWER_STATUS powerStatus{};
    const bool havePowerStatus =
        GetSystemPowerStatus(&powerStatus) != FALSE;
    const bool onAc =
        havePowerStatus && powerStatus.ACLineStatus == 1;
    const bool onBattery =
        havePowerStatus && powerStatus.ACLineStatus == 0;

    int currentPercent =
        onAc ? current.acBrightness :
        onBattery ? current.dcBrightness :
        current.displayPolicy == kDisplayPolicyDc
            ? current.dcBrightness
            : current.acBrightness;
    currentPercent = std::clamp(currentPercent, 0, 100);

    const int desiredPercent = std::clamp(
        currentPercent + percentDelta, 0, 100);

    UCHAR supportedLevels[256]{};
    DWORD supportedBytes = 0;
    int targetPercent = desiredPercent;
    if (DeviceIoControl(
            lcd,
            IOCTL_VIDEO_QUERY_SUPPORTED_BRIGHTNESS,
            nullptr,
            0,
            supportedLevels,
            sizeof(supportedLevels),
            &supportedBytes,
            nullptr) &&
        supportedBytes > 0) {
        int best = static_cast<int>(supportedLevels[0]);
        int bestDistance = std::abs(best - desiredPercent);
        const DWORD count =
            std::min<DWORD>(
                supportedBytes,
                ARRAYSIZE(supportedLevels));
        for (DWORD i = 1; i < count; ++i) {
            const int level =
                static_cast<int>(supportedLevels[i]);
            const int distance =
                std::abs(level - desiredPercent);
            if (distance < bestDistance) {
                best = level;
                bestDistance = distance;
            }
        }
        targetPercent = std::clamp(best, 0, 100);
    }

    DisplayBrightnessIoctl requested = current;
    if (onAc) {
        requested.displayPolicy = kDisplayPolicyAc;
        requested.acBrightness =
            static_cast<UCHAR>(targetPercent);
    } else if (onBattery) {
        requested.displayPolicy = kDisplayPolicyDc;
        requested.dcBrightness =
            static_cast<UCHAR>(targetPercent);
    } else {
        requested.displayPolicy = kDisplayPolicyBoth;
        requested.acBrightness =
            static_cast<UCHAR>(targetPercent);
        requested.dcBrightness =
            static_cast<UCHAR>(targetPercent);
    }

    bytesReturned = 0;
    const BOOL ok = DeviceIoControl(
        lcd,
        IOCTL_VIDEO_SET_DISPLAY_BRIGHTNESS,
        &requested,
        sizeof(requested),
        nullptr,
        0,
        &bytesReturned,
        nullptr);
    const DWORD error =
        ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(lcd);

    if (!ok) {
        ModLog(L"Battery brightness legacy set %d%% failed error=%lu",
               targetPercent,
               error);
        return false;
    }

    ModLog(L"Battery brightness legacy set: current=%d%% target=%d%%",
           currentPercent,
           targetPercent);
    return true;
}

DWORD WINAPI BrightnessWorkerThreadProc(void*) {
    HRESULT comResult =
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitializeCom = SUCCEEDED(comResult);

    winrt::com_ptr<IWbemServices> services;
    HRESULT wmiResult = ConnectBrightnessWmi(services);
    if (SUCCEEDED(wmiResult) && services) {
        ModLog(L"Battery brightness worker: WMI backend ready");
    } else {
        ModLog(L"Battery brightness worker: WMI backend unavailable 0x%08X; legacy backend remains available",
               static_cast<unsigned>(wmiResult));
        services = nullptr;
    }

    std::vector<WmiBrightnessTarget> desiredBrightnessTargets;
    ULONGLONG lastDesiredUpdateTick = 0;
    constexpr ULONGLONG kBrightnessCacheIdleResyncMs = 700;

    while (!g_brightnessWorkerStop.load(
                std::memory_order_acquire)) {
        HANDLE eventHandle = nullptr;
        AcquireSRWLockShared(&g_brightnessWorkerLock);
        eventHandle = g_brightnessWorkerEvent;
        ReleaseSRWLockShared(&g_brightnessWorkerLock);

        if (!eventHandle) {
            break;
        }

        const DWORD wait =
            WaitForSingleObject(eventHandle, INFINITE);
        if (wait != WAIT_OBJECT_0) {
            break;
        }

        if (g_brightnessWorkerStop.load(
                std::memory_order_acquire)) {
            break;
        }

        const int percentDelta =
            g_brightnessPendingPercent.exchange(
                0, std::memory_order_acq_rel);
        if (percentDelta == 0) {
            continue;
        }

        bool applied = false;
        if (services) {
            const ULONGLONG now = GetTickCount64();
            const bool cacheExpired =
                desiredBrightnessTargets.empty() ||
                lastDesiredUpdateTick == 0 ||
                now - lastDesiredUpdateTick >
                    kBrightnessCacheIdleResyncMs;

            if (cacheExpired) {
                std::vector<WmiBrightnessTarget> refreshedTargets;
                if (QueryActiveBrightnessTargetsWmi(
                        services.get(), &refreshedTargets)) {
                    desiredBrightnessTargets =
                        std::move(refreshedTargets);
                    lastDesiredUpdateTick = now;
                    ModLog(L"Battery brightness WMI desired-value cache resynced for %u monitor(s)",
                           static_cast<unsigned>(
                               desiredBrightnessTargets.size()));
                } else {
                    desiredBrightnessTargets.clear();
                    lastDesiredUpdateTick = 0;
                }
            }

            bool anyWmiApplied = false;
            bool anyWmiFailed = false;
            for (auto& target : desiredBrightnessTargets) {
                const int basePercent =
                    std::clamp(target.currentPercent, 0, 100);
                const int targetPercent = std::clamp(
                    basePercent + percentDelta,
                    0,
                    100);

                if (targetPercent == basePercent) {
                    anyWmiApplied = true;
                    continue;
                }

                if (SetBrightnessWmiTarget(
                        services.get(),
                        target.methodPath,
                        targetPercent)) {
                    target.currentPercent = targetPercent;
                    anyWmiApplied = true;
                    ModLog(L"Battery brightness WMI set: monitor=%s delta=%+d%% base=%d%% target=%d%% cachedDesired=1",
                           target.instanceName.c_str(),
                           percentDelta,
                           basePercent,
                           targetPercent);
                } else {
                    anyWmiFailed = true;
                    ModLog(L"Battery brightness WMI set failed for monitor=%s",
                           target.instanceName.c_str());
                }
            }

            if (anyWmiApplied) {
                applied = true;
                lastDesiredUpdateTick =
                    anyWmiFailed ? 0 : now;
            } else if (anyWmiFailed) {
                desiredBrightnessTargets.clear();
                lastDesiredUpdateTick = 0;
            }
        }

        if (!applied) {
            applied = AdjustBrightnessLegacyIoctl(percentDelta);
            if (applied) {
                desiredBrightnessTargets.clear();
                lastDesiredUpdateTick = 0;
            }
        }

        if (!applied) {
            desiredBrightnessTargets.clear();
            lastDesiredUpdateTick = 0;
            ModLog(L"Battery brightness adjustment failed on all available backends");
        }
    }

    services = nullptr;
    if (uninitializeCom) {
        CoUninitialize();
    }
    return 0;
}

bool EnsureBrightnessWorkerStarted() {
    AcquireSRWLockExclusive(&g_brightnessWorkerLock);

    if (g_brightnessWorkerThread &&
        g_brightnessWorkerEvent) {
        ReleaseSRWLockExclusive(&g_brightnessWorkerLock);
        return true;
    }

    g_brightnessWorkerStop.store(
        false, std::memory_order_release);

    HANDLE eventHandle = CreateEventW(
        nullptr, FALSE, FALSE, nullptr);
    if (!eventHandle) {
        const DWORD error = GetLastError();
        ReleaseSRWLockExclusive(&g_brightnessWorkerLock);
        ModLog(L"Couldn't create Battery brightness worker event error=%lu",
               error);
        return false;
    }

    HANDLE threadHandle = CreateThread(
        nullptr,
        0,
        BrightnessWorkerThreadProc,
        nullptr,
        0,
        nullptr);
    if (!threadHandle) {
        const DWORD error = GetLastError();
        CloseHandle(eventHandle);
        ReleaseSRWLockExclusive(&g_brightnessWorkerLock);
        ModLog(L"Couldn't create Battery brightness worker thread error=%lu",
               error);
        return false;
    }

    g_brightnessWorkerEvent = eventHandle;
    g_brightnessWorkerThread = threadHandle;

    ReleaseSRWLockExclusive(&g_brightnessWorkerLock);

    ModLog(L"Battery brightness worker started (event-driven; WMI preferred, legacy fallback)");
    return true;
}

void StopBrightnessWorker() {
    HANDLE eventHandle = nullptr;
    HANDLE threadHandle = nullptr;

    AcquireSRWLockExclusive(&g_brightnessWorkerLock);
    g_brightnessWorkerStop.store(
        true, std::memory_order_release);
    eventHandle = g_brightnessWorkerEvent;
    threadHandle = g_brightnessWorkerThread;
    ReleaseSRWLockExclusive(&g_brightnessWorkerLock);

    if (eventHandle) {
        SetEvent(eventHandle);
    }
    if (threadHandle) {
        // Hot-unload must never abandon a worker whose instruction pointer can
        // still be inside this DLL. WMI calls are bounded in normal operation,
        // so wait for the worker to actually exit rather than timing out and
        // closing only our handle while the thread keeps running.
        WaitForSingleObject(threadHandle, INFINITE);
    }

    AcquireSRWLockExclusive(&g_brightnessWorkerLock);
    if (g_brightnessWorkerThread) {
        CloseHandle(g_brightnessWorkerThread);
        g_brightnessWorkerThread = nullptr;
    }
    if (g_brightnessWorkerEvent) {
        CloseHandle(g_brightnessWorkerEvent);
        g_brightnessWorkerEvent = nullptr;
    }
    g_brightnessPendingPercent.store(
        0, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_brightnessWorkerLock);
}

bool QueueBrightnessAdjustment(
        int percentDelta,
        int logicalSteps,
        int effectiveDelta,
        int remainder,
        int deltaPerStep,
        bool highResolution,
        Input::PointerRoutedEventArgs const& args) {
    if (g_batteryWheelAction.load(
            std::memory_order_relaxed) !=
            BatteryWheelAction::AdjustBrightness ||
        percentDelta == 0 ||
        logicalSteps == 0) {
        return false;
    }

    if (!EnsureBrightnessWorkerStarted()) {
        return false;
    }

    g_brightnessPendingPercent.fetch_add(
        percentDelta, std::memory_order_acq_rel);

    HANDLE eventHandle = nullptr;
    AcquireSRWLockShared(&g_brightnessWorkerLock);
    eventHandle = g_brightnessWorkerEvent;
    ReleaseSRWLockShared(&g_brightnessWorkerLock);

    if (!eventHandle || !SetEvent(eventHandle)) {
        return false;
    }

    args.Handled(true);
    ModLog(L"Battery wheel/brightness queued: source=%s delta=%d logicalSteps=%d step=%d%% deltaPerStep=%d remainder=%d pendingDelta=%+d%%",
           highResolution ? L"high-resolution" : L"detent",
           effectiveDelta,
           logicalSteps,
           std::abs(percentDelta / logicalSteps),
           deltaPerStep,
           remainder,
           g_brightnessPendingPercent.load(
               std::memory_order_relaxed));
    return true;
}


struct BatteryWheelRegistration {
    winrt::weak_ref<Controls::Button> button;
    winrt::Windows::Foundation::IInspectable handler{nullptr};
    bool attached = false;
};

void DetachBatteryBrightnessWheelHandler(
        std::shared_ptr<BatteryWheelRegistration> const& registration) {
    if (!registration || !registration->attached) {
        return;
    }

    auto button = registration->button.get();
    if (button && registration->handler) {
        try {
            button.RemoveHandler(
                UIElement::PointerWheelChangedEvent(),
                registration->handler);
        } catch (...) {
        }
    }

    registration->handler = nullptr;
    registration->attached = false;
}

std::shared_ptr<BatteryWheelRegistration>
AttachBatteryBrightnessWheelHandler(
        Controls::Button const& batteryButton) {
    if (!batteryButton) {
        return {};
    }

    try {
        auto weakButton = winrt::make_weak(batteryButton);
        auto wheelAccumulator =
            std::make_shared<WheelDeltaAccumulator>();

        Input::PointerEventHandler handler{
            [weakButton, wheelAccumulator](
                winrt::Windows::Foundation::IInspectable const&,
                Input::PointerRoutedEventArgs const& args) {
                if (g_unloading.load(std::memory_order_acquire) ||
                    g_batteryWheelAction.load(std::memory_order_relaxed) !=
                        BatteryWheelAction::AdjustBrightness) {
                    return;
                }

                auto button = weakButton.get();
                if (!button || !wheelAccumulator) {
                    return;
                }

                WheelInputResult wheel{};
                if (!TryConsumeVerticalWheelInput(
                        button,
                        args,
                        g_batteryWheelReverse.load(
                            std::memory_order_relaxed),
                        *wheelAccumulator,
                        &wheel)) {
                    return;
                }

                if (wheel.steps == 0) {
                    args.Handled(true);
                    return;
                }

                const int stepPercent =
                    wheel.highResolution
                        ? g_precisionScrollStepPercent.load(
                              std::memory_order_relaxed)
                        : g_batteryWheelStepPercent.load(
                              std::memory_order_relaxed);
                QueueBrightnessAdjustment(
                    wheel.steps * stepPercent,
                    wheel.steps,
                    wheel.effectiveDelta,
                    wheel.remainder,
                    wheel.deltaPerStep,
                    wheel.highResolution,
                    args);
            }};

        auto boxedHandler = winrt::box_value(handler);
        batteryButton.AddHandler(
            UIElement::PointerWheelChangedEvent(),
            boxedHandler,
            true);

        auto registration =
            std::make_shared<BatteryWheelRegistration>();
        registration->button = weakButton;
        registration->handler = boxedHandler;
        registration->attached = true;

        ModLog(L"Battery wheel/brightness routed handler attached (handledEventsToo=1; shared high-resolution classifier/variable-threshold accumulator)");
        return registration;
    } catch (const winrt::hresult_error& e) {
        ModLog(L"Couldn't attach Battery wheel/brightness handler: 0x%08X %s",
               static_cast<unsigned>(e.code().value),
               e.message().c_str());
    } catch (...) {
        ModLog(L"Couldn't attach Battery wheel/brightness handler");
    }

    return {};
}


struct BatteryMiddleClickRegistration {
    winrt::weak_ref<Controls::Button> button;
    winrt::Windows::Foundation::IInspectable handler{nullptr};
    bool attached = false;
};

void DetachBatteryMiddleClickHandler(
        std::shared_ptr<BatteryMiddleClickRegistration> const& registration) {
    if (!registration || !registration->attached) {
        return;
    }

    auto button = registration->button.get();
    if (button && registration->handler) {
        try {
            button.RemoveHandler(
                UIElement::PointerPressedEvent(),
                registration->handler);
        } catch (...) {
        }
    }

    registration->handler = nullptr;
    registration->attached = false;
}

std::shared_ptr<BatteryMiddleClickRegistration>
AttachBatteryMiddleClickHandler(
        Controls::Button const& batteryButton) {
    if (!batteryButton) {
        return {};
    }

    try {
        auto weakButton = winrt::make_weak(batteryButton);
        Input::PointerEventHandler handler{
            [weakButton](
                winrt::Windows::Foundation::IInspectable const&,
                Input::PointerRoutedEventArgs const& args) {
                if (g_unloading.load(std::memory_order_acquire)) {
                    return;
                }

                auto button = weakButton.get();
                if (!button) {
                    return;
                }

                bool middlePressed = false;
                try {
                    auto point = args.GetCurrentPoint(button);
                    middlePressed =
                        point &&
                        point.Properties().IsMiddleButtonPressed();
                } catch (...) {
                    return;
                }

                if (!middlePressed) {
                    return;
                }

                const auto action =
                    g_batteryMiddleClickAction.load(
                        std::memory_order_relaxed);
                if (action == BatteryMiddleClickAction::None) {
                    return;
                }

                args.Handled(true);

                if (action == BatteryMiddleClickAction::OpenFlyout) {
                    ModLog(L"Battery middle-click: opening classic battery flyout");
                    ShowClassicBatteryFlyout(button);
                } else {
                    ModLog(L"Battery middle-click: opening Advanced Power Options");
                    LaunchAdvancedPowerOptionsDirect();
                }
            }};

        auto boxedHandler = winrt::box_value(handler);
        batteryButton.AddHandler(
            UIElement::PointerPressedEvent(),
            boxedHandler,
            true);

        auto registration =
            std::make_shared<BatteryMiddleClickRegistration>();
        registration->button = weakButton;
        registration->handler = boxedHandler;
        registration->attached = true;

        ModLog(L"Battery middle-click routed handler attached (handledEventsToo=1)");
        return registration;
    } catch (const winrt::hresult_error& e) {
        ModLog(L"Couldn't attach Battery middle-click handler: 0x%08X %s",
               static_cast<unsigned>(e.code().value),
               e.message().c_str());
    } catch (...) {
        ModLog(L"Couldn't attach Battery middle-click handler");
    }

    return {};
}

bool VolumeUnmuteDirectionMatches(
        VolumeScrollUnmuteBehavior behavior,
        int direction) {
    switch (behavior) {
        case VolumeScrollUnmuteBehavior::Never:
            return false;
        case VolumeScrollUnmuteBehavior::Increase:
            return direction > 0;
        case VolumeScrollUnmuteBehavior::Decrease:
            return direction < 0;
        case VolumeScrollUnmuteBehavior::Either:
            return direction != 0;
    }
    return false;
}

bool AdjustVolumeFromWheelSteps(
        std::shared_ptr<VolumeMonitorState> const& current,
        int steps,
        int effectiveDelta,
        int remainder,
        int deltaPerStep,
        bool highResolution,
        Input::PointerRoutedEventArgs const& args) {
    if (!current || current->stopping.load() ||
        !current->endpointVolume ||
        g_volumeWheelAction.load(std::memory_order_relaxed) !=
            VolumeWheelAction::Adjust ||
        steps == 0) {
        return false;
    }

    float scalar = 0.0f;
    HRESULT hr =
        current->endpointVolume->GetMasterVolumeLevelScalar(&scalar);
    if (FAILED(hr)) {
        ModLog(L"Volume wheel/touchpad: GetMasterVolumeLevelScalar failed 0x%08X",
               static_cast<unsigned>(hr));
        return false;
    }

    BOOL muted = FALSE;
    hr = current->endpointVolume->GetMute(&muted);
    if (FAILED(hr)) {
        ModLog(L"Volume wheel/touchpad: GetMute failed 0x%08X",
               static_cast<unsigned>(hr));
        return false;
    }

    const int stepPercent =
        highResolution
            ? g_precisionScrollStepPercent.load(
                  std::memory_order_relaxed)
            : g_volumeWheelStepPercent.load(
                  std::memory_order_relaxed);
    const float step =
        static_cast<float>(stepPercent) / 100.0f;
    const float target = std::clamp(
        scalar + (step * static_cast<float>(steps)),
        0.0f,
        1.0f);

    const int direction = steps > 0 ? 1 : -1;
    const auto unmuteBehavior =
        g_volumeScrollUnmuteBehavior.load(
            std::memory_order_relaxed);
    const int unmuteThreshold =
        std::max(
            1,
            g_volumeUnmuteThresholdSteps.load(
                std::memory_order_relaxed));

    bool shouldUnmute = false;
    if (muted) {
        if (VolumeUnmuteDirectionMatches(
                unmuteBehavior, direction)) {
            if (current->mutedUnmuteDirection != direction) {
                current->mutedUnmuteDirection = direction;
                current->mutedUnmuteProgressSteps = 0;
            }

            current->mutedUnmuteProgressSteps += std::abs(steps);
            if (current->mutedUnmuteProgressSteps >=
                unmuteThreshold) {
                shouldUnmute = true;
                current->mutedUnmuteProgressSteps = 0;
                current->mutedUnmuteDirection = 0;
            }
        } else {
            current->mutedUnmuteProgressSteps = 0;
            current->mutedUnmuteDirection = 0;
        }
    } else {
        current->mutedUnmuteProgressSteps = 0;
        current->mutedUnmuteDirection = 0;
    }

    const bool volumeChanged =
        std::fabs(target - scalar) >= 0.0001f;

    if (volumeChanged) {
        hr = current->endpointVolume->SetMasterVolumeLevelScalar(
            target, nullptr);
        if (FAILED(hr)) {
            ModLog(L"Volume wheel/touchpad: SetMasterVolumeLevelScalar failed 0x%08X",
                   static_cast<unsigned>(hr));
            return false;
        }
    }

    const bool targetIsZero = target <= 0.0001f;
    const bool muteAtZero =
        g_volumeMuteAtZero.load(std::memory_order_relaxed);

    // Zero-volume mute wins over auto-unmute. This makes a downward scroll to
    // 0% reliably silent even on endpoints/drivers that still leak audible
    // output at scalar 0.
    if (muteAtZero && targetIsZero) {
        if (!muted) {
            hr = current->endpointVolume->SetMute(TRUE, nullptr);
            if (FAILED(hr)) {
                ModLog(L"Volume wheel/touchpad: SetMute(TRUE at 0%%) failed 0x%08X",
                       static_cast<unsigned>(hr));
                return false;
            }
            muted = TRUE;
        }
    } else if (shouldUnmute && muted) {
        hr = current->endpointVolume->SetMute(FALSE, nullptr);
        if (FAILED(hr)) {
            ModLog(L"Volume wheel/touchpad: SetMute(FALSE after threshold) failed 0x%08X",
                   static_cast<unsigned>(hr));
            return false;
        }
        muted = FALSE;
    }

    args.Handled(true);
    ModLog(
        L"Volume wheel/touchpad: source=%s delta=%d logicalSteps=%d step=%d%% deltaPerStep=%d remainder=%d target=%d%% mute=%d unmuteBehavior=%s progress=%d/%d muteAtZero=%d",
        highResolution ? L"high-resolution" : L"detent",
        effectiveDelta,
        steps,
        stepPercent,
        deltaPerStep,
        remainder,
        static_cast<int>(std::lround(target * 100.0f)),
        muted ? 1 : 0,
        VolumeScrollUnmuteBehaviorName(unmuteBehavior),
        current->mutedUnmuteProgressSteps,
        unmuteThreshold,
        muteAtZero ? 1 : 0);
    return true;
}


bool ToggleVolumeMute(
        std::shared_ptr<VolumeMonitorState> const& current,
        const wchar_t* source) {
    if (!current || current->stopping.load() ||
        !current->endpointVolume) {
        return false;
    }

    BOOL muted = FALSE;
    HRESULT hr = current->endpointVolume->GetMute(&muted);
    if (FAILED(hr)) {
        ModLog(L"%s: GetMute failed 0x%08X",
               source,
               static_cast<unsigned>(hr));
        return false;
    }

    hr = current->endpointVolume->SetMute(!muted, nullptr);
    if (FAILED(hr)) {
        ModLog(L"%s: SetMute(%d) failed 0x%08X",
               source,
               muted ? 0 : 1,
               static_cast<unsigned>(hr));
        return false;
    }

    current->mutedUnmuteProgressSteps = 0;
    current->mutedUnmuteDirection = 0;
    ModLog(L"%s: mute toggled %d -> %d",
           source,
           muted ? 1 : 0,
           muted ? 0 : 1);
    return true;
}

void AttachVolumeMiddleClickHandler(
        std::shared_ptr<VolumeMonitorState> const& state) {
    if (!state || !state->button ||
        state->middleClickHandlerAttached) {
        return;
    }

    std::weak_ptr<VolumeMonitorState> weak = state;
    try {
        Input::PointerEventHandler handler{
            [weak](winrt::Windows::Foundation::IInspectable const&,
                   Input::PointerRoutedEventArgs const& args) {
                auto current = weak.lock();
                if (!current || current->stopping.load() ||
                    g_unloading.load(std::memory_order_acquire) ||
                    g_volumeMiddleClickAction.load(
                        std::memory_order_relaxed) !=
                        VolumeMiddleClickAction::ToggleMute) {
                    return;
                }

                bool middlePressed = false;
                try {
                    auto point =
                        args.GetCurrentPoint(current->button);
                    middlePressed =
                        point &&
                        point.Properties().IsMiddleButtonPressed();
                } catch (...) {
                    return;
                }

                if (!middlePressed) {
                    return;
                }

                if (ToggleVolumeMute(
                        current,
                        L"Volume middle-click")) {
                    args.Handled(true);
                }
            }};

        auto boxedHandler = winrt::box_value(handler);
        state->button.AddHandler(
            UIElement::PointerPressedEvent(),
            boxedHandler,
            true);

        state->middleClickHandler = boxedHandler;
        state->middleClickHandlerAttached = true;
        ModLog(L"Volume middle-click routed handler attached (EarTrumpet-style toggle mute)");
    } catch (const winrt::hresult_error& e) {
        ModLog(L"Couldn't attach Volume middle-click handler: 0x%08X %s",
               static_cast<unsigned>(e.code().value),
               e.message().c_str());
    } catch (...) {
        ModLog(L"Couldn't attach Volume middle-click handler");
    }
}

void AttachVolumeWheelHandler(
        std::shared_ptr<VolumeMonitorState> const& state) {
    if (!state || !state->button || state->wheelHandlerAttached) {
        return;
    }

    std::weak_ptr<VolumeMonitorState> weak = state;
    try {
        Input::PointerEventHandler handler{
            [weak](winrt::Windows::Foundation::IInspectable const&,
                   Input::PointerRoutedEventArgs const& args) {
                auto current = weak.lock();
                if (!current || current->stopping.load() ||
                    !current->endpointVolume ||
                    g_volumeWheelAction.load(std::memory_order_relaxed) !=
                        VolumeWheelAction::Adjust) {
                    return;
                }

                WheelInputResult wheel{};
                if (!TryConsumeVerticalWheelInput(
                        current->button,
                        args,
                        g_volumeWheelReverse.load(
                            std::memory_order_relaxed),
                        current->wheelAccumulator,
                        &wheel)) {
                    return;
                }

                if (wheel.steps == 0) {
                    args.Handled(true);
                    return;
                }

                AdjustVolumeFromWheelSteps(
                    current,
                    wheel.steps,
                    wheel.effectiveDelta,
                    wheel.remainder,
                    wheel.deltaPerStep,
                    wheel.highResolution,
                    args);
            }};

        auto boxedHandler = winrt::box_value(handler);

        // PointerWheelChanged is routed. Using AddHandler with
        // handledEventsToo=true makes this work even if Button/template code
        // or another XAML ancestor has already marked the touchpad/wheel event
        // handled before ordinary user handlers would see it.
        state->button.AddHandler(
            UIElement::PointerWheelChangedEvent(),
            boxedHandler,
            true);

        state->wheelHandler = boxedHandler;
        state->wheelHandlerAttached = true;
        ModLog(L"Volume wheel/touchpad routed handler attached (handledEventsToo=1; shared high-resolution classifier/variable-threshold accumulator; action is runtime-configurable)");
    } catch (const winrt::hresult_error& e) {
        ModLog(L"Couldn't attach Volume wheel/touchpad routed handler: 0x%08X %s",
               static_cast<unsigned>(e.code().value),
               e.message().c_str());
    } catch (...) {
        ModLog(L"Couldn't attach Volume wheel/touchpad routed handler");
    }
}

[[maybe_unused]] void StopVolumeMonitor(std::shared_ptr<VolumeMonitorState> const& state) {
    if (!state || state->cleanupDone.exchange(true)) {
        return;
    }
    state->stopping.store(true, std::memory_order_release);

    if (state->endpointVolume && state->endpointCallback) {
        state->endpointVolume->UnregisterControlChangeNotify(
            state->endpointCallback.Get());
    }
    if (state->enumerator && state->deviceCallback) {
        state->enumerator->UnregisterEndpointNotificationCallback(
            state->deviceCallback.Get());
    }

    // During hot-disable, keep our own COM references alive until callbacks
    // which were already inside the notification objects have returned. This
    // prevents the final Release from deleting a callback object underneath an
    // in-flight method after the registration itself has been removed.
    if (g_unloading.load(std::memory_order_acquire)) {
        while (g_audioCallbacksInFlight.load(std::memory_order_acquire) != 0) {
            Sleep(1);
        }
    }

    state->endpointVolume.Reset();
    state->endpointCallback.Reset();
    state->deviceCallback.Reset();
    state->enumerator.Reset();

    if (state->legacyUiWindow) {
        HWND hwnd = state->legacyUiWindow;
        state->legacyUiWindow = nullptr;
        if (GetCurrentThreadId() == state->uiThreadId) {
            DestroyWindow(hwnd);
        } else if (g_unloading.load(std::memory_order_acquire)) {
            DWORD_PTR ignored = 0;
            SendMessageTimeoutW(
                hwnd, WM_CLOSE, 0, 0,
                SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &ignored);
        } else {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
    }
    UnregisterPrivateProcessWindowClass(
        kLegacyVolumeUiWindowClass, LegacyVolumeUiWndProc);

    if (state->button && state->middleClickHandlerAttached &&
        state->middleClickHandler) {
        try {
            state->button.RemoveHandler(
                UIElement::PointerPressedEvent(),
                state->middleClickHandler);
        } catch (...) {
        }
        state->middleClickHandler = nullptr;
        state->middleClickHandlerAttached = false;
    }

    if (state->button && state->wheelHandlerAttached &&
        state->wheelHandler) {
        try {
            state->button.RemoveHandler(
                UIElement::PointerWheelChangedEvent(),
                state->wheelHandler);
        } catch (...) {
        }
        state->wheelHandler = nullptr;
        state->wheelHandlerAttached = false;
    }

    state->icon = nullptr;
    state->percentText = nullptr;
    state->button = nullptr;
    state->toolTip = nullptr;
    state->endpointName = {};
    state->endpointId.clear();
    state->nativeSndVol = {};
    state->dispatcherQueue = nullptr;
    state->coreDispatcher = nullptr;
}

std::shared_ptr<VolumeMonitorState> StartVolumeMonitor(Controls::UserControl const& owner,
                        Controls::Button const& volumeButton,
                        Controls::TextBlock const& volumeIcon,
                        Controls::TextBlock const& volumePercentText,
                        Controls::ToolTip const& volumeToolTip,
                        wchar_t noEndpointGlyph,
                        NativeSndVolVisuals const& nativeSndVol) {
    std::shared_ptr<VolumeMonitorState> state;
    try {
        state = std::make_shared<VolumeMonitorState>();
        state->icon = volumeIcon;
        state->percentText = volumePercentText;
        state->button = volumeButton;
        state->toolTip = volumeToolTip;
        state->noEndpointGlyph = noEndpointGlyph;
        state->nativeSndVol = nativeSndVol;
        state->uiThreadId = GetCurrentThreadId();
        state->requestedHandoff = g_volumeUiHandoff.load();

        // Probe capabilities on the actual LogonUI XAML UI thread. Automatic
        // is intentionally capability-based, never keyed to an OS build.
        state->dispatcherQueue = System::DispatcherQueue::GetForCurrentThread();
        state->coreDispatcher = owner.Dispatcher();

        if (state->requestedHandoff == VolumeUiHandoffMode::Automatic) {
            if (state->dispatcherQueue) {
                state->activeHandoff = VolumeUiHandoffMode::DispatcherQueue;
            } else if (state->coreDispatcher) {
                state->activeHandoff = VolumeUiHandoffMode::CoreDispatcher;
            } else {
                state->activeHandoff = VolumeUiHandoffMode::LegacyMessageWindow;
            }
        } else {
            state->activeHandoff = state->requestedHandoff;
        }

        if (state->activeHandoff == VolumeUiHandoffMode::LegacyMessageWindow) {
            state->legacyUiWindow = CreateLegacyVolumeUiWindow(state);
        }

        AttachVolumeWheelHandler(state);
        AttachVolumeMiddleClickHandler(state);

        ModLog(L"Volume UI handoff: requested=%s active=%s thread=%u DispatcherQueue=%d CoreDispatcher=%d LegacyMessageWindow=%d",
               VolumeUiHandoffModeName(state->requestedHandoff),
               VolumeUiHandoffModeName(state->activeHandoff),
               state->uiThreadId,
               state->dispatcherQueue ? 1 : 0,
               state->coreDispatcher ? 1 : 0,
               state->legacyUiWindow ? 1 : 0);

        if (state->activeHandoff == VolumeUiHandoffMode::DispatcherQueue &&
            !state->dispatcherQueue) {
            ModLog(L"WARNING: DispatcherQueue was explicitly selected but no queue is associated with the LogonUI UI thread; initial state works, live cross-thread updates will be dropped");
        } else if (state->activeHandoff == VolumeUiHandoffMode::CoreDispatcher &&
                   !state->coreDispatcher) {
            ModLog(L"WARNING: CoreDispatcher was explicitly selected but unavailable on this LogonUI host; initial state works, live cross-thread updates will be dropped");
        } else if (state->activeHandoff == VolumeUiHandoffMode::LegacyMessageWindow &&
                   !state->legacyUiWindow) {
            ModLog(L"WARNING: legacy message-window handoff was selected but the bridge window couldn't be created; initial state works, live cross-thread updates will be dropped");
        }

        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&state->enumerator));
        if (FAILED(hr)) {
            ModLog(L"Couldn't create audio endpoint enumerator: 0x%08X",
                   static_cast<unsigned>(hr));
            return state;
        }

        auto* endpointCallback = new (std::nothrow) EndpointVolumeCallback(
            std::weak_ptr<VolumeMonitorState>(state));
        auto* deviceCallback = new (std::nothrow) DefaultDeviceNotificationCallback(
            std::weak_ptr<VolumeMonitorState>(state));
        if (!endpointCallback || !deviceCallback) {
            if (endpointCallback) {
                endpointCallback->Release();
            }
            if (deviceCallback) {
                deviceCallback->Release();
            }
            ModLog(L"Couldn't allocate live Volume callbacks");
            return state;
        }
        state->endpointCallback.Attach(endpointCallback);
        state->deviceCallback.Attach(deviceCallback);

        hr = state->enumerator->RegisterEndpointNotificationCallback(
            state->deviceCallback.Get());
        if (FAILED(hr)) {
            ModLog(L"Couldn't register default-device notification: 0x%08X",
                   static_cast<unsigned>(hr));
            return state;
        }

        hr = RebindDefaultPlaybackEndpoint(state);
        if (FAILED(hr)) {
            if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
                ModLog(L"No default playback endpoint yet (0x%08X); showing native disabled-audio state and remaining armed for device arrival",
                       static_cast<unsigned>(hr));
                ModLog(L"Live Volume watcher started (event-driven; waiting for endpoint; no polling)");
                return state;
            }

            state->enumerator->UnregisterEndpointNotificationCallback(
                state->deviceCallback.Get());
            state->stopping.store(true);
            ModLog(L"Couldn't bind live Volume endpoint: 0x%08X",
                   static_cast<unsigned>(hr));
            return state;
        }

        ModLog(L"Live Volume watcher started (event-driven; no polling)");
        return state;
    } catch (const winrt::hresult_error& e) {
        ModLog(L"Couldn't start live Volume watcher: 0x%08X %s",
               static_cast<unsigned>(e.code().value),
               e.message().c_str());
    } catch (...) {
        ModLog(L"Couldn't start live Volume watcher: unknown exception");
    }
    return state;
}

bool GetOsBuild(DWORD* build, DWORD* ubr) {
    *build = 0;
    *ubr = 0;

    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return false;
    }

    using RtlGetVersion_t = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    auto rtlGetVersion = reinterpret_cast<RtlGetVersion_t>(
        GetProcAddress(ntdll, "RtlGetVersion"));
    if (!rtlGetVersion) {
        return false;
    }

    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (rtlGetVersion(&version) != 0) {
        return false;
    }

    DWORD value = 0;
    DWORD valueSize = sizeof(value);
    if (RegGetValueW(HKEY_LOCAL_MACHINE,
                     L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                     L"UBR",
                     RRF_RT_REG_DWORD,
                     nullptr,
                     &value,
                     &valueSize) != ERROR_SUCCESS) {
        // Build logging is diagnostic only now; a missing UBR must not block
        // symbol-based hook discovery.
        value = 0;
    }

    *build = version.dwBuildNumber;
    *ubr = value;
    return true;
}

bool IsOnLoadedSymbolName(PCWSTR symbol) {
    if (!symbol) {
        return false;
    }

    constexpr std::wstring_view needle =
        L"LogonUX::SystemProviderUserControl::_OnLoaded";
    const std::wstring_view value(symbol);
    const size_t pos = value.find(needle);
    if (pos == std::wstring_view::npos) {
        return false;
    }

    // Accept the short DbgHelp form and full undecorated signatures, but not
    // EH helper symbols such as `...::_OnLoaded'::`1'::dtor$...`.
    const size_t after = pos + needle.size();
    if (after == value.size()) {
        return true;
    }

    return value[after] == L'(';
}


bool IsExpectedOnLoadedMemberOffset(DWORD offset) {
    // Harvested/validated SystemProviderUserControl::_OnLoaded generations:
    //   17763: 0x1D0
    //   18362/19041/22000: 0x1E0
    //   22621: 0x1D8 / 0x1E8
    //   26100 / current 26200 builds: 0x1D8 / 0x1E0
    switch (offset) {
        case 0x1D0:
        case 0x1D8:
        case 0x1E0:
        case 0x1E8:
            return true;
        default:
            return false;
    }
}

bool MatchOnLoadedStructuralCandidate(
        const StobjectPeView& view,
        DWORD rva,
        DWORD functionEnd,
        const wchar_t** shapeLabel,
        DWORD* memberOffset) {
    if (!shapeLabel || !memberOffset ||
        functionEnd <= rva ||
        !IsPeRvaRangeValid(view, rva, 64)) {
        return false;
    }

    // All harvested targets are substantial event handlers. This is only a
    // broad sanity bound; the byte/semantic checks below carry the identity.
    const DWORD functionSize = functionEnd - rva;
    if (functionSize < 0x140 || functionSize > 0x500) {
        return false;
    }

    const BYTE* p = view.base + rva;

    // Legacy compiler shape observed throughout Windows 10 1809 and 1903
    // (17763 / 18362). Relative call targets and the class member offset are
    // intentionally not hard-coded.
    static constexpr BYTE kLegacyPrefix[] = {
        0x48, 0x8B, 0xC4,                         // mov rax,rsp
        0x55, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x8B, 0xEC,                         // mov rbp,rsp
        0x48, 0x83, 0xEC, 0x40,                   // sub rsp,40h
        0x48, 0xC7, 0x45, 0xE0, 0xFE, 0xFF, 0xFF, 0xFF,
        0x48, 0x89, 0x58, 0x10,
        0x48, 0x89, 0x70, 0x18
    };

    if (memcmp(p, kLegacyPrefix, sizeof(kLegacyPrefix)) == 0) {
        // mov r14/r15, rcx
        if (p[34] != 0x4C || p[35] != 0x8B ||
            (p[36] != 0xF1 && p[36] != 0xF9) ||
            memcmp(p + 37, "\x83\x65\x30\x00", 4) != 0 ||
            memcmp(p + 41, "\x4C\x8D\xA1", 3) != 0) {
            return false;
        }

        DWORD offset = 0;
        memcpy(&offset, p + 44, sizeof(offset));
        if (!IsExpectedOnLoadedMemberOffset(offset) ||
            memcmp(p + 48, "\x49\x83\x3C\x24\x00", 5) != 0 ||
            p[53] != 0x0F ||
            (p[54] != 0x84 && p[54] != 0x85)) {
            return false;
        }

        *shapeLabel = L"legacy 17763/18362 family";
        *memberOffset = offset;
        return true;
    }

    // Later compiler shape: Windows 10 2004/22H2 and Windows 11 families.
    // 19041 uses a 30h stack frame; 22000+ uses 40h in every harvested sample.
    static constexpr BYTE kModernPrefix[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10,
        0x48, 0x89, 0x74, 0x24, 0x18,
        0x55, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x8B, 0xEC
    };

    if (memcmp(p, kModernPrefix, sizeof(kModernPrefix)) != 0 ||
        p[21] != 0x48 || p[22] != 0x83 || p[23] != 0xEC ||
        (p[24] != 0x30 && p[24] != 0x40) ||
        p[25] != 0x4C || p[26] != 0x8B ||
        (p[27] != 0xF1 && p[27] != 0xF9) ||
        memcmp(p + 28, "\x83\x65\x30\x00", 4) != 0 ||
        memcmp(p + 32, "\x4C\x8D\xA1", 3) != 0) {
        return false;
    }

    DWORD offset = 0;
    memcpy(&offset, p + 35, sizeof(offset));
    if (!IsExpectedOnLoadedMemberOffset(offset) ||
        memcmp(p + 39, "\x49\x83\x3C\x24\x00", 5) != 0 ||
        p[44] != 0x0F ||
        (p[45] != 0x84 && p[45] != 0x85)) {
        return false;
    }

    *shapeLabel =
        p[24] == 0x30
            ? L"modern Win10 19041 family"
            : L"modern Win11 22000+ family";
    *memberOffset = offset;
    return true;
}

void* ResolveOnLoadedByStructure(HMODULE module) {
    StobjectPeView view{};
    if (!GetStobjectPeView(module, &view)) {
        ModLog(L"Symbol-free _OnLoaded resolver: invalid Windows.UI.Logon.dll PE image");
        return nullptr;
    }

    const auto* textSection = FindPeSectionByName(view, ".text");
    if (!textSection ||
        (textSection->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
        ModLog(L"Symbol-free _OnLoaded resolver: executable .text section unavailable");
        return nullptr;
    }

    const DWORD textRva = textSection->VirtualAddress;
    const size_t textSpan = PeSectionVirtualSpan(*textSection);
    if (!textSpan ||
        textSpan > view.nt->OptionalHeader.SizeOfImage ||
        !IsPeRvaRangeValid(view, textRva, textSpan)) {
        ModLog(L"Symbol-free _OnLoaded resolver: invalid .text bounds");
        return nullptr;
    }

    const auto& exceptionDir =
        view.nt->OptionalHeader
            .DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!exceptionDir.VirtualAddress ||
        exceptionDir.Size < sizeof(RUNTIME_FUNCTION) ||
        !IsPeRvaRangeValid(
            view, exceptionDir.VirtualAddress, exceptionDir.Size)) {
        ModLog(L"Symbol-free _OnLoaded resolver: x64 runtime-function table unavailable");
        return nullptr;
    }

    auto functions = reinterpret_cast<const RUNTIME_FUNCTION*>(
        view.base + exceptionDir.VirtualAddress);
    const size_t functionCount =
        exceptionDir.Size / sizeof(RUNTIME_FUNCTION);
    const uint64_t textEnd =
        static_cast<uint64_t>(textRva) + textSpan;

    std::vector<DWORD> candidates;
    const wchar_t* uniqueShape = nullptr;
    DWORD uniqueMemberOffset = 0;

    // Enumerating x64 unwind-function starts is both faster and safer than
    // byte-scanning arbitrary instruction interiors. The harvested target is
    // a real runtime-function start on every validated 17763+ sample.
    for (size_t i = 0; i < functionCount; ++i) {
        const DWORD candidateRva = functions[i].BeginAddress;
        const DWORD functionEnd = functions[i].EndAddress;
        if (candidateRva < textRva ||
            static_cast<uint64_t>(candidateRva) + 64 > textEnd ||
            functionEnd <= candidateRva) {
            continue;
        }

        const BYTE* p = view.base + candidateRva;
        if (p[0] != 0x48) {
            continue;
        }

        const wchar_t* shape = nullptr;
        DWORD memberOffset = 0;
        if (!MatchOnLoadedStructuralCandidate(
                view,
                candidateRva,
                functionEnd,
                &shape,
                &memberOffset)) {
            continue;
        }

        PushUniqueRva(&candidates, candidateRva);
        ModLog(L"Symbol-free _OnLoaded candidate: RVA=0x%X shape=%s memberOffset=0x%X",
               candidateRva,
               shape,
               memberOffset);

        if (candidates.size() == 1) {
            uniqueShape = shape;
            uniqueMemberOffset = memberOffset;
        }
    }

    if (candidates.size() != 1) {
        ModLog(L"Symbol-free _OnLoaded resolver expected exactly one validated candidate, found %u; refusing to hook",
               static_cast<unsigned>(candidates.size()));
        return nullptr;
    }

    const DWORD rva = candidates.front();
    ModLog(L"Symbol-free SystemProviderUserControl::_OnLoaded resolver succeeded: RVA=0x%X shape=%s memberOffset=0x%X",
           rva,
           uniqueShape ? uniqueShape : L"<unknown>",
           uniqueMemberOffset);

    return view.base + rva;
}

void* ResolveOnLoadedBySymbols(HMODULE module) {
    ModLog(L"Resolving SystemProviderUserControl::_OnLoaded from Windows.UI.Logon symbols");

    WH_FIND_SYMBOL findData{};
    HANDLE search = Wh_FindFirstSymbol(module, nullptr, &findData);
    if (!search) {
        ModLog(L"Wh_FindFirstSymbol failed; symbols may be unavailable for this Windows.UI.Logon.dll");
        return nullptr;
    }

    struct SearchGuard {
        HANDLE handle;
        ~SearchGuard() {
            if (handle) {
                Wh_FindCloseSymbol(handle);
            }
        }
    } searchGuard{search};

    void* target = nullptr;
    unsigned rawMatchCount = 0;
    std::vector<void*> distinctTargets;

    do {
        if (!IsOnLoadedSymbolName(findData.symbol)) {
            continue;
        }

        ++rawMatchCount;
        ModLog(L"Symbol candidate: %s at %p",
               findData.symbol ? findData.symbol : L"<null>",
               findData.address);

        if (!findData.address) {
            continue;
        }

        if (std::find(distinctTargets.begin(), distinctTargets.end(),
                      findData.address) == distinctTargets.end()) {
            distinctTargets.push_back(findData.address);
        }
    } while (Wh_FindNextSymbol(search, &findData));

    if (distinctTargets.size() != 1) {
        ModLog(L"Expected exactly one distinct _OnLoaded address, got %u raw symbol match(es) across %u distinct address(es); refusing to hook",
               rawMatchCount,
               static_cast<unsigned>(distinctTargets.size()));
        return nullptr;
    }

    target = distinctTargets.front();
    if (rawMatchCount > 1) {
        ModLog(L"Collapsed %u duplicate _OnLoaded symbol entries to one address %p",
               rawMatchCount, target);
    }

    const auto rva = reinterpret_cast<uintptr_t>(target) -
                     reinterpret_cast<uintptr_t>(module);
    ModLog(L"Resolved SystemProviderUserControl::_OnLoaded RVA=0x%llX",
           static_cast<unsigned long long>(rva));
    return target;
}

FrameworkElement FindByName(FrameworkElement root, const wchar_t* name) {
    if (!root) {
        return nullptr;
    }

    try {
        if (root.Name() == name) {
            return root;
        }

        int count = Media::VisualTreeHelper::GetChildrenCount(root);
        for (int i = 0; i < count; ++i) {
            auto child = Media::VisualTreeHelper::GetChild(root, i)
                             .try_as<FrameworkElement>();
            if (!child) {
                continue;
            }

            auto result = FindByName(child, name);
            if (result) {
                return result;
            }
        }
    } catch (...) {
    }

    return nullptr;
}


Controls::TextBlock FindFirstTextBlock(FrameworkElement root) {
    if (!root) {
        return nullptr;
    }

    try {
        if (auto text = root.try_as<Controls::TextBlock>()) {
            return text;
        }

        int count = Media::VisualTreeHelper::GetChildrenCount(root);
        for (int i = 0; i < count; ++i) {
            auto child = Media::VisualTreeHelper::GetChild(root, i)
                             .try_as<FrameworkElement>();
            if (!child) {
                continue;
            }

            auto result = FindFirstTextBlock(child);
            if (result) {
                return result;
            }
        }
    } catch (...) {
    }

    return nullptr;
}

void CopyNativeGlyphTypography(Controls::TextBlock source,
                               Controls::TextBlock target) {
    if (!source || !target) {
        return;
    }

    try { target.FontFamily(source.FontFamily()); } catch (...) {}
    try { target.FontSize(source.FontSize()); } catch (...) {}
    try { target.FontWeight(source.FontWeight()); } catch (...) {}
    try { target.FontStyle(source.FontStyle()); } catch (...) {}
    try { target.FontStretch(source.FontStretch()); } catch (...) {}
    try { target.CharacterSpacing(source.CharacterSpacing()); } catch (...) {}
    try { target.HorizontalAlignment(source.HorizontalAlignment()); } catch (...) {}
    try { target.VerticalAlignment(source.VerticalAlignment()); } catch (...) {}
    try { target.TextAlignment(source.TextAlignment()); } catch (...) {}
}


void LogTree(FrameworkElement root, int depth = 0, int* budget = nullptr) {
    int localBudget = 140;
    if (!budget) {
        budget = &localBudget;
    }

    if (!root || *budget <= 0 || depth > 10) {
        return;
    }

    --*budget;

    try {
        auto className = winrt::get_class_name(root);
        auto name = root.Name();
        ModLog(L"TREE d=%d class=%s name=%s",
               depth,
               className.c_str(),
               name.empty() ? L"-" : name.c_str());

        int count = Media::VisualTreeHelper::GetChildrenCount(root);
        for (int i = 0; i < count && *budget > 0; ++i) {
            auto child = Media::VisualTreeHelper::GetChild(root, i)
                             .try_as<FrameworkElement>();
            if (child) {
                LogTree(child, depth + 1, budget);
            }
        }
    } catch (...) {
    }
}

void ConfigurePercentageTextBlock(
        Controls::TextBlock const& textBlock) {
    if (!textBlock) {
        return;
    }

    const double fontSize = static_cast<double>(
        g_percentageFontSize.load(std::memory_order_relaxed));
    const auto fontFamily = GetPercentageFontFamilySetting();

    try {
        textBlock.FontFamily(Media::FontFamily(fontFamily));
    } catch (...) {
        try {
            textBlock.FontFamily(Media::FontFamily(L"Segoe UI"));
        } catch (...) {
        }
    }

    try {
        textBlock.FontSize(fontSize);
        textBlock.VerticalAlignment(VerticalAlignment::Center);
        textBlock.TextAlignment(TextAlignment::Center);
        textBlock.MinWidth(0.0);
    } catch (...) {
    }
}

void AllowInjectedButtonToDynamicallySizeForPercentage(
        Controls::Button const& button,
        double nativeButtonWidth) {
    if (!button) {
        return;
    }

    try {
        // Override fixed Width from the native Power button style while
        // preserving its template. The content and Auto Grid column now drive
        // the width continuously, similar to variable-width taskbar buttons.
        button.Width(std::numeric_limits<double>::quiet_NaN());
        button.MinWidth(std::max(0.0, nativeButtonWidth));

        ModLog(L"Percentage layout: dynamic width enabled nativeMinWidth=%.1f",
               nativeButtonWidth);
    } catch (...) {
    }
}

void AllowLazyNativeContainerToDynamicallySize(
        FrameworkElement const& element,
        const std::wstring& key) {
    if (!element || (key != L"ime" && key != L"input")) {
        return;
    }

    try {
        // Windows 11 keeps the IME/Input UserControl containers alive even
        // while their inner buttons are lazy/unrealized. Those containers can
        // retain the native 40-DIP Width even when their content currently
        // contributes no visible button. In an Auto Grid column that becomes
        // a blank slot if the user moves Battery/Volume ahead of IME/Input.
        //
        // Make only the *outer container* content-driven. We deliberately do
        // not touch its Visibility, template, or internal buttons here, so
        // Windows remains in full control of when the native control appears.
        // Its left margin is handled separately and dynamically: a permanent
        // margin would itself reserve blank space while the lazy content is
        // dormant.
        element.Width(std::numeric_limits<double>::quiet_NaN());
        element.MinWidth(0.0);
        element.MaxWidth(std::numeric_limits<double>::infinity());

        ModLog(L"Lazy native container sizing normalized: %s (Width=Auto MinWidth=0)",
               key.c_str());
    } catch (...) {
    }
}


bool IsRenderedSystemStripElement(FrameworkElement const& element) {
    if (!element) {
        return false;
    }

    try {
        return element.Visibility() == Visibility::Visible &&
               element.ActualWidth() > 0.0 &&
               element.ActualHeight() > 0.0;
    } catch (...) {
        return false;
    }
}

bool IsLazyImeOrInputContainer(FrameworkElement const& element) {
    if (!element) {
        return false;
    }

    try {
        const auto className = winrt::get_class_name(element);
        return className == L"LogonUX.ImeModeIndicatorUserControl" ||
               className == L"LogonUX.InputSwitchUserControl";
    } catch (...) {
        return false;
    }
}

void ApplyImeModeGlyphVisualOffsetRecursive(
        FrameworkElement const& root,
        double offsetX,
        int depth = 0) {
    if (!root || depth > 10) {
        return;
    }

    try {
        if (auto text = root.try_as<Controls::TextBlock>()) {
            // Shift only the rendered glyph/content, never the Button or its
            // 40-DIP native hit target. Reuse a TranslateTransform if one was
            // already installed by us; don't overwrite an unrelated native
            // transform if Windows ever adds one.
            auto current = text.RenderTransform();
            auto translate = current.try_as<Media::TranslateTransform>();
            if (!current) {
                translate = Media::TranslateTransform();
                text.RenderTransform(translate);
            } else if (!translate) {
                return;
            }

            if (std::fabs(translate.X() - offsetX) >= 0.01) {
                translate.X(offsetX);
            }
            return;
        }

        const int count = Media::VisualTreeHelper::GetChildrenCount(root);
        for (int i = 0; i < count; ++i) {
            auto child = Media::VisualTreeHelper::GetChild(root, i)
                             .try_as<FrameworkElement>();
            if (child) {
                ApplyImeModeGlyphVisualOffsetRecursive(
                    child, offsetX, depth + 1);
            }
        }
    } catch (...) {
    }
}

void UpdateImeModeGlyphVisualOffset(FrameworkElement const& element) {
    if (!element) {
        return;
    }

    try {
        if (winrt::get_class_name(element) !=
            L"LogonUX.ImeModeIndicatorUserControl") {
            return;
        }

        ApplyImeModeGlyphVisualOffsetRecursive(
            element,
            static_cast<double>(
                g_imeModeVisualOffsetX.load(std::memory_order_relaxed)));
    } catch (...) {
    }
}

void UpdateLazyNativeContainerSpacing(
        Controls::Grid const& parentGrid,
        int firstSystemColumn,
        int usedEndColumn,
        double nativeLeftSpacing) {
    if (!parentGrid || usedEndColumn <= firstSystemColumn) {
        return;
    }

    try {
        auto children = parentGrid.Children();
        std::vector<FrameworkElement> directElements;
        directElements.reserve(children.Size());

        for (uint32_t i = 0; i < children.Size(); ++i) {
            auto element = children.GetAt(i).try_as<FrameworkElement>();
            if (!element) {
                continue;
            }

            const int col = Controls::Grid::GetColumn(element);
            if (col < firstSystemColumn || col >= usedEndColumn) {
                continue;
            }

            directElements.push_back(element);
        }

        for (const auto& element : directElements) {
            if (!IsLazyImeOrInputContainer(element)) {
                continue;
            }

            const int col = Controls::Grid::GetColumn(element);
            const bool rendered = IsRenderedSystemStripElement(element);
            FrameworkElement nearestRenderedPredecessor{nullptr};
            int nearestPredecessorColumn = INT_MIN;

            if (rendered) {
                for (const auto& other : directElements) {
                    if (!other ||
                        winrt::get_abi(other) == winrt::get_abi(element)) {
                        continue;
                    }

                    const int otherCol = Controls::Grid::GetColumn(other);
                    if (otherCol >= col || otherCol <= nearestPredecessorColumn) {
                        continue;
                    }

                    if (IsRenderedSystemStripElement(other)) {
                        nearestRenderedPredecessor = other;
                        nearestPredecessorColumn = otherCol;
                    }
                }
            }

            const bool hasRenderedPredecessor =
                static_cast<bool>(nearestRenderedPredecessor);
            const bool predecessorIsLazyPair =
                hasRenderedPredecessor &&
                IsLazyImeOrInputContainer(nearestRenderedPredecessor);

            // Also look right so an IME container that is immediately followed
            // by the rendered Input container can act as the leading half of a
            // single native pair. In that common Japanese state the pair gets
            // no extra leading margin; otherwise the narrow IME glyph appears
            // much farther from the preceding control than ENG does.
            FrameworkElement nearestRenderedSuccessor{nullptr};
            int nearestSuccessorColumn = INT_MAX;
            if (rendered) {
                for (const auto& other : directElements) {
                    if (!other ||
                        winrt::get_abi(other) == winrt::get_abi(element)) {
                        continue;
                    }

                    const int otherCol = Controls::Grid::GetColumn(other);
                    if (otherCol <= col || otherCol >= nearestSuccessorColumn) {
                        continue;
                    }

                    if (IsRenderedSystemStripElement(other)) {
                        nearestRenderedSuccessor = other;
                        nearestSuccessorColumn = otherCol;
                    }
                }
            }

            const bool successorIsLazyPair =
                nearestRenderedSuccessor &&
                IsLazyImeOrInputContainer(nearestRenderedSuccessor);
            const bool isImeContainer =
                winrt::get_class_name(element) ==
                L"LogonUX.ImeModeIndicatorUserControl";
            const bool leadingAdjacentImeInputPair =
                isImeContainer && successorIsLazyPair;

            // IME mode + Input switch are a native Windows pair. The leading
            // IME half gets no injected external margin when the Input half is
            // also rendered, and Input itself gets no second gap after IME.
            // A lone ENG/Input button still receives the configurable boundary
            // spacing that fixed the earlier no-gap regression.
            const double desiredLeft =
                rendered && hasRenderedPredecessor &&
                        !predecessorIsLazyPair &&
                        !leadingAdjacentImeInputPair
                    ? std::max(0.0, nativeLeftSpacing)
                    : 0.0;

            UpdateImeModeGlyphVisualOffset(element);

            auto current = element.Margin();
            if (std::fabs(current.Left - desiredLeft) < 0.01) {
                continue;
            }

            element.Margin(
                Thickness{
                    desiredLeft,
                    current.Top,
                    current.Right,
                    current.Bottom});

            ModLog(
                L"Lazy native spacing updated: class=%s col=%d rendered=%d predecessor=%d predecessorLazyPair=%d successorLazyPair=%d leadingImePair=%d left=%.1f",
                winrt::get_class_name(element).c_str(),
                col,
                rendered ? 1 : 0,
                hasRenderedPredecessor ? 1 : 0,
                predecessorIsLazyPair ? 1 : 0,
                successorIsLazyPair ? 1 : 0,
                leadingAdjacentImeInputPair ? 1 : 0,
                desiredLeft);
        }
    } catch (...) {
    }
}


std::wstring FormatPercentageText(
        int percent,
        bool volume,
        bool muted) {
    if (percent < 0 || percent > 100) {
        return {};
    }

    if ((!volume &&
         g_hideBatteryPercentageAt100.load(std::memory_order_relaxed) &&
         percent == 100) ||
        (volume &&
         g_hideVolumePercentageAt100.load(std::memory_order_relaxed) &&
         percent == 100)) {
        return {};
    }

    std::wstring value = std::to_wstring(percent);
    if (g_percentageShowPercentSign.load(std::memory_order_relaxed)) {
        value += L"%";
    }

    if (volume && muted &&
        g_showMutedTextInVolumePercentage.load(
            std::memory_order_relaxed)) {
        value += L" (muted)";
    }

    return value;
}

void SetPercentageLabelText(
        Controls::TextBlock const& label,
        const std::wstring& value,
        bool featureEnabled) {
    if (!label) {
        return;
    }

    try {
        if (!featureEnabled || value.empty()) {
            label.Text(L"");
            label.Visibility(Visibility::Collapsed);
        } else {
            label.Text(hstring(value));
            label.Visibility(Visibility::Visible);
        }
    } catch (...) {
    }
}


void CopySafeVisualProperties(Controls::Control source,
                              Controls::Button target) {
    if (!source || !target) {
        return;
    }

    try {
        if (auto style = source.Style()) {
            target.Style(style);
        }
    } catch (...) {
    }

    try {
        target.Margin(source.Margin());
    } catch (...) {
    }

    try {
        target.MinWidth(source.MinWidth());
        target.MinHeight(source.MinHeight());
    } catch (...) {
    }

    try {
        auto foreground = source.Foreground();
        if (foreground) {
            target.Foreground(foreground);
        }
    } catch (...) {
    }
}



struct SystemButtonGroup {
    std::wstring key;
    int originalColumn = -1;
    std::vector<FrameworkElement> elements;
};


struct ColumnSizingSnapshot {
    GridLength width{};
    double minWidth = 0.0;
    double maxWidth = std::numeric_limits<double>::infinity();
};

struct NativeElementSnapshot {
    winrt::weak_ref<FrameworkElement> element;
    std::wstring groupKey;
    int column = 0;
    Visibility visibility = Visibility::Visible;
    Thickness margin{};
    double width = std::numeric_limits<double>::quiet_NaN();
    double minWidth = 0.0;
    double maxWidth = std::numeric_limits<double>::infinity();
    bool isControl = false;
    bool isTabStop = false;
    int tabIndex = 0;
    bool restoreColumn = false;
    bool restoreVisibility = false;
    bool restoreMargin = false;
    bool restoreSizing = false;
    bool restoreTabState = false;
    bool restoreImeVisualOffset = false;
};

struct InjectionSession {
    winrt::weak_ref<Controls::UserControl> owner;
    winrt::weak_ref<Controls::Grid> grid;
    DWORD xamlThreadId = 0;
    void* ownerIdentity = nullptr;
    uint32_t originalColumnCount = 0;
    std::vector<ColumnSizingSnapshot> originalColumns;
    std::vector<NativeElementSnapshot> nativeElements;

    Controls::Button batteryButton{nullptr};
    Controls::Button volumeButton{nullptr};
    Controls::Button clockButton{nullptr};
    event_token batteryClickToken{};
    event_token volumeClickToken{};
    event_token clockClickToken{};
    bool batteryClickAttached = false;
    bool volumeClickAttached = false;
    bool clockClickAttached = false;
    std::shared_ptr<BatteryWheelRegistration> batteryWheel;
    std::shared_ptr<BatteryMiddleClickRegistration> batteryMiddleClick;
    std::shared_ptr<VolumeMonitorState> volumeState;
    DispatcherTimer clockTimer{nullptr};
    event_token clockTickToken{};
    bool clockTickAttached = false;

    event_token spacingLayoutToken{};
    bool spacingLayoutAttached = false;
    event_token diagnosticsLayoutToken{};
    bool diagnosticsLayoutAttached = false;
    event_token ownerUnloadedToken{};
    bool ownerUnloadedAttached = false;
    event_token ownerLoadedToken{};
    bool ownerLoadedAttached = false;

    std::atomic<bool> active{true};
    std::atomic<bool> cleaning{false};
};

SRWLOCK g_injectionSessionsLock = SRWLOCK_INIT;
std::vector<std::shared_ptr<InjectionSession>> g_injectionSessions;

void TrackInjectionSession(
        std::shared_ptr<InjectionSession> const& session) {
    if (!session) return;
    AcquireSRWLockExclusive(&g_injectionSessionsLock);
    g_injectionSessions.push_back(session);
    ReleaseSRWLockExclusive(&g_injectionSessionsLock);
}

void UntrackInjectionSession(
        std::shared_ptr<InjectionSession> const& session) {
    if (!session) return;
    AcquireSRWLockExclusive(&g_injectionSessionsLock);
    auto it = std::remove_if(
        g_injectionSessions.begin(),
        g_injectionSessions.end(),
        [&](const std::shared_ptr<InjectionSession>& item) {
            return item.get() == session.get();
        });
    g_injectionSessions.erase(it, g_injectionSessions.end());
    ReleaseSRWLockExclusive(&g_injectionSessionsLock);
}

std::shared_ptr<InjectionSession> FindInjectionSessionForOwner(
        void* ownerIdentity) {
    if (!ownerIdentity) return nullptr;
    std::shared_ptr<InjectionSession> result;
    AcquireSRWLockShared(&g_injectionSessionsLock);
    for (const auto& session : g_injectionSessions) {
        if (session && session->ownerIdentity == ownerIdentity &&
            !session->cleaning.load(std::memory_order_acquire)) {
            result = session;
            break;
        }
    }
    ReleaseSRWLockShared(&g_injectionSessionsLock);
    return result;
}

std::vector<DWORD> SnapshotInjectionSessionThreads() {
    std::vector<DWORD> result;
    AcquireSRWLockShared(&g_injectionSessionsLock);
    for (const auto& session : g_injectionSessions) {
        if (!session || !session->xamlThreadId) continue;
        if (std::find(result.begin(), result.end(), session->xamlThreadId) ==
            result.end()) {
            result.push_back(session->xamlThreadId);
        }
    }
    ReleaseSRWLockShared(&g_injectionSessionsLock);
    return result;
}

void CleanupInjectionSession(
    std::shared_ptr<InjectionSession> const& session,
    bool restoreLayout,
    bool fromOwnerUnload);

void AttachDynamicLazyNativeSpacing(
        Controls::UserControl const& userControl,
        Controls::Grid const& parentGrid,
        int firstSystemColumn,
        int usedEndColumn,
        double nativeLeftSpacing,
        std::shared_ptr<InjectionSession> const& session) {
    if (!userControl || !parentGrid || !session ||
        usedEndColumn <= firstSystemColumn) {
        return;
    }

    try {
        auto weakGrid = winrt::make_weak(parentGrid);

        UpdateLazyNativeContainerSpacing(
            parentGrid,
            firstSystemColumn,
            usedEndColumn,
            nativeLeftSpacing);

        session->spacingLayoutToken = userControl.LayoutUpdated(
            [weakGrid,
             firstSystemColumn,
             usedEndColumn,
             nativeLeftSpacing](auto const&, auto const&) {
                if (g_unloading.load(std::memory_order_acquire)) {
                    return;
                }
                auto grid = weakGrid.get();
                if (!grid) {
                    return;
                }

                UpdateLazyNativeContainerSpacing(
                    grid,
                    firstSystemColumn,
                    usedEndColumn,
                    nativeLeftSpacing);
            });
        session->spacingLayoutAttached = true;

        ModLog(
            L"Dynamic IME/Input spacing watcher attached: cols=%d..%d nativeLeftSpacing=%.1f",
            firstSystemColumn,
            usedEndColumn - 1,
            nativeLeftSpacing);
    } catch (...) {
        ModLog(L"Failed to attach dynamic IME/Input spacing watcher");
    }
}


std::vector<Controls::Button> DirectButtonsAtGridColumn(
    Controls::Grid const& grid,
    int targetRow,
    int targetColumn) {
    std::vector<Controls::Button> result;
    if (!grid) return result;

    try {
        auto children = grid.Children();
        for (uint32_t i = 0; i < children.Size(); ++i) {
            auto button = children.GetAt(i).try_as<Controls::Button>();
            if (!button) continue;
            if (Controls::Grid::GetRow(button) != targetRow) continue;
            if (Controls::Grid::GetColumn(button) != targetColumn) continue;
            result.push_back(button);
        }
    } catch (...) {
    }
    return result;
}

bool ContainsElementIdentity(const std::vector<FrameworkElement>& elements,
                             FrameworkElement const& needle) {
    const void* needleAbi = needle ? winrt::get_abi(needle) : nullptr;
    for (const auto& element : elements) {
        if (element && winrt::get_abi(element) == needleAbi) return true;
    }
    return false;
}

void CollectSystemButtonDiagnosticHints(
        FrameworkElement const& element,
        std::vector<std::wstring>& hints,
        int depth = 0) {
    if (!element || depth > 6 || hints.size() >= 32) {
        return;
    }

    try {
        const auto name = element.Name();
        if (!name.empty()) {
            hints.emplace_back(name.c_str());
        }
    } catch (...) {
    }

    try {
        const auto automationName =
            Automation::AutomationProperties::GetName(element);
        if (!automationName.empty()) {
            hints.emplace_back(automationName.c_str());
        }
    } catch (...) {
    }

    try {
        if (auto textBlock = element.try_as<Controls::TextBlock>()) {
            const auto value = textBlock.Text();
            if (!value.empty()) {
                hints.emplace_back(value.c_str());
            }
        }
    } catch (...) {
    }

    try {
        if (auto contentControl =
                element.try_as<Controls::ContentControl>()) {
            auto content = contentControl.Content();
            if (content) {
                try {
                    const auto value =
                        winrt::unbox_value<hstring>(content);
                    if (!value.empty()) {
                        hints.emplace_back(value.c_str());
                    }
                } catch (...) {
                }
            }
        }
    } catch (...) {
    }

    try {
        const int childCount =
            Media::VisualTreeHelper::GetChildrenCount(element);
        for (int i = 0; i < childCount && hints.size() < 32; ++i) {
            auto child =
                Media::VisualTreeHelper::GetChild(element, i)
                    .try_as<FrameworkElement>();
            if (child) {
                CollectSystemButtonDiagnosticHints(
                    child, hints, depth + 1);
            }
        }
    } catch (...) {
    }
}

bool IsSystemButtonGroupCurrentlyVisible(
        const SystemButtonGroup& group) {
    for (const auto& element : group.elements) {
        if (!element) {
            continue;
        }

        try {
            if (element.Visibility() == Visibility::Visible &&
                element.ActualWidth() > 0.0 &&
                element.ActualHeight() > 0.0) {
                return true;
            }
        } catch (...) {
        }

        // A Windows-owned UserControl can itself have no rendered size while
        // one of its lazy-loaded child Buttons is transitioning. Check the
        // subtree as a diagnostic aid without using this for role detection.
        try {
            const int childCount =
                Media::VisualTreeHelper::GetChildrenCount(element);
            for (int i = 0; i < childCount; ++i) {
                auto child = Media::VisualTreeHelper::GetChild(element, i)
                                 .try_as<FrameworkElement>();
                if (child && child.Visibility() == Visibility::Visible &&
                    child.ActualWidth() > 0.0 && child.ActualHeight() > 0.0) {
                    return true;
                }
            }
        } catch (...) {
        }
    }
    return false;
}

void LogNativeSystemButtonSemanticDiagnostics(
        const std::vector<SystemButtonGroup>& groups) {
    if (!(g_enableLogging.load(std::memory_order_relaxed) ||
          g_enableFileLogging.load(std::memory_order_relaxed)) ||
        !g_enableVerboseDiagnostics.load(std::memory_order_relaxed)) {
        return;
    }

    for (const auto& group : groups) {
        std::vector<std::wstring> hints;
        for (const auto& element : group.elements) {
            CollectSystemButtonDiagnosticHints(element, hints);
        }

        ModLog(L"Native-slot diagnostic: key=%s originalCol=%d visible=%d hints=%u",
               group.key.c_str(),
               group.originalColumn,
               IsSystemButtonGroupCurrentlyVisible(group) ? 1 : 0,
               static_cast<unsigned>(hints.size()));

        for (size_t i = 0; i < hints.size(); ++i) {
            ModLog(L"Native-slot hint: key=%s originalCol=%d hint[%u]=%s",
                   group.key.c_str(),
                   group.originalColumn,
                   static_cast<unsigned>(i),
                   hints[i].c_str());
        }
    }
}

bool IsSystemButtonGroupEnabled(const std::wstring& key) {
    if (key == L"ime") {
        return g_showImeModeButton.load(std::memory_order_relaxed);
    }
    if (key == L"input") {
        return g_showInputButton.load(std::memory_order_relaxed);
    }
    if (key == L"network-signin") {
        return g_showNetworkSignInButton.load(std::memory_order_relaxed);
    }
    if (key == L"disconnect") {
        return g_showDisconnectButton.load(std::memory_order_relaxed);
    }
    if (key == L"network") {
        return g_showNetworkButton.load(std::memory_order_relaxed);
    }
    if (key == L"accessibility") {
        return g_showAccessibilityButton.load(std::memory_order_relaxed);
    }
    if (key == L"power") {
        return g_showPowerButton.load(std::memory_order_relaxed);
    }
    if (key.rfind(L"native", 0) == 0) {
        return g_showOtherNativeButtons.load(std::memory_order_relaxed);
    }
    return true;
}

std::vector<SystemButtonGroup> DiscoverNativeSystemButtonGroups(
    Controls::Grid const& grid,
    Controls::Button const& powerButton,
    int row,
    int powerColumn) {
    std::vector<SystemButtonGroup> directButtonGroups;

    // Direct Windows-owned Buttons form a contiguous strip ending at Power.
    // On the tested layouts the last three are Network, Accessibility, Power;
    // two earlier auxiliary slots are Network sign-in and Disconnect.
    for (int col = powerColumn; col >= 0; --col) {
        auto buttons = DirectButtonsAtGridColumn(grid, row, col);
        if (buttons.empty()) {
            if (col == powerColumn) return {};
            break;
        }

        SystemButtonGroup group;
        group.originalColumn = col;
        for (const auto& button : buttons) {
            group.elements.push_back(button);
        }
        directButtonGroups.push_back(std::move(group));
    }

    std::reverse(directButtonGroups.begin(), directButtonGroups.end());
    if (directButtonGroups.empty() ||
        !ContainsElementIdentity(
            directButtonGroups.back().elements, powerButton)) {
        return {};
    }

    directButtonGroups.back().key = L"power";
    if (directButtonGroups.size() >= 2) {
        directButtonGroups[directButtonGroups.size() - 2].key = L"accessibility";
    }
    if (directButtonGroups.size() >= 3) {
        directButtonGroups[directButtonGroups.size() - 3].key = L"network";
    }

    const size_t auxiliaryCount =
        directButtonGroups.size() > 3 ? directButtonGroups.size() - 3 : 0;
    if (auxiliaryCount == 2) {
        directButtonGroups[0].key = L"network-signin";
        directButtonGroups[1].key = L"disconnect";
    } else {
        size_t nativeIndex = 1;
        for (size_t i = 0; i < auxiliaryCount; ++i) {
            directButtonGroups[i].key =
                L"native" + std::to_wstring(nativeIndex++);
        }
    }

    std::vector<SystemButtonGroup> groups = directButtonGroups;

    // Windows 11 keeps both of these UserControls in the SystemProvider tree
    // even before their inner Buttons become visible. Identify the containers
    // by runtime XAML class so ordering/visibility works before lazy loading.
    try {
        auto children = grid.Children();
        for (uint32_t i = 0; i < children.Size(); ++i) {
            auto element = children.GetAt(i).try_as<FrameworkElement>();
            if (!element || element.try_as<Controls::Button>()) {
                continue;
            }
            if (Controls::Grid::GetRow(element) != row) {
                continue;
            }

            std::wstring key;
            try {
                const auto className = winrt::get_class_name(element);
                if (className == L"LogonUX.ImeModeIndicatorUserControl") {
                    key = L"ime";
                } else if (className == L"LogonUX.InputSwitchUserControl") {
                    key = L"input";
                }
            } catch (...) {
            }

            if (key.empty()) {
                continue;
            }

            SystemButtonGroup group;
            group.key = std::move(key);
            group.originalColumn = Controls::Grid::GetColumn(element);
            group.elements.push_back(element);
            groups.push_back(std::move(group));
        }
    } catch (...) {
    }

    std::stable_sort(
        groups.begin(), groups.end(),
        [](const SystemButtonGroup& a, const SystemButtonGroup& b) {
            if (a.originalColumn != b.originalColumn) {
                return a.originalColumn < b.originalColumn;
            }
            // If two native containers share a column, keep IME before Input.
            if (a.key == L"ime" && b.key == L"input") return true;
            if (a.key == L"input" && b.key == L"ime") return false;
            return a.key < b.key;
        });

    return groups;
}

void LogNativeSystemButtonGroups(
    const std::vector<SystemButtonGroup>& groups) {
    for (const auto& group : groups) {
        ModLog(L"Button-order native slot: %s originalCol=%d elementCount=%u",
               group.key.c_str(),
               group.originalColumn,
               static_cast<unsigned>(group.elements.size()));
    }
}

bool WriteBgraBmp(const wchar_t* path,
                  const uint8_t* pixels,
                  uint32_t width,
                  uint32_t height,
                  uint32_t byteCount) {
    if (!pixels || width == 0 || height == 0) {
        ModLog(L"XAML capture: invalid pixel buffer/size");
        return false;
    }

    const uint64_t expected64 =
        static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4ull;
    if (expected64 > UINT32_MAX || byteCount < static_cast<uint32_t>(expected64)) {
        ModLog(L"XAML capture: unexpected pixel buffer size=%u expected=%llu",
               byteCount, expected64);
        return false;
    }

    BITMAPFILEHEADER fileHeader{};
    fileHeader.bfType = 0x4D42; // 'BM'
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize =
        fileHeader.bfOffBits + static_cast<DWORD>(expected64);

    BITMAPINFOHEADER info{};
    info.biSize = sizeof(BITMAPINFOHEADER);
    info.biWidth = static_cast<LONG>(width);
    info.biHeight = -static_cast<LONG>(height); // RenderTargetBitmap is top-down BGRA.
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    info.biSizeImage = static_cast<DWORD>(expected64);

    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        ModLog(L"XAML capture: CreateFile failed for %s: %lu",
               path, GetLastError());
        return false;
    }

    DWORD written = 0;
    const BOOL w1 =
        WriteFile(file, &fileHeader, sizeof(fileHeader), &written, nullptr);
    const BOOL w2 =
        WriteFile(file, &info, sizeof(info), &written, nullptr);
    const BOOL w3 =
        WriteFile(file, pixels, static_cast<DWORD>(expected64), &written, nullptr);
    const bool ok = w1 && w2 && w3;
    if (!ok) {
        ModLog(L"XAML capture: WriteFile failed: %lu", GetLastError());
    }

    CloseHandle(file);
    return ok;
}

std::wstring BuildXamlCapturePath() {
    std::wstring directory;
    AcquireSRWLockShared(&g_settingsLock);
    directory = g_xamlCaptureDirectory;
    ReleaseSRWLockShared(&g_settingsLock);

    if (directory.empty()) {
        directory = ExpandEnvironmentPathOrDefault(
            nullptr, L"%PUBLIC%\\Desktop");
    }

    while (!directory.empty() &&
           (directory.back() == L'\\' || directory.back() == L'/')) {
        directory.pop_back();
    }

    SYSTEMTIME st{};
    GetLocalTime(&st);

    wchar_t fileName[128]{};
    swprintf_s(
        fileName, ARRAYSIZE(fileName),
        L"LogonUI-SystemProvider-%04u%02u%02u-%02u%02u%02u-%03u.bmp",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    if (directory.empty()) {
        return fileName;
    }

    return directory + L"\\" + fileName;
}

[[maybe_unused]] winrt::fire_and_forget CaptureXamlSubtreeAsync(FrameworkElement element) {
    struct CaptureInFlightGuard {
        CaptureInFlightGuard() {
            g_captureAsyncInFlight.fetch_add(1, std::memory_order_acq_rel);
        }
        ~CaptureInFlightGuard() {
            g_captureAsyncInFlight.fetch_sub(1, std::memory_order_acq_rel);
        }
    } inFlightGuard;

    // Keep the projected XAML element alive across the asynchronous render.
    auto strongElement = element;

    try {
        Media::Imaging::RenderTargetBitmap bitmap;
        co_await bitmap.RenderAsync(strongElement);

        const int pixelWidth = bitmap.PixelWidth();
        const int pixelHeight = bitmap.PixelHeight();
        if (pixelWidth <= 0 || pixelHeight <= 0) {
            ModLog(L"XAML capture: RenderTargetBitmap returned invalid size %dx%d",
                   pixelWidth, pixelHeight);
            g_captureScheduled.store(false, std::memory_order_relaxed);
            co_return;
        }

        auto buffer = co_await bitmap.GetPixelsAsync();
        const uint32_t length = buffer.Length();
        std::vector<uint8_t> pixels(length);

        auto reader =
            winrt::Windows::Storage::Streams::DataReader::FromBuffer(buffer);
        reader.ReadBytes(pixels);

        const std::wstring path = BuildXamlCapturePath();

        if (WriteBgraBmp(path.c_str(),
                         pixels.data(),
                         static_cast<uint32_t>(pixelWidth),
                         static_cast<uint32_t>(pixelHeight),
                         length)) {
            ModLog(L"XAML SystemProvider capture saved: %s (%dx%d)",
                   path.c_str(), pixelWidth, pixelHeight);
        } else {
            ModLog(L"XAML SystemProvider capture failed while saving");
        }
    } catch (const winrt::hresult_error& e) {
        ModLog(L"XAML SystemProvider capture failed: 0x%08X %s",
               static_cast<unsigned>(e.code().value),
               e.message().c_str());
    } catch (...) {
        ModLog(L"XAML SystemProvider capture failed with unknown exception");
    }

    // A successful capture used to leave this latched forever, making the
    // diagnostics effectively one-shot. Clear it after every completed render
    // so a later native-layout change can request a second capture.
    g_captureScheduled.store(false, std::memory_order_relaxed);
}

struct XamlCaptureWaitState {
    DWORD xamlThreadId = 0;
    Controls::UserControl control{nullptr};
    Controls::Image batteryImage{nullptr};
    DispatcherTimer timer{nullptr};
    event_token tickToken{};
    int attempts = 0;
    int stableTicks = 0;
    double lastWidth = -1.0;
    double lastHeight = -1.0;
};

SRWLOCK g_xamlCaptureWaitLock = SRWLOCK_INIT;
std::shared_ptr<XamlCaptureWaitState> g_xamlCaptureWaitState;

[[maybe_unused]] void StopXamlCaptureWait(std::shared_ptr<XamlCaptureWaitState> const& state) {
    if (!state) {
        return;
    }

    if (state->timer) {
        try {
            state->timer.Tick(state->tickToken);
        } catch (...) {
        }
        try {
            state->timer.Stop();
        } catch (...) {
        }
        state->timer = nullptr;
    }

    state->control = nullptr;
    state->batteryImage = nullptr;

    AcquireSRWLockExclusive(&g_xamlCaptureWaitLock);
    if (g_xamlCaptureWaitState.get() == state.get()) {
        g_xamlCaptureWaitState.reset();
    }
    ReleaseSRWLockExclusive(&g_xamlCaptureWaitLock);
}

DWORD GetOutstandingXamlCaptureThreadId() {
    DWORD threadId = 0;
    AcquireSRWLockShared(&g_xamlCaptureWaitLock);
    if (g_xamlCaptureWaitState) {
        threadId = g_xamlCaptureWaitState->xamlThreadId;
    }
    ReleaseSRWLockShared(&g_xamlCaptureWaitLock);
    return threadId;
}

void StopOutstandingXamlCaptureWaitOnUiThread() {
    std::shared_ptr<XamlCaptureWaitState> state;
    AcquireSRWLockShared(&g_xamlCaptureWaitLock);
    state = g_xamlCaptureWaitState;
    ReleaseSRWLockShared(&g_xamlCaptureWaitLock);
    if (state && state->xamlThreadId &&
        state->xamlThreadId != GetCurrentThreadId()) {
        return;
    }
    StopXamlCaptureWait(state);
    g_captureScheduled.store(false, std::memory_order_release);
}

void ScheduleXamlSubtreeCapture(Controls::UserControl const& userControl,
                                Controls::Image const& batteryImage) {
    bool expected = false;
    if (!g_captureScheduled.compare_exchange_strong(expected, true)) {
        return;
    }

    try {
        auto state = std::make_shared<XamlCaptureWaitState>();
        state->xamlThreadId = GetCurrentThreadId();
        AcquireSRWLockExclusive(&g_xamlCaptureWaitLock);
        g_xamlCaptureWaitState = state;
        ReleaseSRWLockExclusive(&g_xamlCaptureWaitLock);
        state->control = userControl;
        state->batteryImage = batteryImage;
        state->timer = DispatcherTimer();
        state->timer.Interval(std::chrono::milliseconds(200));

        state->tickToken = state->timer.Tick(
            [state](auto const&, auto const&) {
                try {
                    ++state->attempts;

                    if (!state->control || !state->batteryImage) {
                        ModLog(L"XAML capture: target disappeared before capture");
                        StopXamlCaptureWait(state);
                        g_captureScheduled = false;
                        return;
                    }

                    auto element =
                        state->control.Content().try_as<FrameworkElement>();
                    if (!element) {
                        ModLog(L"XAML capture: SystemProvider root disappeared");
                        StopXamlCaptureWait(state);
                        g_captureScheduled = false;
                        return;
                    }

                    const double width = element.ActualWidth();
                    const double height = element.ActualHeight();

                    if (std::abs(width - state->lastWidth) < 0.25 &&
                        std::abs(height - state->lastHeight) < 0.25) {
                        ++state->stableTicks;
                    } else {
                        state->stableTicks = 0;
                        state->lastWidth = width;
                        state->lastHeight = height;
                    }

                    int batteryPixelWidth = 0;
                    int batteryPixelHeight = 0;
                    bool batteryDecoded = false;

                    try {
                        auto source = state->batteryImage.Source();
                        auto bitmap =
                            source.try_as<Media::Imaging::BitmapImage>();
                        if (bitmap) {
                            batteryPixelWidth = bitmap.PixelWidth();
                            batteryPixelHeight = bitmap.PixelHeight();
                            batteryDecoded =
                                batteryPixelWidth > 0 && batteryPixelHeight > 0;
                        }
                    } catch (...) {
                    }

                    const bool rootRenderable =
                        width > 0.0 && height > 0.0;
                    const bool layoutExpanded =
                        width >= 120.0; // 96 DIP is the observed Win+L transitional width.
                    const bool batteryLaidOut =
                        state->batteryImage.ActualWidth() > 0.0 &&
                        state->batteryImage.ActualHeight() > 0.0;

                    if (rootRenderable &&
                        layoutExpanded &&
                        batteryDecoded &&
                        batteryLaidOut &&
                        state->stableTicks >= 2) {
                        ModLog(
                            L"XAML capture: stable SystemProvider %.1fx%.1f DIP, "
                            L"battery bitmap=%dx%d, image=%.1fx%.1f DIP; rendering",
                            width,
                            height,
                            batteryPixelWidth,
                            batteryPixelHeight,
                            state->batteryImage.ActualWidth(),
                            state->batteryImage.ActualHeight());

                        auto captureElement = element;
                        StopXamlCaptureWait(state);
                        CaptureXamlSubtreeAsync(captureElement);
                        return;
                    }

                    if (state->attempts == 1 ||
                        state->attempts % 5 == 0) {
                        ModLog(
                            L"XAML capture waiting: root=%.1fx%.1f DIP stable=%d "
                            L"battery=%dx%d image=%.1fx%.1f DIP attempt=%d",
                            width,
                            height,
                            state->stableTicks,
                            batteryPixelWidth,
                            batteryPixelHeight,
                            state->batteryImage.ActualWidth(),
                            state->batteryImage.ActualHeight(),
                            state->attempts);
                    }

                    if (state->attempts >= 25) {
                        ModLog(
                            L"XAML capture abandoned after 5 seconds; "
                            L"layout/image never reached a stable renderable state");
                        StopXamlCaptureWait(state);
                        g_captureScheduled = false;
                    }
                } catch (const winrt::hresult_error& e) {
                    ModLog(L"XAML capture wait failed: 0x%08X %s",
                           static_cast<unsigned>(e.code().value),
                           e.message().c_str());
                    StopXamlCaptureWait(state);
                    g_captureScheduled = false;
                } catch (...) {
                    ModLog(L"XAML capture wait failed with unknown exception");
                    StopXamlCaptureWait(state);
                    g_captureScheduled = false;
                }
            });

        state->timer.Start();
        ModLog(
            L"XAML SystemProvider capture armed; waiting for stable layout "
            L"and decoded BatteryStatusIcon");
    } catch (const winrt::hresult_error& e) {
        ModLog(L"Couldn't arm stable XAML SystemProvider capture: 0x%08X %s",
               static_cast<unsigned>(e.code().value),
               e.message().c_str());
        g_captureScheduled = false;
    } catch (...) {
        ModLog(
            L"Couldn't arm stable XAML SystemProvider capture with unknown exception");
        g_captureScheduled = false;
    }
}

void CollectNativeButtonsForDynamicDiagnostics(
        FrameworkElement const& element,
        std::vector<Controls::Button>& buttons,
        int depth = 0) {
    if (!element || depth > 12 || buttons.size() >= 64) {
        return;
    }

    try {
        if (auto button = element.try_as<Controls::Button>()) {
            bool injected = false;
            try {
                const auto name = button.Name();
                injected =
                    (!name.empty() &&
                     (_wcsicmp(name.c_str(), kInjectedBatteryName) == 0 ||
                      _wcsicmp(name.c_str(), kInjectedVolumeName) == 0 ||
                      _wcsicmp(name.c_str(), kInjectedClockName) == 0));
            } catch (...) {
            }

            if (!injected) {
                buttons.push_back(button);
            }
        }

        const int childCount =
            Media::VisualTreeHelper::GetChildrenCount(element);
        for (int i = 0;
             i < childCount && buttons.size() < 64;
             ++i) {
            auto child =
                Media::VisualTreeHelper::GetChild(element, i)
                    .try_as<FrameworkElement>();
            if (child) {
                CollectNativeButtonsForDynamicDiagnostics(
                    child, buttons, depth + 1);
            }
        }
    } catch (...) {
    }
}

std::wstring BuildNativeButtonDynamicSignature(
        FrameworkElement const& root) {
    std::vector<Controls::Button> buttons;
    CollectNativeButtonsForDynamicDiagnostics(root, buttons);

    std::wstring signature;
    signature.reserve(512);
    signature += std::to_wstring(buttons.size());

    for (const auto& button : buttons) {
        if (!button) {
            continue;
        }

        signature += L"|";
        signature += std::to_wstring(
            reinterpret_cast<unsigned long long>(winrt::get_abi(button)));

        try {
            const auto name = button.Name();
            signature += L":n=";
            signature.append(name.c_str(), name.size());
        } catch (...) {
        }

        try {
            const auto automationName =
                Automation::AutomationProperties::GetName(button);
            signature += L":a=";
            signature.append(
                automationName.c_str(), automationName.size());
        } catch (...) {
        }

        try {
            const bool visible =
                button.Visibility() == Visibility::Visible;
            const bool rendered =
                visible && button.ActualWidth() > 0.0 &&
                button.ActualHeight() > 0.0;
            signature += L":v=" +
                std::to_wstring(visible ? 1 : 0);
            signature += L":r=" +
                std::to_wstring(rendered ? 1 : 0);
            signature += L":row=" +
                std::to_wstring(Controls::Grid::GetRow(button));
            signature += L":col=" +
                std::to_wstring(Controls::Grid::GetColumn(button));
        } catch (...) {
        }
    }

    return signature;
}

void LogDynamicNativeButtonDiagnostics(
        FrameworkElement const& root) {
    std::vector<Controls::Button> buttons;
    CollectNativeButtonsForDynamicDiagnostics(root, buttons);

    ModLog(L"Dynamic native-button snapshot: count=%u root=%.1fx%.1f DIP",
           static_cast<unsigned>(buttons.size()),
           root ? root.ActualWidth() : 0.0,
           root ? root.ActualHeight() : 0.0);

    for (size_t i = 0; i < buttons.size(); ++i) {
        const auto& button = buttons[i];
        if (!button) {
            continue;
        }

        hstring className;
        hstring name;
        hstring automationName;
        hstring parentClass;
        hstring parentName;
        int row = -1;
        int col = -1;
        int visible = 0;
        int rendered = 0;
        double width = 0.0;
        double height = 0.0;

        try { className = winrt::get_class_name(button); } catch (...) {}
        try { name = button.Name(); } catch (...) {}
        try {
            automationName =
                Automation::AutomationProperties::GetName(button);
        } catch (...) {}
        try {
            row = Controls::Grid::GetRow(button);
            col = Controls::Grid::GetColumn(button);
            visible =
                button.Visibility() == Visibility::Visible ? 1 : 0;
            width = button.ActualWidth();
            height = button.ActualHeight();
            rendered =
                visible && width > 0.0 && height > 0.0 ? 1 : 0;
        } catch (...) {}
        try {
            auto parent =
                Media::VisualTreeHelper::GetParent(button)
                    .try_as<FrameworkElement>();
            if (parent) {
                parentClass = winrt::get_class_name(parent);
                parentName = parent.Name();
            }
        } catch (...) {}

        ModLog(
            L"Dynamic native-button[%u]: class=%s name=%s automation=%s visible=%d rendered=%d size=%.1fx%.1f row=%d col=%d parentClass=%s parentName=%s",
            static_cast<unsigned>(i),
            className.empty() ? L"-" : className.c_str(),
            name.empty() ? L"-" : name.c_str(),
            automationName.empty() ? L"-" : automationName.c_str(),
            visible,
            rendered,
            width,
            height,
            row,
            col,
            parentClass.empty() ? L"-" : parentClass.c_str(),
            parentName.empty() ? L"-" : parentName.c_str());

        std::vector<std::wstring> hints;
        CollectSystemButtonDiagnosticHints(button, hints);
        for (size_t hintIndex = 0;
             hintIndex < hints.size();
             ++hintIndex) {
            ModLog(
                L"Dynamic native-button[%u] hint[%u]=%s",
                static_cast<unsigned>(i),
                static_cast<unsigned>(hintIndex),
                hints[hintIndex].c_str());
        }
    }
}

void AttachDynamicSystemProviderDiagnostics(
        Controls::UserControl const& userControl,
        Controls::Image const& batteryImage,
        std::shared_ptr<InjectionSession> const& session) {
    const bool verbose =
        (g_enableLogging.load(std::memory_order_relaxed) ||
         g_enableFileLogging.load(std::memory_order_relaxed)) &&
        g_enableVerboseDiagnostics.load(std::memory_order_relaxed);
    const bool capture =
        g_enableXamlCapture.load(std::memory_order_relaxed);

    if (!userControl || !session || (!verbose && !capture)) {
        return;
    }

    try {
        auto weakControl = winrt::make_weak(userControl);
        auto weakBatteryImage = winrt::make_weak(batteryImage);
        auto layoutToken = std::make_shared<winrt::event_token>();
        auto lastSignature = std::make_shared<std::wstring>();
        auto baselineReady = std::make_shared<bool>(false);
        auto pendingFollowupCapture =
            std::make_shared<bool>(false);

        // Snapshot immediately after our own injection. Waiting for the first
        // diagnostic capture to finish can miss the exact transition: if the
        // next LayoutUpdated is caused by tapping the password box, that event
        // already contains the newly materialized Input button.
        try {
            auto initialRoot =
                userControl.Content().try_as<FrameworkElement>();
            if (initialRoot) {
                *lastSignature =
                    BuildNativeButtonDynamicSignature(initialRoot);
                *baselineReady = true;
                if (verbose) {
                    ModLog(
                        L"Dynamic SystemProvider native-button watcher baseline recorded at attachment");
                    LogDynamicNativeButtonDiagnostics(initialRoot);
                }
            }
        } catch (...) {
        }

        *layoutToken = userControl.LayoutUpdated(
            [weakControl, weakBatteryImage, lastSignature,
             baselineReady, pendingFollowupCapture, verbose](
                auto const&, auto const&) {
                if (g_unloading.load(std::memory_order_acquire)) {
                    return;
                }
                auto control = weakControl.get();
                if (!control) {
                    return;
                }

                try {
                    auto root =
                        control.Content().try_as<FrameworkElement>();
                    if (!root) {
                        return;
                    }

                    // Teardown can produce a final LayoutUpdated with a
                    // zero-sized root before Unloaded fires. Ignore it instead
                    // of generating a misleading full-tree diagnostic dump.
                    if (root.ActualWidth() <= 0.0 &&
                        root.ActualHeight() <= 0.0) {
                        return;
                    }

                    const auto signature =
                        BuildNativeButtonDynamicSignature(root);

                    if (!*baselineReady) {
                        *lastSignature = signature;
                        *baselineReady = true;
                        if (verbose) {
                            ModLog(
                                L"Dynamic SystemProvider native-button watcher late baseline recorded");
                            LogDynamicNativeButtonDiagnostics(root);
                        }
                        return;
                    }

                    if (signature != *lastSignature) {
                        *lastSignature = signature;
                        *pendingFollowupCapture = true;

                        if (verbose) {
                            ModLog(
                                L"Dynamic SystemProvider native-button tree changed (possible lazy-loaded Input/language control)");
                            LogDynamicNativeButtonDiagnostics(root);

                            // One full tree dump at the transition tells us
                            // whether the new control lives in the same native
                            // button Grid or another XAML host.
                            LogTree(root);
                        }
                    }

                    if (*pendingFollowupCapture &&
                        g_enableXamlCapture.load(
                            std::memory_order_relaxed) &&
                        !g_captureScheduled.load(
                            std::memory_order_relaxed)) {
                        auto image = weakBatteryImage.get();
                        if (image) {
                            *pendingFollowupCapture = false;
                            ModLog(
                                L"Dynamic SystemProvider change: arming follow-up XAML capture");
                            ScheduleXamlSubtreeCapture(
                                control, image);
                        }
                    }
                } catch (...) {
                }
            });

        session->diagnosticsLayoutToken = *layoutToken;
        session->diagnosticsLayoutAttached = true;

        if (verbose) {
            ModLog(
                L"Dynamic SystemProvider native-button watcher attached (event-driven; verbose diagnostics)");
        } else {
            ModLog(
                L"Dynamic SystemProvider watcher attached for XAML capture only");
        }
    } catch (const winrt::hresult_error& e) {
        ModLog(
            L"Failed to attach dynamic SystemProvider diagnostics: 0x%08X %s",
            static_cast<unsigned>(e.code().value),
            e.message().c_str());
    } catch (...) {
        ModLog(
            L"Failed to attach dynamic SystemProvider diagnostics with unknown exception");
    }
}

bool IsSystemProviderUserControl(
        Controls::UserControl const& userControl) {
    if (!userControl) return false;
    try {
        return winrt::get_class_name(userControl) ==
               L"LogonUX.SystemProviderUserControl";
    } catch (...) {
        return false;
    }
}

void InjectVisual(void* sender, bool allowLayoutRetry = true);

bool TryMarkLayoutRetryActive(void* identity) {
    if (!identity) {
        return false;
    }

    AcquireSRWLockExclusive(&g_layoutRetryLock);
    const bool alreadyActive =
        std::find(g_layoutRetryOwners.begin(),
                  g_layoutRetryOwners.end(),
                  identity) != g_layoutRetryOwners.end();
    if (!alreadyActive) {
        g_layoutRetryOwners.push_back(identity);
    }
    ReleaseSRWLockExclusive(&g_layoutRetryLock);
    return !alreadyActive;
}

void ClearLayoutRetryActive(void* identity) {
    if (!identity) {
        return;
    }

    AcquireSRWLockExclusive(&g_layoutRetryLock);
    auto it = std::remove(
        g_layoutRetryOwners.begin(),
        g_layoutRetryOwners.end(),
        identity);
    g_layoutRetryOwners.erase(it, g_layoutRetryOwners.end());
    ReleaseSRWLockExclusive(&g_layoutRetryLock);
}

struct LayoutRetryRegistration {
    winrt::weak_ref<Controls::UserControl> owner;
    DWORD xamlThreadId = 0;
    event_token layoutToken{};
    event_token unloadedToken{};
    bool layoutAttached = false;
    bool unloadedAttached = false;
    void* identity = nullptr;
    std::atomic<bool> detached{false};
};

SRWLOCK g_layoutRetryRegistrationsLock = SRWLOCK_INIT;
std::vector<std::shared_ptr<LayoutRetryRegistration>>
    g_layoutRetryRegistrations;

void TrackLayoutRetryRegistration(
        std::shared_ptr<LayoutRetryRegistration> const& registration) {
    AcquireSRWLockExclusive(&g_layoutRetryRegistrationsLock);
    g_layoutRetryRegistrations.push_back(registration);
    ReleaseSRWLockExclusive(&g_layoutRetryRegistrationsLock);
}

void UntrackLayoutRetryRegistration(
        std::shared_ptr<LayoutRetryRegistration> const& registration) {
    AcquireSRWLockExclusive(&g_layoutRetryRegistrationsLock);
    auto it = std::remove_if(
        g_layoutRetryRegistrations.begin(),
        g_layoutRetryRegistrations.end(),
        [&](const std::shared_ptr<LayoutRetryRegistration>& item) {
            return item.get() == registration.get();
        });
    g_layoutRetryRegistrations.erase(
        it, g_layoutRetryRegistrations.end());
    ReleaseSRWLockExclusive(&g_layoutRetryRegistrationsLock);
}

void DetachLayoutRetryRegistration(
        std::shared_ptr<LayoutRetryRegistration> const& registration,
        bool detachUnloaded) {
    if (!registration || registration->detached.exchange(true)) {
        return;
    }

    auto control = registration->owner.get();
    if (control) {
        if (registration->layoutAttached) {
            try {
                control.LayoutUpdated(registration->layoutToken);
            } catch (...) {
            }
            registration->layoutAttached = false;
        }
        if (detachUnloaded && registration->unloadedAttached) {
            try {
                control.Unloaded(registration->unloadedToken);
            } catch (...) {
            }
            registration->unloadedAttached = false;
        }
    }

    ClearLayoutRetryActive(registration->identity);
    UntrackLayoutRetryRegistration(registration);
}

std::vector<DWORD> SnapshotLayoutRetryThreads() {
    std::vector<DWORD> result;
    AcquireSRWLockShared(&g_layoutRetryRegistrationsLock);
    for (const auto& registration : g_layoutRetryRegistrations) {
        if (!registration || !registration->xamlThreadId) continue;
        if (std::find(result.begin(), result.end(), registration->xamlThreadId) ==
            result.end()) {
            result.push_back(registration->xamlThreadId);
        }
    }
    ReleaseSRWLockShared(&g_layoutRetryRegistrationsLock);
    return result;
}

void DetachLayoutRetriesOnCurrentThread() {
    const DWORD currentThreadId = GetCurrentThreadId();
    std::vector<std::shared_ptr<LayoutRetryRegistration>> registrations;
    AcquireSRWLockShared(&g_layoutRetryRegistrationsLock);
    registrations = g_layoutRetryRegistrations;
    ReleaseSRWLockShared(&g_layoutRetryRegistrationsLock);

    for (const auto& registration : registrations) {
        if (registration && registration->xamlThreadId == currentThreadId) {
            DetachLayoutRetryRegistration(registration, true);
        }
    }
}

void AttachLayoutRetry(Controls::UserControl const& userControl) {
    RememberXamlThread(GetCurrentThreadId(), true);
    void* identity = reinterpret_cast<void*>(winrt::get_abi(userControl));
    if (!TryMarkLayoutRetryActive(identity)) {
        ModLog(
            L"PowerOptions lazy-load retry already armed for this SystemProvider; duplicate request coalesced");
        return;
    }

    try {
        auto registration =
            std::make_shared<LayoutRetryRegistration>();
        registration->owner = winrt::make_weak(userControl);
        registration->xamlThreadId = GetCurrentThreadId();
        registration->identity = identity;

        auto weakRegistration =
            std::weak_ptr<LayoutRetryRegistration>(registration);

        registration->layoutToken = userControl.LayoutUpdated(
            [weakRegistration](auto const&, auto const&) {
                if (g_unloading.load(std::memory_order_acquire)) {
                    return;
                }
                auto registration = weakRegistration.lock();
                if (!registration) {
                    return;
                }
                auto control = registration->owner.get();
                if (!control) {
                    DetachLayoutRetryRegistration(registration, false);
                    return;
                }

                try {
                    auto root = control.Content().try_as<FrameworkElement>();
                    if (!root) {
                        return;
                    }

                    if (FindByName(root, kInjectedBatteryName) ||
                        FindByName(root, kInjectedVolumeName) ||
                        FindByName(root, kInjectedClockName)) {
                        DetachLayoutRetryRegistration(registration, true);
                        ModLog(
                            L"Lazy-load retry detached: visuals already present");
                        return;
                    }

                    auto powerButton = FindByName(root, L"PowerOptions")
                                           .try_as<Controls::Button>();
                    if (!powerButton || powerButton.ActualWidth() <= 0.0 ||
                        powerButton.ActualHeight() <= 0.0 ||
                        !FindFirstTextBlock(powerButton)) {
                        return;
                    }

                    ModLog(
                        L"Lazy-loaded PowerOptions is now realized; retrying injection");
                    InjectVisual(
                        reinterpret_cast<void*>(winrt::get_abi(control)),
                        false);

                    root = control.Content().try_as<FrameworkElement>();
                    if (root &&
                        (FindByName(root, kInjectedBatteryName) ||
                         FindByName(root, kInjectedVolumeName))) {
                        DetachLayoutRetryRegistration(registration, true);
                        ModLog(
                            L"Lazy-load retry detached after successful injection");
                    }
                } catch (...) {
                }
            });
        registration->layoutAttached = true;

        registration->unloadedToken = userControl.Unloaded(
            [weakRegistration](auto const&, auto const&) {
                auto registration = weakRegistration.lock();
                if (!registration) {
                    return;
                }
                // LogonUI can transiently unload and then reload the same
                // SystemProvider object while switching sign-in surfaces.
                // Keep the retry armed; BeforeUninit performs deterministic
                // cross-thread detachment on the owning XAML thread.
                if (g_unloading.load(std::memory_order_acquire)) {
                    return;
                }
                ModLog(
                    L"PowerOptions lazy-load retry retained across transient SystemProvider unload");
            });
        registration->unloadedAttached = true;

        TrackLayoutRetryRegistration(registration);
        ModLog(
            L"PowerOptions not realized yet; armed one coalesced LayoutUpdated lazy-load retry");
    } catch (const winrt::hresult_error& e) {
        ClearLayoutRetryActive(identity);
        ModLog(L"Failed to arm LayoutUpdated retry: 0x%08X %s",
               static_cast<unsigned>(e.code().value),
               e.message().c_str());
    } catch (...) {
        ClearLayoutRetryActive(identity);
        ModLog(
            L"Failed to arm LayoutUpdated retry with unknown exception");
    }
}


void CleanupInjectionSession(
        std::shared_ptr<InjectionSession> const& session,
        bool restoreLayout,
        bool fromOwnerUnload) {
    if (!session || session->cleaning.exchange(true)) {
        return;
    }

    auto owner = session->owner.get();
    auto grid = session->grid.get();

    // Detach callbacks owned by the persistent native UserControl first. This
    // is the critical hot-unload invariant: after this point Windows XAML no
    // longer holds delegates whose code lives inside this mod DLL.
    if (owner) {
        if (session->spacingLayoutAttached) {
            try {
                owner.LayoutUpdated(session->spacingLayoutToken);
            } catch (...) {
            }
            session->spacingLayoutAttached = false;
        }
        if (session->diagnosticsLayoutAttached) {
            try {
                owner.LayoutUpdated(session->diagnosticsLayoutToken);
            } catch (...) {
            }
            session->diagnosticsLayoutAttached = false;
        }
        if (!fromOwnerUnload && session->ownerUnloadedAttached) {
            try {
                owner.Unloaded(session->ownerUnloadedToken);
            } catch (...) {
            }
            session->ownerUnloadedAttached = false;
        } else if (fromOwnerUnload) {
            session->ownerUnloadedAttached = false;
        }
        if (session->ownerLoadedAttached) {
            try {
                owner.Loaded(session->ownerLoadedToken);
            } catch (...) {
            }
            session->ownerLoadedAttached = false;
        }
    }

    session->active.store(false, std::memory_order_release);
    StopVolumeMonitor(session->volumeState);
    session->volumeState.reset();

    DetachBatteryBrightnessWheelHandler(session->batteryWheel);
    session->batteryWheel.reset();

    DetachBatteryMiddleClickHandler(session->batteryMiddleClick);
    session->batteryMiddleClick.reset();

    if (session->clockTimer) {
        try {
            if (session->clockTickAttached) {
                session->clockTimer.Tick(session->clockTickToken);
            }
            session->clockTimer.Stop();
        } catch (...) {
        }
        session->clockTickAttached = false;
        session->clockTimer = nullptr;
    }

    if (session->batteryButton && session->batteryClickAttached) {
        try {
            session->batteryButton.Click(session->batteryClickToken);
        } catch (...) {
        }
        session->batteryClickAttached = false;
    }
    if (session->volumeButton && session->volumeClickAttached) {
        try {
            session->volumeButton.Click(session->volumeClickToken);
        } catch (...) {
        }
        session->volumeClickAttached = false;
    }
    if (session->clockButton && session->clockClickAttached) {
        try {
            session->clockButton.Click(session->clockClickToken);
        } catch (...) {
        }
        session->clockClickAttached = false;
    }

    if (restoreLayout && grid) {
        try {
            auto children = grid.Children();
            auto removeInjected = [&](Controls::Button const& button) {
                if (!button) return;
                uint32_t index = 0;
                if (children.IndexOf(button, index)) {
                    children.RemoveAt(index);
                }
            };
            removeInjected(session->batteryButton);
            removeInjected(session->volumeButton);
            removeInjected(session->clockButton);
        } catch (...) {
            ModLog(L"Hot-unload: couldn't remove one or more injected controls");
        }

        for (const auto& snapshot : session->nativeElements) {
            auto element = snapshot.element.get();
            if (!element) {
                continue;
            }
            try {
                if (snapshot.restoreColumn) {
                    Controls::Grid::SetColumn(element, snapshot.column);
                }
                if (snapshot.restoreVisibility) {
                    element.Visibility(snapshot.visibility);
                }
                if (snapshot.restoreMargin) {
                    element.Margin(snapshot.margin);
                }
                if (snapshot.restoreSizing) {
                    element.Width(snapshot.width);
                    element.MinWidth(snapshot.minWidth);
                    element.MaxWidth(snapshot.maxWidth);
                }

                if (snapshot.isControl && snapshot.restoreTabState) {
                    if (auto control = element.try_as<Controls::Control>()) {
                        control.IsTabStop(snapshot.isTabStop);
                        control.TabIndex(snapshot.tabIndex);
                    }
                }

                if (snapshot.restoreImeVisualOffset) {
                    ApplyImeModeGlyphVisualOffsetRecursive(element, 0.0);
                }
            } catch (...) {
            }
        }

        try {
            auto columns = grid.ColumnDefinitions();
            while (columns.Size() > session->originalColumnCount) {
                columns.RemoveAt(columns.Size() - 1);
            }
            const uint32_t restoreCount = std::min(
                columns.Size(),
                static_cast<uint32_t>(session->originalColumns.size()));
            for (uint32_t i = 0; i < restoreCount; ++i) {
                auto definition = columns.GetAt(i);
                const auto& snapshot = session->originalColumns[i];
                definition.Width(snapshot.width);
                definition.MinWidth(snapshot.minWidth);
                definition.MaxWidth(snapshot.maxWidth);
            }
        } catch (...) {
            ModLog(L"Hot-unload: couldn't fully restore native Grid columns");
        }

        // Do not remove/reinsert Windows-owned buttons during hot-unload.
        // That transiently fires their native Unloaded lifecycle and can make
        // LogonUX permanently drop Accessibility/Network controls from the
        // still-running SystemProvider. Grid columns + TabIndex are restored
        // in place instead.
    }

    session->batteryButton = nullptr;
    session->volumeButton = nullptr;
    session->clockButton = nullptr;
    session->nativeElements.clear();
    session->originalColumns.clear();

    UntrackInjectionSession(session);
    if (restoreLayout) {
        ModLog(L"Hot-unload: injection session detached and native layout restored");
    } else {
        ModLog(L"Injection session retired without replaying a stale native-layout snapshot");
    }
}

void CleanupInjectionSessionsOnCurrentThread(bool restoreLayout) {
    const DWORD currentThreadId = GetCurrentThreadId();
    std::vector<std::shared_ptr<InjectionSession>> sessions;
    AcquireSRWLockShared(&g_injectionSessionsLock);
    sessions = g_injectionSessions;
    ReleaseSRWLockShared(&g_injectionSessionsLock);

    for (const auto& session : sessions) {
        if (session && session->xamlThreadId == currentThreadId) {
            CleanupInjectionSession(session, restoreLayout, false);
        }
    }
}

void RetireInactiveInjectionSessionsOnThread(
        DWORD threadId,
        void* keepOwnerIdentity) {
    std::vector<std::shared_ptr<InjectionSession>> sessions;
    AcquireSRWLockShared(&g_injectionSessionsLock);
    sessions = g_injectionSessions;
    ReleaseSRWLockShared(&g_injectionSessionsLock);

    for (const auto& session : sessions) {
        if (!session || session->xamlThreadId != threadId ||
            session->ownerIdentity == keepOwnerIdentity ||
            session->active.load(std::memory_order_acquire)) {
            continue;
        }
        ModLog(L"Retiring stale inactive SystemProvider session on XAML thread=%lu owner=%p",
               threadId, session->ownerIdentity);
        CleanupInjectionSession(session, false, false);
    }
}

void InjectVisual(void* sender, bool allowLayoutRetry) {
    try {
        if (g_unloading.load(std::memory_order_acquire)) {
            return;
        }
        if (!sender) {
            ModLog(L"Loaded hook supplied a null sender");
            return;
        }

        winrt::Windows::Foundation::IUnknown unknown;
        winrt::copy_from_abi(unknown, sender);

        auto userControl = unknown.try_as<Controls::UserControl>();
        if (!userControl) {
            ModLog(L"Couldn't project SystemProviderUserControl as UserControl");
            return;
        }

        if (!IsSystemProviderUserControl(userControl)) {
            try {
                ModLog(L"Rejected non-SystemProvider UserControl class=%s",
                       winrt::get_class_name(userControl).c_str());
            } catch (...) {
                ModLog(L"Rejected non-SystemProvider UserControl");
            }
            return;
        }

        auto root = userControl.Content().try_as<FrameworkElement>();
        if (!root) {
            ModLog(L"SystemProviderUserControl content isn't a FrameworkElement");
            return;
        }

        auto powerElement = FindByName(root, L"PowerOptions");
        auto powerButton = powerElement.try_as<Controls::Button>();
        if (!powerButton) {
            ModLog(L"PowerOptions button not found; no UI change made");
            LogTree(root);
            return;
        }

        const DWORD currentXamlThreadId = GetCurrentThreadId();
        RememberXamlThread(currentXamlThreadId, true);
        void* ownerIdentity =
            reinterpret_cast<void*>(winrt::get_abi(userControl));
        RetireInactiveInjectionSessionsOnThread(
            currentXamlThreadId, ownerIdentity);

        auto existingSession =
            FindInjectionSessionForOwner(ownerIdentity);
        const bool injectedVisualPresent =
            FindByName(root, kInjectedBatteryName) ||
            FindByName(root, kInjectedVolumeName) ||
            FindByName(root, kInjectedClockName);
        if (existingSession && injectedVisualPresent) {
            existingSession->active.store(true, std::memory_order_release);
            ModLog(L"Battery/Volume visuals already exist; retained session revalidated and duplicate skipped");
            return;
        }
        if (existingSession && !injectedVisualPresent) {
            ModLog(L"SystemProvider tree was rebuilt without retained injected visuals; retiring stale session before reinjection");
            CleanupInjectionSession(existingSession, false, false);
        } else if (injectedVisualPresent) {
            ModLog(L"Battery/Volume visuals already exist without a tracked session; skipping duplicate until orphan cleanup");
            return;
        }

        auto parent = Media::VisualTreeHelper::GetParent(powerButton);
        auto parentGrid = parent.try_as<Controls::Grid>();
        if (!parentGrid) {
            ModLog(L"PowerOptions parent isn't a Grid; no UI change made");
            LogTree(root);
            return;
        }

        auto margin = powerButton.Margin();
        const double actualWidth = powerButton.ActualWidth();
        const double actualHeight = powerButton.ActualHeight();
        const int row = Controls::Grid::GetRow(powerButton);
        const int column = Controls::Grid::GetColumn(powerButton);
        const int rowSpan = Controls::Grid::GetRowSpan(powerButton);
        const int columnSpan = Controls::Grid::GetColumnSpan(powerButton);

        ModLog(L"PowerOptions geometry: size=%.1fx%.1f margin=(%.1f,%.1f,%.1f,%.1f) row=%d col=%d rowSpan=%d colSpan=%d hAlign=%d vAlign=%d",
               actualWidth,
               actualHeight,
               margin.Left,
               margin.Top,
               margin.Right,
               margin.Bottom,
               row,
               column,
               rowSpan,
               columnSpan,
               static_cast<int>(powerButton.HorizontalAlignment()),
               static_cast<int>(powerButton.VerticalAlignment()));

        if (actualWidth <= 0.0 || actualHeight <= 0.0) {
            ModLog(L"PowerOptions exists but is not rendered yet (%.1fx%.1f)",
                   actualWidth, actualHeight);
            if (allowLayoutRetry) {
                AttachLayoutRetry(userControl);
            }
            return;
        }

        auto nativePowerGlyph = FindFirstTextBlock(powerButton);
        if (!nativePowerGlyph) {
            ModLog(L"Couldn't find native PowerOptions TextBlock; PowerOptions template is not realized yet");
            if (allowLayoutRetry) {
                AttachLayoutRetry(userControl);
            }
            return;
        }

        try {
            auto ff = nativePowerGlyph.FontFamily();
            ModLog(L"Native Power glyph typography: font=%s size=%.1f text=U+%04X",
                   ff ? ff.Source().c_str() : L"-",
                   nativePowerGlyph.FontSize(),
                   nativePowerGlyph.Text().empty() ? 0 : nativePowerGlyph.Text()[0]);
        } catch (...) {
        }
        try {
            ModLog(L"Native Power keyboard focus: IsTabStop=%d TabIndex=%d",
                   powerButton.IsTabStop() ? 1 : 0,
                   powerButton.TabIndex());
        } catch (...) {
        }

        hstring nativeGlyphFontSource;
        try {
            auto ff = nativePowerGlyph.FontFamily();
            if (ff) {
                nativeGlyphFontSource = ff.Source();
            }
        } catch (...) {
        }
        const std::wstring_view nativeGlyphFontView(
            nativeGlyphFontSource.c_str(), nativeGlyphFontSource.size());
        const bool nativePowerUsesFluent =
            nativeGlyphFontView.find(L"Fluent") != std::wstring_view::npos;

        // Explicit renderer modes are intentionally unrestricted. This is a
        // Advanced setting: an explicit renderer means "try exactly this"
        // regardless of Windows version. A missing font may therefore render
        // tofu/squares, and a resource container that doesn't exist on that OS
        // may fail to load. Automatic remains the only compatibility-oriented
        // mode.
        const VolumeRendererMode rendererMode = g_volumeRenderer.load();

        bool useSndVolResources = false;
        SndVolResourceSource sndVolSource =
            SndVolResourceSource::WindowsAutomatic;
        hstring explicitFontFamily;
        bool useFluentDisabledGlyph = false;

        switch (rendererMode) {
            case VolumeRendererMode::Automatic:
                // Existing recommended behavior: the already-realized native
                // Power glyph tells us which LogonUI visual generation is in
                // use. Modern Fluent LogonUI uses Fluent font glyphs; older
                // LogonUI uses SndVolSSO through Windows resource lookup.
                if (nativePowerUsesFluent) {
                    explicitFontFamily = L"Segoe Fluent Icons";
                    useFluentDisabledGlyph = true;
                } else {
                    useSndVolResources = true;
                    sndVolSource = SndVolResourceSource::WindowsAutomatic;
                }
                break;

            case VolumeRendererMode::SegoeMdl2:
                explicitFontFamily = L"Segoe MDL2 Assets";
                useFluentDisabledGlyph = false;
                break;

            case VolumeRendererMode::SegoeFluent:
                explicitFontFamily = L"Segoe Fluent Icons";
                useFluentDisabledGlyph = true;
                break;

            case VolumeRendererMode::SndVolAutomatic:
                useSndVolResources = true;
                sndVolSource = SndVolResourceSource::WindowsAutomatic;
                break;

            case VolumeRendererMode::SndVolDll:
                useSndVolResources = true;
                sndVolSource = SndVolResourceSource::DirectDll;
                break;

            case VolumeRendererMode::SndVolMun:
                useSndVolResources = true;
                sndVolSource = SndVolResourceSource::DirectMun;
                break;
        }

        // MDL2 has no exact single-color equivalent of Win10's multicolor
        // red-X no-endpoint artwork. Fluent does have VolumeDisabled U+EA85.
        const wchar_t noEndpointGlyph = useFluentDisabledGlyph
            ? kVolumeGlyphDisabledFluent
            : kVolumeGlyphMute;

        ModLog(L"Volume renderer selected: %s (explicit modes unrestricted)",
               VolumeRendererModeName(rendererMode));

        Controls::Button batteryButton;
        batteryButton.Name(kInjectedBatteryName);
        // Participate in keyboard focus exactly like the native PowerOptions
        // button. The system-button child collection is reordered later so
        // equal/default TabIndex values follow the configured visual order.
        batteryButton.IsHitTestVisible(true);
        try {
            batteryButton.IsTabStop(powerButton.IsTabStop());
            batteryButton.TabIndex(powerButton.TabIndex());
        } catch (...) {
            batteryButton.IsTabStop(true);
        }
        batteryButton.HorizontalAlignment(powerButton.HorizontalAlignment());
        batteryButton.VerticalAlignment(powerButton.VerticalAlignment());
        try {
            Automation::AutomationProperties::SetName(
                batteryButton, hstring(L"Battery"));
        } catch (...) {
        }

        const bool automaticBatteryVisibility =
            g_batteryVisibilityAutomatic.load(std::memory_order_relaxed);
        if (automaticBatteryVisibility) {
            // Give the button a sensible initial state before the live LogonUX
            // visibility binding resolves. BatteryFlag bit 7 means that no
            // system battery is present; 255 means unknown percentage.
            SYSTEM_POWER_STATUS visibilityStatus{};
            const bool batteryPresent =
                GetSystemPowerStatus(&visibilityStatus) &&
                (visibilityStatus.BatteryFlag & 0x80) == 0 &&
                visibilityStatus.BatteryLifePercent != 255;
            batteryButton.Visibility(
                batteryPresent ? Visibility::Visible : Visibility::Collapsed);
        }

        Controls::Image batteryImage;
        batteryImage.HorizontalAlignment(HorizontalAlignment::Center);
        batteryImage.VerticalAlignment(VerticalAlignment::Center);
        batteryImage.Stretch(Media::Stretch::Uniform);
        // Match the battery artwork to the native system-button generation.
        // Win11 uses a 16-DIP Power glyph; Win10 22H2 uses a much larger
        // 30-DIP Segoe MDL2 Power glyph. Keep the accepted Win11 battery at
        // 16 DIP, but use 24 DIP for the older/larger Win10 presentation.
        double batteryIconSize = 16.0;
        try {
            if (nativePowerGlyph.FontSize() >= 24.0) {
                batteryIconSize = 24.0;
            }
        } catch (...) {
        }
        batteryImage.Width(batteryIconSize);
        batteryImage.Height(batteryIconSize);
        ModLog(L"Battery visual size selected: %.1f DIP", batteryIconSize);

        try {
            auto dataContext = userControl.DataContext();
            hstring dataContextClass;
            if (dataContext) {
                try {
                    dataContextClass = winrt::get_class_name(dataContext);
                } catch (...) {
                }
            }

            ModLog(L"SystemProvider DataContext class=%s",
                   dataContextClass.empty() ? L"<null/unknown>" : dataContextClass.c_str());

            if (dataContext) {
                const bool isSystemStatus =
                    dataContextClass.size() != 0 &&
                    wcsstr(dataContextClass.c_str(), L"SystemStatusViewModel") != nullptr;
                const bool isRootViewModel =
                    dataContextClass.size() != 0 &&
                    wcsstr(dataContextClass.c_str(), L"RootViewModel") != nullptr;

                Data::Binding batteryBinding;
                batteryBinding.Source(dataContext);
                if (isSystemStatus) {
                    batteryBinding.Path(PropertyPath(L"BatteryStatusIcon"));
                    ModLog(L"Battery binding path selected: BatteryStatusIcon");
                } else if (isRootViewModel) {
                    batteryBinding.Path(PropertyPath(L"SysViewModel.BatteryStatusIcon"));
                    ModLog(L"Battery binding path selected: SysViewModel.BatteryStatusIcon");
                } else {
                    batteryBinding.Path(PropertyPath(L"BatteryStatusIcon"));
                    ModLog(L"Battery binding path selected speculatively: BatteryStatusIcon");
                }

                batteryBinding.Mode(Data::BindingMode::OneWay);
                batteryImage.SetBinding(Controls::Image::SourceProperty(), batteryBinding);
                ModLog(L"Installed live BatteryStatusIcon -> Image.Source binding");

                if (automaticBatteryVisibility) {
                    try {
                        // Windows 10 1607+ has a built-in Boolean-to-Visibility
                        // conversion for traditional Binding. Our supported
                        // range begins at Windows 10 1809, so binding directly
                        // to LogonUX's native visibility decision avoids
                        // retaining a mod-owned IValueConverter callback.
                        Data::Binding batteryVisibilityBinding;
                        batteryVisibilityBinding.Source(dataContext);
                        if (isSystemStatus) {
                            batteryVisibilityBinding.Path(
                                PropertyPath(L"IsBatteryStatusVisible"));
                        } else if (isRootViewModel) {
                            batteryVisibilityBinding.Path(
                                PropertyPath(L"SysViewModel.IsBatteryStatusVisible"));
                        } else {
                            batteryVisibilityBinding.Path(
                                PropertyPath(L"IsBatteryStatusVisible"));
                        }
                        batteryVisibilityBinding.Mode(Data::BindingMode::OneWay);
                        batteryButton.SetBinding(
                            UIElement::VisibilityProperty(),
                            batteryVisibilityBinding);
                        ModLog(L"Installed native LogonUX IsBatteryStatusVisible -> Battery.Visibility binding");
                    } catch (const winrt::hresult_error& e) {
                        ModLog(L"Battery automatic visibility binding failed: 0x%08X %s; keeping power-status fallback",
                               static_cast<unsigned>(e.code().value),
                               e.message().c_str());
                    } catch (...) {
                        ModLog(L"Battery automatic visibility binding failed; keeping power-status fallback");
                    }
                }

                // Reuse Windows' own LogonUX BatteryStatusText rather than
                // synthesizing a percentage/status string in the mod.
                try {
                    Controls::ToolTip batteryToolTip;
                    Data::Binding batteryTextBinding;
                    batteryTextBinding.Source(dataContext);
                    if (isSystemStatus) {
                        batteryTextBinding.Path(PropertyPath(L"BatteryStatusText"));
                    } else if (isRootViewModel) {
                        batteryTextBinding.Path(PropertyPath(L"SysViewModel.BatteryStatusText"));
                    } else {
                        batteryTextBinding.Path(PropertyPath(L"BatteryStatusText"));
                    }
                    batteryTextBinding.Mode(Data::BindingMode::OneWay);
                    batteryToolTip.SetBinding(
                        Controls::ContentControl::ContentProperty(),
                        batteryTextBinding);
                    Controls::ToolTipService::SetToolTip(
                        batteryButton, batteryToolTip);
                    ModLog(L"Installed native LogonUX BatteryStatusText hover tooltip");

                    Data::Binding batteryAutomationNameBinding;
                    batteryAutomationNameBinding.Source(dataContext);
                    if (isSystemStatus) {
                        batteryAutomationNameBinding.Path(PropertyPath(L"BatteryStatusText"));
                    } else if (isRootViewModel) {
                        batteryAutomationNameBinding.Path(PropertyPath(L"SysViewModel.BatteryStatusText"));
                    } else {
                        batteryAutomationNameBinding.Path(PropertyPath(L"BatteryStatusText"));
                    }
                    batteryAutomationNameBinding.Mode(Data::BindingMode::OneWay);
                    batteryButton.SetBinding(
                        Automation::AutomationProperties::NameProperty(),
                        batteryAutomationNameBinding);
                    ModLog(L"Installed live BatteryStatusText -> AutomationProperties.Name binding");
                } catch (const winrt::hresult_error& e) {
                    ModLog(L"Battery hover tooltip binding failed: 0x%08X %s",
                           static_cast<unsigned>(e.code().value),
                           e.message().c_str());
                } catch (...) {
                    ModLog(L"Battery hover tooltip binding failed with unknown exception");
                }

                try {
                    auto resolvedSource = batteryImage.Source();
                    hstring sourceClass;
                    if (resolvedSource) {
                        try { sourceClass = winrt::get_class_name(resolvedSource); } catch (...) {}
                    }
                    ModLog(L"Resolved BatteryStatusIcon source class=%s",
                           sourceClass.empty() ? L"<null/unknown>" : sourceClass.c_str());
                } catch (...) {
                    ModLog(L"Couldn't inspect resolved BatteryStatusIcon image source");
                }
            } else {
                ModLog(L"SystemProvider DataContext is null; Battery image source left empty");
            }
        } catch (const winrt::hresult_error& e) {
            ModLog(L"Battery image binding setup failed: 0x%08X %s",
                   static_cast<unsigned>(e.code().value),
                   e.message().c_str());
        } catch (...) {
            ModLog(L"Battery image binding setup failed with unknown exception");
        }

        Controls::TextBlock batteryPercentText;
        ConfigurePercentageTextBlock(batteryPercentText);

        const auto batteryPercentagePosition =
            g_batteryPercentagePosition.load(std::memory_order_relaxed);
        const double percentageSpacing = static_cast<double>(
            g_percentageSpacing.load(std::memory_order_relaxed));
        batteryPercentText.Margin(
            batteryPercentagePosition == PercentagePosition::Left
                ? Thickness{0.0, 0.0, percentageSpacing, 0.0}
                : Thickness{percentageSpacing, 0.0, 0.0, 0.0});

        SYSTEM_POWER_STATUS initialPowerStatus{};
        const bool haveInitialBattery =
            GetSystemPowerStatus(&initialPowerStatus) &&
            initialPowerStatus.BatteryLifePercent <= 100;
        SetPercentageLabelText(
            batteryPercentText,
            haveInitialBattery
                ? FormatPercentageText(
                      static_cast<int>(
                          initialPowerStatus.BatteryLifePercent),
                      false,
                      false)
                : std::wstring{},
            g_showBatteryPercentage.load(std::memory_order_relaxed));

        Controls::StackPanel batteryVisual;
        batteryVisual.Orientation(Controls::Orientation::Horizontal);
        batteryVisual.HorizontalAlignment(HorizontalAlignment::Center);
        batteryVisual.VerticalAlignment(VerticalAlignment::Center);
        if (batteryPercentagePosition == PercentagePosition::Left) {
            batteryVisual.Children().Append(batteryPercentText);
            batteryVisual.Children().Append(batteryImage);
        } else {
            batteryVisual.Children().Append(batteryImage);
            batteryVisual.Children().Append(batteryPercentText);
        }

        batteryButton.Content(batteryVisual);
        CopySafeVisualProperties(powerButton, batteryButton);

        if (g_showBatteryPercentage.load(std::memory_order_relaxed)) {
            AllowInjectedButtonToDynamicallySizeForPercentage(
                batteryButton,
                actualWidth);
            g_batteryPercentText = winrt::make_weak(batteryPercentText);
            EnsureBatteryUiBridgeWindow();
            EnsureBatteryNotificationWatcherStarted();
        }

        auto batteryClickToken = batteryButton.Click(RoutedEventHandler(
            [](winrt::Windows::Foundation::IInspectable const& sender,
               RoutedEventArgs const&) {
                if (g_unloading.load(std::memory_order_acquire)) {
                    return;
                }
                auto element = sender.try_as<FrameworkElement>();
                if (!element) {
                    ModLog(L"Battery click sender isn't a FrameworkElement; refusing classic flyout");
                    return;
                }
                ShowClassicBatteryFlyout(element);
            }));

        Controls::Button volumeButton;
        volumeButton.Name(kInjectedVolumeName);
        volumeButton.IsHitTestVisible(true);
        try {
            volumeButton.IsTabStop(powerButton.IsTabStop());
            volumeButton.TabIndex(powerButton.TabIndex());
        } catch (...) {
            volumeButton.IsTabStop(true);
        }
        volumeButton.HorizontalAlignment(powerButton.HorizontalAlignment());
        volumeButton.VerticalAlignment(powerButton.VerticalAlignment());
        try {
            Automation::AutomationProperties::SetName(
                volumeButton, hstring(L"Volume"));
        } catch (...) {
        }

        Controls::TextBlock volumeIcon;
        wchar_t initialVolumeText[2]{noEndpointGlyph, L'\0'};
        volumeIcon.Text(initialVolumeText); // Unavailable state until the first live Core Audio read.
        CopyNativeGlyphTypography(nativePowerGlyph, volumeIcon);
        if (!explicitFontFamily.empty()) {
            try {
                volumeIcon.FontFamily(Media::FontFamily(explicitFontFamily));
                ModLog(L"Volume font family forced to %s",
                       explicitFontFamily.c_str());
            } catch (const winrt::hresult_error& e) {
                ModLog(L"Couldn't apply requested Volume font family %s: 0x%08X %s",
                       explicitFontFamily.c_str(),
                       static_cast<unsigned>(e.code().value),
                       e.message().c_str());
            } catch (...) {
                ModLog(L"Couldn't apply requested Volume font family %s",
                       explicitFontFamily.c_str());
            }
        }

        Controls::Grid volumeIconHost;
        volumeIconHost.HorizontalAlignment(HorizontalAlignment::Center);
        volumeIconHost.VerticalAlignment(VerticalAlignment::Center);
        volumeIconHost.Children().Append(volumeIcon);

        Controls::TextBlock volumePercentText;
        ConfigurePercentageTextBlock(volumePercentText);

        const auto volumePercentagePosition =
            g_volumePercentagePosition.load(std::memory_order_relaxed);
        const double volumePercentageSpacing = static_cast<double>(
            g_percentageSpacing.load(std::memory_order_relaxed));
        volumePercentText.Margin(
            volumePercentagePosition == PercentagePosition::Left
                ? Thickness{0.0, 0.0, volumePercentageSpacing, 0.0}
                : Thickness{volumePercentageSpacing, 0.0, 0.0, 0.0});
        volumePercentText.Text(L"");
        volumePercentText.Visibility(Visibility::Collapsed);

        Controls::StackPanel volumeVisual;
        volumeVisual.Orientation(Controls::Orientation::Horizontal);
        volumeVisual.HorizontalAlignment(HorizontalAlignment::Center);
        volumeVisual.VerticalAlignment(VerticalAlignment::Center);
        if (volumePercentagePosition == PercentagePosition::Left) {
            volumeVisual.Children().Append(volumePercentText);
            volumeVisual.Children().Append(volumeIconHost);
        } else {
            volumeVisual.Children().Append(volumeIconHost);
            volumeVisual.Children().Append(volumePercentText);
        }

        NativeSndVolVisuals nativeSndVol{};
        if (useSndVolResources) {
            nativeSndVol.muted = LoadSndVolGroupBitmap(kSndVolGroupMute, sndVolSource);
            nativeSndVol.zero = LoadSndVolGroupBitmap(kSndVolGroup0, sndVolSource);
            nativeSndVol.low = LoadSndVolGroupBitmap(kSndVolGroup1, sndVolSource);
            nativeSndVol.medium = LoadSndVolGroupBitmap(kSndVolGroup2, sndVolSource);
            nativeSndVol.high = LoadSndVolGroupBitmap(kSndVolGroup3, sndVolSource);
            nativeSndVol.noEndpoint = LoadSndVolGroupBitmap(kSndVolGroupNoEndpoint, sndVolSource);

            if (nativeSndVol.muted && nativeSndVol.zero &&
                nativeSndVol.low && nativeSndVol.medium &&
                nativeSndVol.high && nativeSndVol.noEndpoint) {
                nativeSndVol.image = Controls::Image();
                nativeSndVol.image.Source(nativeSndVol.noEndpoint);
                nativeSndVol.image.Stretch(Media::Stretch::Uniform);
                nativeSndVol.image.HorizontalAlignment(
                    HorizontalAlignment::Center);
                nativeSndVol.image.VerticalAlignment(
                    VerticalAlignment::Center);

                double nativeImageSize = 30.0;
                try {
                    nativeImageSize = nativePowerGlyph.FontSize();
                } catch (...) {
                }
                nativeSndVol.image.Width(nativeImageSize);
                nativeSndVol.image.Height(nativeImageSize);
                nativeSndVol.image.Visibility(Visibility::Visible);
                volumeIcon.Visibility(Visibility::Collapsed);
                volumeIconHost.Children().Append(nativeSndVol.image);

                ModLog(
                    L"Volume visual uses SndVolSSO groups 120-125 at %.1f DIP (source mode=%d)",
                    nativeImageSize,
                    static_cast<int>(sndVolSource));
            } else {
                nativeSndVol = {};
                ModLog(
                    L"Requested SndVolSSO resource set incomplete; falling back to the selected/native Power font glyph path");
            }
        }

        // ToolTipService gives us the platform XAML tooltip presentation;
        // the content is kept live by the Core Audio monitor. The Button style
        // itself is still cloned from PowerOptions for LogonUI-native hover.
        Controls::ToolTip volumeToolTip;
        volumeToolTip.Content(box_value(hstring(L"Volume")));
        Controls::ToolTipService::SetToolTip(volumeButton, volumeToolTip);

        volumeButton.Content(volumeVisual);
        CopySafeVisualProperties(powerButton, volumeButton);

        if (g_showVolumePercentage.load(std::memory_order_relaxed)) {
            AllowInjectedButtonToDynamicallySizeForPercentage(
                volumeButton,
                actualWidth);
        }

        // Use the real classic SndVol executable instead of recreating its UI.
        // Parameters are intentionally user-controlled; the mod doesn't append
        // a hard-coded coordinate because current SndVol builds can ignore the
        // historical packed-position argument.
        auto volumeClickToken = volumeButton.Click(RoutedEventHandler(
            [](winrt::Windows::Foundation::IInspectable const& sender,
               RoutedEventArgs const&) {
                if (g_unloading.load(std::memory_order_acquire)) {
                    return;
                }
                auto element = sender.try_as<FrameworkElement>();
                if (!element) {
                    ModLog(L"Volume click sender isn't a FrameworkElement; refusing SndVol launch");
                    return;
                }
                LaunchNativeSndVol(element);
            }));


        Controls::Button clockButton;
        clockButton.Name(kInjectedClockName);
        clockButton.IsHitTestVisible(true);
        try {
            clockButton.IsTabStop(powerButton.IsTabStop());
            clockButton.TabIndex(powerButton.TabIndex());
        } catch (...) {
            clockButton.IsTabStop(true);
        }
        clockButton.HorizontalAlignment(powerButton.HorizontalAlignment());
        clockButton.VerticalAlignment(powerButton.VerticalAlignment());
        try {
            Automation::AutomationProperties::SetName(
                clockButton, hstring(L"Date and time"));
        } catch (...) {
        }

        Controls::TextBlock clockText;
        try {
            clockText.FontFamily(Media::FontFamily(L"Segoe UI"));
        } catch (...) {
        }
        clockText.FontSize(12.0);
        clockText.HorizontalAlignment(HorizontalAlignment::Center);
        clockText.VerticalAlignment(VerticalAlignment::Center);
        clockText.TextAlignment(TextAlignment::Center);
        clockText.Text(FormatClockText(
            g_clockShowSeconds.load(std::memory_order_relaxed),
            g_clockShowDate.load(std::memory_order_relaxed)));

        Controls::ToolTip clockToolTip;
        clockToolTip.Content(box_value(hstring(L"Date and time")));
        Controls::ToolTipService::SetToolTip(clockButton, clockToolTip);

        clockButton.Content(clockText);
        CopySafeVisualProperties(powerButton, clockButton);
        try {
            clockButton.MinWidth(actualWidth);
        } catch (...) {
        }

        auto weakClockButton = winrt::make_weak(clockButton);
        auto clockClickToken = clockButton.Click(RoutedEventHandler(
            [weakClockButton](winrt::Windows::Foundation::IInspectable const&,
               RoutedEventArgs const&) {
                if (g_unloading.load(std::memory_order_acquire)) {
                    return;
                }

                const auto action =
                    g_clockClickAction.load(std::memory_order_relaxed);
                if (action == ClockClickAction::ClassicFlyout) {
                    auto button = weakClockButton.get();
                    if (button) {
                        ShowClassicClockFlyout(button);
                    }
                } else if (action == ClockClickAction::DateTimeDialog) {
                    auto button = weakClockButton.get();
                    if (button) {
                        LaunchClassicDateTimeDialog(button);
                    }
                } else {
                    ModLog(L"Clock click ignored by setting");
                }
            }));

        // PowerOptions is expected to occupy the rightmost Grid column on the
        // tested builds. We still extend by two Power-sized columns, but the
        // final Grid.Column assignment for every system button is determined by
        // the user-configurable left-to-right order.
        auto columns = parentGrid.ColumnDefinitions();
        const uint32_t columnCount = columns.Size();
        ModLog(L"System grid column count=%u; PowerOptions column=%d",
               columnCount, column);

        if (column < 0 || static_cast<uint32_t>(column) >= columnCount ||
            static_cast<uint32_t>(column + 1) != columnCount) {
            ModLog(L"PowerOptions is not in the rightmost Grid column; refusing layout mutation");
            LogTree(root);
            return;
        }

        std::vector<ColumnSizingSnapshot> originalColumnSizing;
        originalColumnSizing.reserve(columns.Size());
        for (uint32_t i = 0; i < columns.Size(); ++i) {
            auto definition = columns.GetAt(i);
            originalColumnSizing.push_back(
                {definition.Width(),
                 definition.MinWidth(),
                 definition.MaxWidth()});
        }

        auto nativeGroups = DiscoverNativeSystemButtonGroups(
            parentGrid, powerButton, row, column);
        if (nativeGroups.empty()) {
            ModLog(L"Couldn't discover the contiguous native system-button strip; refusing layout mutation");
            LogTree(root);
            return;
        }
        LogNativeSystemButtonGroups(nativeGroups);
        LogNativeSystemButtonSemanticDiagnostics(nativeGroups);

        // Capture a complete-enough native layout snapshot before the first
        // mutation. Hot-disable can then remove our controls and return the
        // still-running LogonUI instance to its pre-injection state.
        auto session = std::make_shared<InjectionSession>();
        session->owner = winrt::make_weak(userControl);
        session->grid = winrt::make_weak(parentGrid);
        session->xamlThreadId = GetCurrentThreadId();
        session->ownerIdentity =
            reinterpret_cast<void*>(winrt::get_abi(userControl));
        session->active.store(true, std::memory_order_release);
        session->originalColumnCount = columnCount;
        session->originalColumns = originalColumnSizing;
        session->batteryButton = batteryButton;
        session->volumeButton = volumeButton;
        session->clockButton = clockButton;
        session->batteryClickToken = batteryClickToken;
        session->volumeClickToken = volumeClickToken;
        session->clockClickToken = clockClickToken;
        session->batteryClickAttached = true;
        session->volumeClickAttached = true;
        session->clockClickAttached = true;

        std::vector<void*> snappedNativeIdentities;
        for (const auto& group : nativeGroups) {
            for (const auto& element : group.elements) {
                if (!element) continue;
                void* identity =
                    reinterpret_cast<void*>(winrt::get_abi(element));
                if (std::find(snappedNativeIdentities.begin(),
                              snappedNativeIdentities.end(),
                              identity) != snappedNativeIdentities.end()) {
                    continue;
                }
                snappedNativeIdentities.push_back(identity);

                NativeElementSnapshot snapshot;
                snapshot.element = winrt::make_weak(element);
                snapshot.groupKey = group.key;
                snapshot.column = Controls::Grid::GetColumn(element);
                const bool groupEnabledAtInjection =
                    IsSystemButtonGroupEnabled(group.key);
                snapshot.restoreColumn = groupEnabledAtInjection;
                snapshot.restoreVisibility = !groupEnabledAtInjection;
                snapshot.restoreMargin =
                    group.key == L"ime" || group.key == L"input";
                snapshot.restoreSizing = snapshot.restoreMargin;
                snapshot.restoreTabState = true;
                snapshot.restoreImeVisualOffset = group.key == L"ime";
                try { snapshot.visibility = element.Visibility(); } catch (...) {}
                try { snapshot.margin = element.Margin(); } catch (...) {}
                try { snapshot.width = element.Width(); } catch (...) {}
                try { snapshot.minWidth = element.MinWidth(); } catch (...) {}
                try { snapshot.maxWidth = element.MaxWidth(); } catch (...) {}
                try {
                    if (auto control = element.try_as<Controls::Control>()) {
                        snapshot.isControl = true;
                        snapshot.isTabStop = control.IsTabStop();
                        snapshot.tabIndex = control.TabIndex();
                    }
                } catch (...) {
                }
                session->nativeElements.push_back(std::move(snapshot));
            }
        }

        TrackInjectionSession(session);
        try {
            auto weakSession = std::weak_ptr<InjectionSession>(session);
            session->ownerUnloadedToken = userControl.Unloaded(
                [weakSession](auto const&, auto const&) {
                    auto session = weakSession.lock();
                    if (!session) return;

                    // Unloaded is not terminal in LogonUI. The same native
                    // SystemProvider can be reinserted into a XAML tree after
                    // icon/flyout/security-surface transitions. Preserve all
                    // mod-owned state and let Loaded revalidate it.
                    if (g_unloading.load(std::memory_order_acquire)) {
                        ModLog(L"SystemProvider Unloaded during BeforeUninit; lifecycle state left untouched for owning-thread hot cleanup");
                        return;
                    }
                    session->active.store(false, std::memory_order_release);
                    UserStatus::OnSystemProviderUnloaded();
                    ModLog(L"SystemProvider transiently Unloaded; retaining injection session and callbacks for reload");
                });
            session->ownerUnloadedAttached = true;

            session->ownerLoadedToken = userControl.Loaded(
                [weakSession](auto const&, auto const&) {
                    auto session = weakSession.lock();
                    if (!session ||
                        g_unloading.load(std::memory_order_acquire)) {
                        return;
                    }

                    RememberXamlThread(GetCurrentThreadId(), true);
                    auto owner = session->owner.get();
                    if (!owner || !IsSystemProviderUserControl(owner)) {
                        return;
                    }

                    try {
                        auto root = owner.Content().try_as<FrameworkElement>();
                        FrameworkElement power{nullptr};
                        if (root) {
                            power = FindByName(root, L"PowerOptions");
                        }
                        const bool batteryExpected =
                            g_showBatteryButton.load(std::memory_order_relaxed);
                        const bool volumeExpected =
                            g_showVolumeButton.load(std::memory_order_relaxed);
                        const bool clockExpected =
                            g_showClockButton.load(std::memory_order_relaxed);
                        const bool batteryPresent = root &&
                            FindByName(root, kInjectedBatteryName);
                        const bool volumePresent = root &&
                            FindByName(root, kInjectedVolumeName);
                        const bool clockPresent = root &&
                            FindByName(root, kInjectedClockName);
                        const bool retainedVisualsValid =
                            power &&
                            (!batteryExpected || batteryPresent) &&
                            (!volumeExpected || volumePresent) &&
                            (!clockExpected || clockPresent);

                        if (retainedVisualsValid) {
                            session->active.store(
                                true, std::memory_order_release);
                            ModLog(L"SystemProvider Loaded revalidated retained injection session on XAML thread=%lu",
                                   GetCurrentThreadId());

                            // The native C++ _OnLoaded handler isn't guaranteed
                            // to run again when the same XAML SystemProvider is
                            // detached for the lock-screen surface and later
                            // reinserted. Re-dispatch the user-list lifecycle
                            // explicitly so status/qualified-name rows are
                            // repaired after every XAML Loaded transition.
                            UserStatus::OnSystemProviderLoaded(
                                reinterpret_cast<void*>(winrt::get_abi(owner)));
                            return;
                        }

                        ModLog(L"SystemProvider Loaded with rebuilt tree; retiring stale retained session and reinjecting");
                        CleanupInjectionSession(session, false, false);
                        InjectVisual(
                            reinterpret_cast<void*>(winrt::get_abi(owner)),
                            true);
                        UserStatus::OnSystemProviderLoaded(
                            reinterpret_cast<void*>(winrt::get_abi(owner)));
                    } catch (const winrt::hresult_error& e) {
                        ModLog(L"SystemProvider Loaded revalidation failed: 0x%08X %s",
                               static_cast<unsigned>(e.code().value),
                               e.message().c_str());
                    } catch (...) {
                        ModLog(L"SystemProvider Loaded revalidation failed with unknown exception");
                    }
                });
            session->ownerLoadedAttached = true;
        } catch (...) {
            ModLog(L"Couldn't attach SystemProvider lifecycle revalidation callbacks");
        }

        int firstSystemColumn = nativeGroups.front().originalColumn;
        for (const auto& group : nativeGroups) {
            firstSystemColumn = std::min(firstSystemColumn, group.originalColumn);
        }

        auto sourceColumn = columns.GetAt(static_cast<uint32_t>(column));
        auto appendClonedColumn = [&](bool allowContentWidth) -> bool {
            Controls::ColumnDefinition newColumn;
            try {
                if (allowContentWidth) {
                    // Auto-width columns must be allowed to collapse to zero.
                    // The injected Button itself keeps the native minimum width
                    // while visible, so this also avoids a blank gap when
                    // Automatic Battery visibility collapses the control.
                    newColumn.Width(GridLengthHelper::Auto());
                    newColumn.MinWidth(0.0);
                } else {
                    newColumn.Width(sourceColumn.Width());
                    newColumn.MinWidth(sourceColumn.MinWidth());
                    newColumn.MaxWidth(sourceColumn.MaxWidth());
                }
                columns.Append(newColumn);
                return true;
            } catch (...) {
                return false;
            }
        };

        const bool showBattery =
            g_showBatteryButton.load(std::memory_order_relaxed);
        const bool showVolume =
            g_showVolumeButton.load(std::memory_order_relaxed);
        const bool showClock =
            g_showClockButton.load(std::memory_order_relaxed);
        const bool showBatteryPercentage =
            g_showBatteryPercentage.load(std::memory_order_relaxed);
        const bool showVolumePercentage =
            g_showVolumePercentage.load(std::memory_order_relaxed);

        if (showBattery &&
            !appendClonedColumn(
                showBatteryPercentage ||
                g_batteryVisibilityAutomatic.load(std::memory_order_relaxed))) {
            ModLog(L"Couldn't create Battery column sizing; refusing layout mutation");
            CleanupInjectionSession(session, true, false);
            return;
        }
        if (showVolume &&
            !appendClonedColumn(showVolumePercentage)) {
            ModLog(L"Couldn't create Volume column sizing; refusing layout mutation");
            CleanupInjectionSession(session, true, false);
            return;
        }
        if (showClock &&
            !appendClonedColumn(true)) {
            ModLog(L"Couldn't create Clock column sizing; refusing layout mutation");
            CleanupInjectionSession(session, true, false);
            return;
        }

        Controls::Grid::SetRow(batteryButton, row);
        Controls::Grid::SetRowSpan(batteryButton, rowSpan);
        Controls::Grid::SetColumnSpan(batteryButton, columnSpan);

        Controls::Grid::SetRow(volumeButton, row);
        Controls::Grid::SetRowSpan(volumeButton, rowSpan);
        Controls::Grid::SetColumnSpan(volumeButton, columnSpan);

        Controls::Grid::SetRow(clockButton, row);
        Controls::Grid::SetRowSpan(clockButton, rowSpan);
        Controls::Grid::SetColumnSpan(clockButton, columnSpan);

        auto children = parentGrid.Children();

        std::vector<SystemButtonGroup> allGroups = nativeGroups;

        if (showBattery) {
            children.Append(batteryButton);

            SystemButtonGroup batteryGroup;
            batteryGroup.key = L"battery";
            batteryGroup.originalColumn = column;
            batteryGroup.elements.push_back(batteryButton);
            allGroups.push_back(std::move(batteryGroup));
        }

        if (showVolume) {
            children.Append(volumeButton);

            SystemButtonGroup volumeGroup;
            volumeGroup.key = L"volume";
            volumeGroup.originalColumn = column + (showBattery ? 1 : 0);
            volumeGroup.elements.push_back(volumeButton);
            allGroups.push_back(std::move(volumeGroup));
        }

        if (showClock) {
            children.Append(clockButton);

            SystemButtonGroup clockGroup;
            clockGroup.key = L"clock";
            clockGroup.originalColumn =
                column + (showBattery ? 1 : 0) + (showVolume ? 1 : 0);
            clockGroup.elements.push_back(clockButton);
            allGroups.push_back(std::move(clockGroup));
        }

        auto requestedOrder = ParseButtonOrderSetting(GetButtonOrderSetting());
        std::vector<size_t> orderedIndices;

        auto findGroup = [&](const std::wstring& key) -> size_t {
            for (size_t i = 0; i < allGroups.size(); ++i) {
                if (allGroups[i].key == key) return i;
            }
            return static_cast<size_t>(-1);
        };
        auto alreadyOrdered = [&](size_t index) {
            return std::find(orderedIndices.begin(), orderedIndices.end(), index) !=
                   orderedIndices.end();
        };

        std::vector<bool> groupEnabled(
            allGroups.size(), true);
        size_t enabledGroupCount = 0;
        for (size_t i = 0; i < allGroups.size(); ++i) {
            const auto& group = allGroups[i];
            bool enabled = true;

            if (group.key == L"battery") {
                enabled = showBattery;
            } else if (group.key == L"volume") {
                enabled = showVolume;
            } else if (group.key == L"clock") {
                enabled = showClock;
            } else {
                enabled = IsSystemButtonGroupEnabled(group.key);
            }

            groupEnabled[i] = enabled;
            if (enabled) {
                ++enabledGroupCount;
                continue;
            }

            for (const auto& element : group.elements) {
                if (!element) continue;
                try {
                    element.Visibility(Visibility::Collapsed);
                } catch (...) {
                }
                try {
                    if (auto control = element.try_as<Controls::Control>()) {
                        control.IsTabStop(false);
                    }
                } catch (...) {
                }
            }

            ModLog(L"System button hidden by setting: %s",
                   group.key.c_str());
        }

        for (const auto& key : requestedOrder) {
            const size_t index = findGroup(key);
            if (index == static_cast<size_t>(-1)) {
                if (key == L"ime" &&
                    findGroup(L"input") != static_cast<size_t>(-1)) {
                    ModLog(L"Standalone IME mode container isn't present on this LogonUI layout; current IME/input state is handled by InputSwitchUserControl, so the 'ime' order token is skipped");
                } else {
                    ModLog(L"Button-order token isn't present on this LogonUI instance: %s",
                           key.c_str());
                }
                continue;
            }
            if (!groupEnabled[index]) {
                continue;
            }
            if (!alreadyOrdered(index)) orderedIndices.push_back(index);
        }

        // A malformed/partial setting never hides a button. First honor the
        // requested named order exactly; then append anything that wasn't
        // explicitly named. Unknown native slots therefore can't jump ahead of
        // the user's battery/volume/network ordering.
        static constexpr const wchar_t* kFallbackOrder[] = {
            L"battery", L"volume", L"clock", L"ime", L"input",
            L"network-signin", L"disconnect", L"network",
            L"accessibility", L"power"
        };
        for (const wchar_t* key : kFallbackOrder) {
            const size_t index = findGroup(key);
            if (index != static_cast<size_t>(-1) &&
                groupEnabled[index] &&
                !alreadyOrdered(index)) {
                orderedIndices.push_back(index);
            }
        }
        for (size_t i = 0; i < allGroups.size(); ++i) {
            if (groupEnabled[i] && !alreadyOrdered(i)) {
                orderedIndices.push_back(i);
            }
        }

        if (orderedIndices.size() != enabledGroupCount) {
            ModLog(L"Button-order internal mismatch; refusing column reassignment");
            CleanupInjectionSession(session, true, false);
            return;
        }

        auto applyColumnSizing =
            [&](uint32_t targetColumn,
                const SystemButtonGroup& group) {
                if (targetColumn >= columns.Size()) {
                    return;
                }

                auto target = columns.GetAt(targetColumn);

                const bool isBattery = group.key == L"battery";
                const bool isVolume = group.key == L"volume";
                const bool isClock = group.key == L"clock";
                const bool dynamicInjected =
                    (isBattery && showBatteryPercentage) ||
                    (isVolume && showVolumePercentage) ||
                    isClock;
                const bool dynamicNative =
                    group.key == L"ime" || group.key == L"input" ||
                    group.key == L"network-signin" ||
                    group.key == L"disconnect";

                try {
                    if (dynamicInjected) {
                        target.Width(GridLengthHelper::Auto());
                        target.MinWidth(sourceColumn.MinWidth());
                        target.MaxWidth(
                            std::numeric_limits<double>::infinity());
                        return;
                    }

                    // Optional/lazy-loaded native controls must not reserve a
                    // blank 40-DIP slot while hidden, but must be able to grow
                    // when Windows materializes them later.
                    if (dynamicNative) {
                        target.Width(GridLengthHelper::Auto());
                        target.MinWidth(0.0);
                        target.MaxWidth(
                            std::numeric_limits<double>::infinity());
                        return;
                    }

                    if (!isBattery && !isVolume && !isClock &&
                        group.originalColumn >= 0 &&
                        static_cast<size_t>(group.originalColumn) <
                            originalColumnSizing.size()) {
                        const auto& snapshot =
                            originalColumnSizing[
                                static_cast<size_t>(
                                    group.originalColumn)];
                        target.Width(snapshot.width);
                        target.MinWidth(snapshot.minWidth);
                        target.MaxWidth(snapshot.maxWidth);
                        return;
                    }

                    target.Width(sourceColumn.Width());
                    target.MinWidth(sourceColumn.MinWidth());
                    target.MaxWidth(sourceColumn.MaxWidth());
                } catch (...) {
                }
            };

        for (size_t slot = 0; slot < orderedIndices.size(); ++slot) {
            const auto& group = allGroups[orderedIndices[slot]];
            const int targetColumn =
                firstSystemColumn + static_cast<int>(slot);
            for (const auto& element : group.elements) {
                AllowLazyNativeContainerToDynamicallySize(
                    element, group.key);
                Controls::Grid::SetColumn(element, targetColumn);
            }
            applyColumnSizing(
                static_cast<uint32_t>(targetColumn), group);
            ModLog(L"Button order applied: %s -> col=%d (elementCount=%u)",
                   group.key.c_str(),
                   targetColumn,
                   static_cast<unsigned>(group.elements.size()));
        }

        // Collapse every now-unused slot at the right edge of the system
        // strip, so hiding native controls removes their layout space instead
        // of leaving a blank Power-sized gap.
        const uint32_t usedEnd =
            static_cast<uint32_t>(
                firstSystemColumn +
                static_cast<int>(orderedIndices.size()));
        for (uint32_t colIndex = usedEnd;
             colIndex < columns.Size();
             ++colIndex) {
            auto definition = columns.GetAt(colIndex);
            try {
                definition.MinWidth(0.0);
                definition.MaxWidth(0.0);
                definition.Width(GridLengthHelper::FromPixels(0.0));
            } catch (...) {
            }
        }

        // Keep Windows-owned XAML elements attached to their parent. Earlier
        // versions reordered the Children collection with RemoveAt/InsertAt to
        // influence Tab traversal. On a persistent hot-loaded LogonUI that
        // transiently fires native Unloaded/Loaded handlers and can make
        // Windows-owned buttons disappear. Use TabIndex in-place instead.
        for (size_t slot = 0; slot < orderedIndices.size(); ++slot) {
            const auto& group = allGroups[orderedIndices[slot]];
            for (const auto& element : group.elements) {
                try {
                    if (auto control = element.try_as<Controls::Control>()) {
                        control.TabIndex(static_cast<int>(slot));
                    }
                } catch (...) {
                }
            }
        }
        ModLog(L"Native TabIndex aligned with configured visual order without reparenting Windows-owned controls");

        // IME/Input are native UserControl containers that normally sit at the
        // left edge of the Windows strip and therefore don't carry the ordinary
        // left gap themselves. If the user moves one after a different visible
        // control, apply the configured external spacing only while rendered.
        // When IME and Input are adjacent to each other, keep their native pair
        // spacing (no second injected gap between the two containers). Dormant
        // containers remain margin-free so they still collapse completely.
        if (!orderedIndices.empty()) {
            const int configuredImeInputSpacing =
                g_nativeImeInputSpacing.load(std::memory_order_relaxed);
            const double resolvedImeInputSpacing =
                configuredImeInputSpacing < 0
                    ? std::max(0.0, margin.Left)
                    : static_cast<double>(configuredImeInputSpacing);

            ModLog(
                L"Resolved IME/Input external spacing: %.1f DIP (%s; native Power left margin=%.1f)",
                resolvedImeInputSpacing,
                configuredImeInputSpacing < 0 ? L"automatic" : L"explicit",
                margin.Left);

            AttachDynamicLazyNativeSpacing(
                userControl,
                parentGrid,
                firstSystemColumn,
                static_cast<int>(usedEnd),
                resolvedImeInputSpacing,
                session);
        }

        // Attach routed input handlers only after the child-order rewrite.
        // RemoveAt/InsertAt transiently unloads the injected Button, so an
        // earlier Unloaded cleanup would otherwise detach the Battery wheel
        // handler before the user can ever scroll it.
        if (showBattery) {
            session->batteryWheel =
                AttachBatteryBrightnessWheelHandler(batteryButton);
            session->batteryMiddleClick =
                AttachBatteryMiddleClickHandler(batteryButton);
        }

        if (showVolume) {
            session->volumeState = StartVolumeMonitor(
                userControl,
                volumeButton,
                volumeIcon,
                volumePercentText,
                volumeToolTip,
                noEndpointGlyph,
                nativeSndVol);
        }

        if (showClock) {
            try {
                DispatcherTimer timer;
                timer.Interval(std::chrono::seconds(1));
                auto weakClockText = winrt::make_weak(clockText);
                session->clockTickToken = timer.Tick(
                    [weakClockText](auto const&, auto const&) {
                        if (g_unloading.load(std::memory_order_acquire)) {
                            return;
                        }
                        auto textBlock = weakClockText.get();
                        if (!textBlock) {
                            return;
                        }
                        try {
                            const auto next = FormatClockText(
                                g_clockShowSeconds.load(
                                    std::memory_order_relaxed),
                                g_clockShowDate.load(
                                    std::memory_order_relaxed));
                            if (textBlock.Text() != next) {
                                textBlock.Text(next);
                            }
                        } catch (...) {
                        }
                    });
                session->clockTickAttached = true;
                session->clockTimer = timer;
                timer.Start();
                ModLog(L"Clock timer started (1 s tick; taskbar-style time/date text updated only when formatted value changes)");
            } catch (const winrt::hresult_error& e) {
                ModLog(L"Couldn't start Clock timer: 0x%08X %s",
                       static_cast<unsigned>(e.code().value),
                       e.message().c_str());
            } catch (...) {
                ModLog(L"Couldn't start Clock timer");
            }
        }

        if (!orderedIndices.empty()) {
            ModLog(L"Inserted configurable Battery/Volume/Clock controls; battery=%d volume=%d clock=%d visible system strip spans cols=%d..%d",
                   showBattery ? 1 : 0,
                   showVolume ? 1 : 0,
                   showClock ? 1 : 0,
                   firstSystemColumn,
                   firstSystemColumn +
                       static_cast<int>(orderedIndices.size()) - 1);
        } else {
            ModLog(L"All configurable system buttons are hidden; system strip columns collapsed");
        }

        if (g_enableXamlCapture.load(std::memory_order_relaxed)) {
            if (showBattery) {
                ScheduleXamlSubtreeCapture(userControl, batteryImage);
            } else {
                ModLog(L"Diagnostic XAML capture skipped because Battery button is hidden");
            }
        } else {
            ModLog(L"Diagnostic XAML capture disabled");
        }

        // Windows 11 can materialize the Input/language control only after a
        // credential field is activated. Attach the dynamic diagnostic watcher
        // only for verbose RE logging or XAML capture; functional IME/Input
        // spacing has its own independent watcher.
        AttachDynamicSystemProviderDiagnostics(
            userControl, batteryImage, session);
    } catch (const winrt::hresult_error& e) {
        ModLog(L"Injection failed: 0x%08X %s",
               static_cast<unsigned>(e.code().value),
               e.message().c_str());
    } catch (...) {
        ModLog(L"Injection failed with unknown exception");
    }
}

struct LateAttachWindowCandidate {
    HWND hwnd = nullptr;
    DWORD threadId = 0;
    unsigned long long area = 0;
};

BOOL CALLBACK EnumLateAttachWindow(HWND hwnd, LPARAM lParam) {
    auto* candidates =
        reinterpret_cast<std::vector<LateAttachWindowCandidate>*>(lParam);
    if (!candidates) {
        return TRUE;
    }

    DWORD processId = 0;
    DWORD threadId = GetWindowThreadProcessId(hwnd, &processId);
    if (!threadId || processId != GetCurrentProcessId()) {
        return TRUE;
    }

    RECT rect{};
    unsigned long long area = 0;
    if (GetWindowRect(hwnd, &rect)) {
        const LONG width = rect.right - rect.left;
        const LONG height = rect.bottom - rect.top;
        if (width > 0 && height > 0) {
            area = static_cast<unsigned long long>(width) *
                   static_cast<unsigned long long>(height);
        }
    }

    auto it = std::find_if(
        candidates->begin(), candidates->end(),
        [threadId](const LateAttachWindowCandidate& candidate) {
            return candidate.threadId == threadId;
        });
    if (it == candidates->end()) {
        candidates->push_back({hwnd, threadId, area});
    } else if (!it->hwnd || area > it->area) {
        it->hwnd = hwnd;
        it->area = area;
    }

    return TRUE;
}

std::vector<LateAttachWindowCandidate> CollectUiThreadCandidates(
        bool includeKnownXamlThreads) {
    std::vector<LateAttachWindowCandidate> candidates;
    EnumWindows(EnumLateAttachWindow,
                reinterpret_cast<LPARAM>(&candidates));

    if (includeKnownXamlThreads) {
        for (DWORD threadId : SnapshotKnownXamlThreads()) {
            auto it = std::find_if(
                candidates.begin(), candidates.end(),
                [threadId](const LateAttachWindowCandidate& candidate) {
                    return candidate.threadId == threadId;
                });
            if (it == candidates.end()) {
                candidates.push_back({nullptr, threadId, 0});
            }
        }
    }

    const DWORD preferred =
        g_xamlUiThreadId.load(std::memory_order_acquire);
    std::stable_sort(
        candidates.begin(), candidates.end(),
        [preferred](const LateAttachWindowCandidate& a,
                    const LateAttachWindowCandidate& b) {
            const bool aPreferred = preferred && a.threadId == preferred;
            const bool bPreferred = preferred && b.threadId == preferred;
            if (aPreferred != bPreferred) return aPreferred;
            return a.area > b.area;
        });
    return candidates;
}

struct UiThreadRendezvousPostResult {
    bool postedAny = false;
    unsigned hwndPosts = 0;
    unsigned hwndSends = 0;
    bool threadPost = false;
    HWND activeHwnd = nullptr;
    HWND focusHwnd = nullptr;
    DWORD lastError = ERROR_SUCCESS;
};

bool IsWindowOnCurrentProcessThread(HWND hwnd, DWORD threadId) {
    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }

    DWORD processId = 0;
    const DWORD ownerThreadId =
        GetWindowThreadProcessId(hwnd, &processId);
    return ownerThreadId == threadId &&
           processId == GetCurrentProcessId();
}

void AppendUniqueRendezvousWindow(
        std::vector<HWND>* windows,
        HWND hwnd,
        DWORD threadId) {
    if (!windows ||
        !IsWindowOnCurrentProcessThread(hwnd, threadId)) {
        return;
    }

    if (std::find(windows->begin(), windows->end(), hwnd) ==
        windows->end()) {
        windows->push_back(hwnd);
    }
}

UiThreadRendezvousPostResult PostUiThreadRendezvous(
        DWORD threadId,
        HWND fallbackHwnd,
        UINT message,
        WPARAM cookie) {
    UiThreadRendezvousPostResult result{};
    if (!threadId || !message) {
        result.lastError = ERROR_INVALID_PARAMETER;
        return result;
    }

    std::vector<HWND> windows;
    GUITHREADINFO guiInfo{};
    guiInfo.cbSize = sizeof(guiInfo);
    if (GetGUIThreadInfo(threadId, &guiInfo)) {
        result.activeHwnd = guiInfo.hwndActive;
        result.focusHwnd = guiInfo.hwndFocus;

        // A nested language/IME selector can temporarily pump only the
        // currently active/focused HWND. Put those first, then the normal
        // top-level window discovered by EnumWindows.
        AppendUniqueRendezvousWindow(
            &windows, guiInfo.hwndActive, threadId);
        AppendUniqueRendezvousWindow(
            &windows, guiInfo.hwndFocus, threadId);
    }
    AppendUniqueRendezvousWindow(
        &windows, fallbackHwnd, threadId);

    for (HWND hwnd : windows) {
        if (PostMessageW(hwnd, message, cookie, 0)) {
            result.postedAny = true;
            ++result.hwndPosts;
        } else {
            result.lastError = GetLastError();
        }

        // Also send a nonqueued notification. A XAML language/IME selector can
        // run a nested pump which keeps servicing sent messages while delaying
        // or filtering ordinary queued messages. WH_CALLWNDPROC below catches
        // this path before the native HWND procedure sees the private message.
        if (SendNotifyMessageW(hwnd, message, cookie, 0)) {
            result.postedAny = true;
            ++result.hwndSends;
        } else {
            result.lastError = GetLastError();
        }
    }

    // Also fan out to the thread queue. The hook consumes the first matching
    // rendezvous only; duplicate registered messages are harmless afterward.
    // This retains the old path for UI loops that don't filter thread messages.
    if (PostThreadMessageW(threadId, message, cookie, 0)) {
        result.postedAny = true;
        result.threadPost = true;
    } else {
        result.lastError = GetLastError();
    }

    return result;
}

Controls::UserControl FindSystemProviderFromPowerButton(
        FrameworkElement const& powerElement) {
    try {
        DependencyObject current = powerElement;
        while (current) {
            if (auto control = current.try_as<Controls::UserControl>()) {
                if (IsSystemProviderUserControl(control)) {
                    return control;
                }
            }
            current = Media::VisualTreeHelper::GetParent(current);
        }
    } catch (...) {
    }

    return nullptr;
}

bool LateAttachExistingSystemProviderOnUiThread() {
    const DWORD currentThreadId = GetCurrentThreadId();
    ModLog(L"Late-attach rendezvous probing UI thread=%lu",
           currentThreadId);

    if (g_unloading.load(std::memory_order_acquire)) {
        return false;
    }

    try {
        auto window = Window::Current();
        if (!window) {
            ModLog(L"Late-attach: Window::Current is null on thread=%lu",
                   currentThreadId);
            return false;
        }

        auto root = window.Content().try_as<FrameworkElement>();
        if (!root) {
            ModLog(L"Late-attach: current XAML Window has no FrameworkElement content on thread=%lu",
                   currentThreadId);
            return false;
        }

        // Remember every positively identified XAML Window thread, not merely
        // whichever current-process HWND happens to be largest at this moment.
        RememberXamlThread(currentThreadId, false);

        auto powerElement = FindByName(root, L"PowerOptions");
        if (!powerElement) {
            ModLog(L"Late-attach: thread=%lu has a XAML Window but no PowerOptions/SystemProvider tree",
                   currentThreadId);
            return false;
        }

        auto userControl = FindSystemProviderFromPowerButton(powerElement);
        if (!userControl || !IsSystemProviderUserControl(userControl)) {
            ModLog(L"Late-attach: thread=%lu has PowerOptions but no validated LogonUX.SystemProviderUserControl ancestor",
                   currentThreadId);
            return false;
        }

        RememberXamlThread(currentThreadId, true);
        ModLog(L"Late-attach: validated existing SystemProvider on thread=%lu class=LogonUX.SystemProviderUserControl",
               currentThreadId);

        // Reuse the normal injection path. It handles retained-session
        // revalidation, realized controls, and lazy PowerOptions templates.
        InjectVisual(
            reinterpret_cast<void*>(winrt::get_abi(userControl)), true);
        return true;
    } catch (const winrt::hresult_error& e) {
        ModLog(L"Late-attach probe failed on thread=%lu: 0x%08X %s",
               currentThreadId,
               static_cast<unsigned>(e.code().value),
               e.message().c_str());
    } catch (...) {
        ModLog(L"Late-attach probe failed on thread=%lu with unknown exception",
               currentThreadId);
    }
    return false;
}

bool TryHandleLateAttachRendezvous(UINT message, WPARAM wParam) {
    if (!g_lateAttachPending.load(std::memory_order_acquire) ||
        message != g_lateAttachMessage ||
        GetCurrentThreadId() !=
            g_lateAttachProbeThreadId.load(std::memory_order_acquire) ||
        static_cast<ULONG_PTR>(wParam) !=
            g_lateAttachProbeCookie.load(std::memory_order_acquire)) {
        return false;
    }

    bool expected = true;
    if (!g_lateAttachPending.compare_exchange_strong(
            expected, false, std::memory_order_acq_rel)) {
        return false;
    }

    const bool matched = LateAttachExistingSystemProviderOnUiThread();
    g_lateAttachProbeMatched.store(matched, std::memory_order_release);
    if (g_lateAttachCompletedEvent) {
        SetEvent(g_lateAttachCompletedEvent);
    }
    return true;
}

void DetachLateAttachRendezvousHooks() {
    if (HHOOK hook = g_lateAttachHook.exchange(nullptr)) {
        UnhookWindowsHookEx(hook);
    }
    if (HHOOK hook = g_lateAttachCallWndHook.exchange(nullptr)) {
        UnhookWindowsHookEx(hook);
    }
}

LRESULT CALLBACK LateAttachGetMsgProc(int code, WPARAM wParam, LPARAM lParam) {
    bool handled = false;
    if (code >= 0 && lParam) {
        const auto* message = reinterpret_cast<const MSG*>(lParam);
        handled = TryHandleLateAttachRendezvous(
            message->message, message->wParam);
    }

    const LRESULT next = CallNextHookEx(nullptr, code, wParam, lParam);
    if (handled) {
        DetachLateAttachRendezvousHooks();
    }
    return next;
}

LRESULT CALLBACK LateAttachCallWndProc(
        int code, WPARAM wParam, LPARAM lParam) {
    bool handled = false;
    if (code >= 0 && lParam) {
        const auto* message =
            reinterpret_cast<const CWPSTRUCT*>(lParam);
        handled = TryHandleLateAttachRendezvous(
            message->message, message->wParam);
    }

    const LRESULT next = CallNextHookEx(nullptr, code, wParam, lParam);
    if (handled) {
        DetachLateAttachRendezvousHooks();
    }
    return next;
}

void RequestLateAttachExistingSystemProvider() {
    if (g_unloading.load(std::memory_order_acquire)) {
        return;
    }
    if (!g_lateAttachMessage) {
        g_lateAttachMessage = RegisterWindowMessageW(
            L"Windhawk.LogonUI.Tweaks.LateAttach.v1");
        if (!g_lateAttachMessage) {
            ModLog(L"Late-attach: RegisterWindowMessage failed: %lu",
                   GetLastError());
            return;
        }
    }
    if (!g_lateAttachCompletedEvent) {
        g_lateAttachCompletedEvent =
            CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!g_lateAttachCompletedEvent) {
            ModLog(L"Late-attach: CreateEvent failed: %lu",
                   GetLastError());
            return;
        }
    }

    // A Windows 10 language/IME selector can keep the SystemProvider's XAML
    // thread inside a nested message pump for several seconds after dismissal.
    // Retry long enough to bridge that transient state, but keep every delay
    // cancellable through g_stopEvent so hot-disable remains responsive.
    constexpr DWORD kRetryDelayMs[] = {
        0, 500, 1000, 2000, 4000, 5000
    };
    constexpr DWORD kProbeWaitMs = 750;

    unsigned totalProbed = 0;
    unsigned totalMatches = 0;

    for (unsigned pass = 0;
         pass < ARRAYSIZE(kRetryDelayMs);
         ++pass) {
        if (pass != 0) {
            const DWORD delay = kRetryDelayMs[pass];
            ModLog(L"Late-attach: no validated SystemProvider yet; retry pass %u/%u in %lu ms",
                   pass + 1,
                   static_cast<unsigned>(ARRAYSIZE(kRetryDelayMs)),
                   delay);

            if (g_stopEvent &&
                WaitForSingleObject(g_stopEvent, delay) ==
                    WAIT_OBJECT_0) {
                break;
            }
            if (g_unloading.load(std::memory_order_acquire)) {
                break;
            }
        }

        auto candidates = CollectUiThreadCandidates(true);
        if (candidates.empty()) {
            ModLog(L"Late-attach: no current-process UI thread candidates found on pass %u; _OnLoaded hook remains armed",
                   pass + 1);
            continue;
        }

        unsigned passProbed = 0;
        unsigned passMatches = 0;

        for (const auto& candidate : candidates) {
            if (!candidate.threadId ||
                g_unloading.load(std::memory_order_acquire)) {
                continue;
            }

            ResetEvent(g_lateAttachCompletedEvent);
            g_lateAttachProbeMatched.store(
                false, std::memory_order_release);
            g_lateAttachProbeThreadId.store(
                candidate.threadId, std::memory_order_release);
            g_lateAttachPending.store(
                true, std::memory_order_release);

            const ULONG_PTR cookie =
                g_lateAttachCookieCounter.fetch_add(
                    1, std::memory_order_acq_rel);
            g_lateAttachProbeCookie.store(
                cookie, std::memory_order_release);

            HHOOK getMsgHook = SetWindowsHookExW(
                WH_GETMESSAGE,
                LateAttachGetMsgProc,
                nullptr,
                candidate.threadId);
            HHOOK callWndHook = SetWindowsHookExW(
                WH_CALLWNDPROC,
                LateAttachCallWndProc,
                nullptr,
                candidate.threadId);
            if (!getMsgHook && !callWndHook) {
                g_lateAttachPending.store(
                    false, std::memory_order_release);
                ModLog(L"Late-attach: couldn't install GETMESSAGE/CALLWNDPROC hooks for candidate thread=%lu: %lu",
                       candidate.threadId, GetLastError());
                continue;
            }

            if (HHOOK oldHook =
                    g_lateAttachHook.exchange(getMsgHook)) {
                UnhookWindowsHookEx(oldHook);
            }
            if (HHOOK oldHook =
                    g_lateAttachCallWndHook.exchange(callWndHook)) {
                UnhookWindowsHookEx(oldHook);
            }

            const auto post = PostUiThreadRendezvous(
                candidate.threadId,
                candidate.hwnd,
                g_lateAttachMessage,
                cookie);
            if (!post.postedAny) {
                g_lateAttachPending.store(
                    false, std::memory_order_release);
                DetachLateAttachRendezvousHooks();
                ModLog(L"Late-attach: couldn't post rendezvous to candidate thread=%lu: %lu",
                       candidate.threadId, post.lastError);
                continue;
            }

            ++passProbed;
            ++totalProbed;
            ModLog(L"Late-attach: probing candidate pass=%u hwnd=%p thread=%lu fanout(postedHwnd=%u sentHwnd=%u thread=%d active=%p focus=%p)",
                   pass + 1,
                   candidate.hwnd,
                   candidate.threadId,
                   post.hwndPosts,
                   post.hwndSends,
                   post.threadPost ? 1 : 0,
                   post.activeHwnd,
                   post.focusHwnd);

            HANDLE waitHandles[2] = {
                g_lateAttachCompletedEvent,
                g_stopEvent
            };
            const DWORD waitCount = g_stopEvent ? 2 : 1;
            const DWORD wait = WaitForMultipleObjects(
                waitCount,
                waitHandles,
                FALSE,
                kProbeWaitMs);

            if (wait == WAIT_OBJECT_0) {
                if (g_lateAttachProbeMatched.load(
                        std::memory_order_acquire)) {
                    ++passMatches;
                    ++totalMatches;
                }
            } else {
                g_lateAttachPending.store(
                    false, std::memory_order_release);
                DetachLateAttachRendezvousHooks();

                if (g_stopEvent &&
                    wait == WAIT_OBJECT_0 + 1) {
                    g_lateAttachProbeThreadId.store(
                        0, std::memory_order_release);
                    ModLog(L"Late-attach: stop requested during rendezvous scan");
                    return;
                }

                ModLog(L"Late-attach: candidate thread=%lu did not service rendezvous in %lu ms on pass %u; continuing scan",
                       candidate.threadId,
                       kProbeWaitMs,
                       pass + 1);
            }
        }

        g_lateAttachProbeThreadId.store(
            0, std::memory_order_release);

        ModLog(L"Late-attach pass %u complete: probed=%u validatedSystemProviderThreads=%u",
               pass + 1, passProbed, passMatches);

        if (passMatches != 0) {
            break;
        }
    }

    g_lateAttachPending.store(false, std::memory_order_release);
    g_lateAttachProbeThreadId.store(0, std::memory_order_release);
    DetachLateAttachRendezvousHooks();

    ModLog(L"Late-attach recovery complete: totalProbed=%u validatedSystemProviderThreads=%u; _OnLoaded hook remains armed",
           totalProbed, totalMatches);
}

bool QueueHotCleanupFenceOnUiThread() {
    HANDLE fenceEvent = g_hotCleanupFenceEvent;
    if (!fenceEvent) {
        return false;
    }

    g_hotCleanupFenceCountdown.store(0, std::memory_order_release);
    auto makeFenceCallback = [fenceEvent]() {
        return [fenceEvent]() {
            if (g_hotCleanupFenceCountdown.fetch_sub(
                    1, std::memory_order_acq_rel) == 1) {
                SetEvent(fenceEvent);
            }
        };
    };

    bool queuedAny = false;

    // Fence both scheduling mechanisms. Automatic normally uses
    // DispatcherQueue on current builds, but CoreDispatcher is an explicit
    // advanced option and can have its own already-queued callbacks.
    try {
        auto queue = System::DispatcherQueue::GetForCurrentThread();
        if (queue) {
            g_hotCleanupFenceCountdown.fetch_add(
                1, std::memory_order_acq_rel);
            if (queue.TryEnqueue(makeFenceCallback())) {
                queuedAny = true;
            } else {
                g_hotCleanupFenceCountdown.fetch_sub(
                    1, std::memory_order_acq_rel);
            }
        }
    } catch (...) {
    }

    try {
        auto window = Window::Current();
        if (window) {
            auto dispatcher = window.Dispatcher();
            if (dispatcher) {
                g_hotCleanupFenceCountdown.fetch_add(
                    1, std::memory_order_acq_rel);
                try {
                    dispatcher.RunAsync(
                        Core::CoreDispatcherPriority::Normal,
                        makeFenceCallback());
                    queuedAny = true;
                } catch (...) {
                    g_hotCleanupFenceCountdown.fetch_sub(
                        1, std::memory_order_acq_rel);
                }
            }
        }
    } catch (...) {
    }

    if (!queuedAny ||
        g_hotCleanupFenceCountdown.load(std::memory_order_acquire) == 0) {
        SetEvent(fenceEvent);
        return false;
    }

    return true;
}

void SweepOrphanInjectedVisualsOnCurrentXamlThread() {
    try {
        auto window = Window::Current();
        if (!window) return;

        auto root = window.Content().try_as<FrameworkElement>();
        if (!root) return;

        auto power = FindByName(root, L"PowerOptions");
        if (!power) return;
        auto provider = FindSystemProviderFromPowerButton(power);
        if (!provider || !IsSystemProviderUserControl(provider)) return;

        RememberXamlThread(GetCurrentThreadId(), true);

        unsigned removed = 0;
        auto removeNamed = [&](const wchar_t* name) {
            auto element = FindByName(root, name);
            if (!element) return;
            try {
                auto parent = Media::VisualTreeHelper::GetParent(element)
                                  .try_as<Controls::Grid>();
                if (!parent) return;
                auto children = parent.Children();
                uint32_t index = 0;
                if (children.IndexOf(element, index)) {
                    children.RemoveAt(index);
                    ++removed;
                }
            } catch (...) {
            }
        };

        removeNamed(kInjectedBatteryName);
        removeNamed(kInjectedVolumeName);
        removeNamed(kInjectedClockName);
        if (removed) {
            ModLog(L"Hot-unload orphan sweep removed %u injected control(s) on XAML thread=%lu",
                   removed, GetCurrentThreadId());
        }
    } catch (...) {
    }
}

void HotCleanupOnUiThread() {
    const DWORD currentThreadId = GetCurrentThreadId();
    ModLog(L"Hot-unload cleanup running on LogonUI UI/XAML thread=%lu",
           currentThreadId);

    StopOutstandingXamlCaptureWaitOnUiThread();
    DetachLayoutRetriesOnCurrentThread();
    CleanupInjectionSessionsOnCurrentThread(true);
    SweepOrphanInjectedVisualsOnCurrentXamlThread();

    // The bridge window is thread-affine. Destroy it only from its owning
    // thread; RequestHotCleanupOnUiThread explicitly includes that thread.
    HWND bridge = g_batteryUiBridgeWindow.load(std::memory_order_acquire);
    if (bridge &&
        GetWindowThreadProcessId(bridge, nullptr) == currentThreadId) {
        HWND expected = bridge;
        if (g_batteryUiBridgeWindow.compare_exchange_strong(
                expected, nullptr, std::memory_order_acq_rel)) {
            DestroyWindow(bridge);
        }
    }

    // Weak refs themselves are mod-owned bookkeeping. The actual XAML event
    // handlers and controls above have already been detached on this thread.
    g_batteryPercentText = {};

    QueueHotCleanupFenceOnUiThread();
}

bool TryHandleHotCleanupRendezvous(UINT message, WPARAM wParam) {
    if (!g_hotCleanupPending.load(std::memory_order_acquire) ||
        message != g_hotCleanupMessage ||
        GetCurrentThreadId() !=
            g_hotCleanupProbeThreadId.load(std::memory_order_acquire) ||
        static_cast<ULONG_PTR>(wParam) !=
            g_hotCleanupProbeCookie.load(std::memory_order_acquire)) {
        return false;
    }

    bool expected = true;
    if (!g_hotCleanupPending.compare_exchange_strong(
            expected, false, std::memory_order_acq_rel)) {
        return false;
    }

    HotCleanupOnUiThread();
    if (g_hotCleanupCompletedEvent) {
        SetEvent(g_hotCleanupCompletedEvent);
    }
    return true;
}

void DetachHotCleanupRendezvousHooks() {
    if (HHOOK hook = g_hotCleanupHook.exchange(nullptr)) {
        UnhookWindowsHookEx(hook);
    }
    if (HHOOK hook = g_hotCleanupCallWndHook.exchange(nullptr)) {
        UnhookWindowsHookEx(hook);
    }
}

LRESULT CALLBACK HotCleanupGetMsgProc(
        int code, WPARAM wParam, LPARAM lParam) {
    bool handled = false;
    if (code >= 0 && lParam) {
        const auto* message = reinterpret_cast<const MSG*>(lParam);
        handled = TryHandleHotCleanupRendezvous(
            message->message, message->wParam);
    }

    const LRESULT next = CallNextHookEx(nullptr, code, wParam, lParam);
    if (handled) {
        DetachHotCleanupRendezvousHooks();
    }
    return next;
}

LRESULT CALLBACK HotCleanupCallWndProc(
        int code, WPARAM wParam, LPARAM lParam) {
    bool handled = false;
    if (code >= 0 && lParam) {
        const auto* message =
            reinterpret_cast<const CWPSTRUCT*>(lParam);
        handled = TryHandleHotCleanupRendezvous(
            message->message, message->wParam);
    }

    const LRESULT next = CallNextHookEx(nullptr, code, wParam, lParam);
    if (handled) {
        DetachHotCleanupRendezvousHooks();
    }
    return next;
}

struct HotCleanupTarget {
    DWORD threadId = 0;
    HWND hwnd = nullptr;
    bool required = false;
};

void AddHotCleanupTarget(
        std::vector<HotCleanupTarget>* targets,
        DWORD threadId,
        HWND hwnd,
        bool required) {
    if (!targets || !threadId) return;
    auto it = std::find_if(
        targets->begin(), targets->end(),
        [threadId](const HotCleanupTarget& target) {
            return target.threadId == threadId;
        });
    if (it == targets->end()) {
        targets->push_back({threadId, hwnd, required});
        return;
    }
    if (!it->hwnd && hwnd) it->hwnd = hwnd;
    it->required = it->required || required;
}

std::vector<HotCleanupTarget> CollectHotCleanupTargets() {
    std::vector<HotCleanupTarget> targets;

    // Probe every XAML/UI thread discovered during the multi-thread late
    // attach scan so orphaned named controls cannot survive on a secondary
    // XAML Window.
    for (const auto& candidate : CollectUiThreadCandidates(true)) {
        AddHotCleanupTarget(
            &targets, candidate.threadId, candidate.hwnd, false);
    }

    // Threads which own actual mod callbacks/state are mandatory rendezvous
    // targets. They must be cleaned before Windhawk can unload this DLL.
    for (DWORD threadId : SnapshotInjectionSessionThreads()) {
        AddHotCleanupTarget(&targets, threadId, nullptr, true);
    }
    for (DWORD threadId : SnapshotLayoutRetryThreads()) {
        AddHotCleanupTarget(&targets, threadId, nullptr, true);
    }
    if (DWORD captureThreadId = GetOutstandingXamlCaptureThreadId()) {
        AddHotCleanupTarget(&targets, captureThreadId, nullptr, true);
    }
    if (HWND bridge =
            g_batteryUiBridgeWindow.load(std::memory_order_acquire)) {
        DWORD bridgeThreadId = GetWindowThreadProcessId(bridge, nullptr);
        AddHotCleanupTarget(&targets, bridgeThreadId, bridge, true);
    }

    const DWORD preferred =
        g_xamlUiThreadId.load(std::memory_order_acquire);
    std::stable_sort(
        targets.begin(), targets.end(),
        [preferred](const HotCleanupTarget& a,
                    const HotCleanupTarget& b) {
            const bool aPreferred = preferred && a.threadId == preferred;
            const bool bPreferred = preferred && b.threadId == preferred;
            if (aPreferred != bPreferred) return aPreferred;
            if (a.required != b.required) return a.required;
            return a.threadId < b.threadId;
        });
    return targets;
}

bool RequestHotCleanupOnUiThread() {
    if (!g_hotCleanupMessage) {
        g_hotCleanupMessage = RegisterWindowMessageW(
            L"Windhawk.LogonUI.Tweaks.HotCleanup.v3");
        if (!g_hotCleanupMessage) {
            ModLog(L"Hot-unload: RegisterWindowMessage failed: %lu",
                   GetLastError());
            return false;
        }
    }

    auto targets = CollectHotCleanupTargets();
    if (targets.empty()) {
        ModLog(L"Hot-unload: no tracked XAML/UI threads or live UI state remain");
        return true;
    }

    g_hotCleanupCompletedEvent =
        CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_hotCleanupFenceEvent =
        CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_hotCleanupCompletedEvent || !g_hotCleanupFenceEvent) {
        ModLog(L"Hot-unload: couldn't create rendezvous events: %lu",
               GetLastError());
        if (g_hotCleanupCompletedEvent) {
            CloseHandle(g_hotCleanupCompletedEvent);
            g_hotCleanupCompletedEvent = nullptr;
        }
        if (g_hotCleanupFenceEvent) {
            CloseHandle(g_hotCleanupFenceEvent);
            g_hotCleanupFenceEvent = nullptr;
        }
        return false;
    }

    bool allRequiredTargetsCleaned = true;
    unsigned cleanedTargets = 0;

    for (const auto& target : targets) {
        if (!target.threadId) continue;

        ResetEvent(g_hotCleanupCompletedEvent);
        ResetEvent(g_hotCleanupFenceEvent);
        g_hotCleanupFenceCountdown.store(0, std::memory_order_release);
        g_hotCleanupProbeThreadId.store(
            target.threadId, std::memory_order_release);

        if (GetCurrentThreadId() == target.threadId) {
            ModLog(L"Hot-unload: BeforeUninit is already on target UI thread=%lu; running direct detach",
                   target.threadId);
            HotCleanupOnUiThread();
            ++cleanedTargets;
            // A same-thread dispatcher fence cannot execute while
            // BeforeUninit is blocking the dispatcher. State is detached, but
            // conservatively report incomplete confirmation for required state.
            if (target.required) {
                allRequiredTargetsCleaned = false;
            }
            continue;
        }

        const ULONG_PTR cookie =
            g_hotCleanupCookieCounter.fetch_add(
                1, std::memory_order_acq_rel);
        g_hotCleanupProbeCookie.store(
            cookie, std::memory_order_release);

        HHOOK getMsgHook = SetWindowsHookExW(
            WH_GETMESSAGE,
            HotCleanupGetMsgProc,
            nullptr,
            target.threadId);
        HHOOK callWndHook = SetWindowsHookExW(
            WH_CALLWNDPROC,
            HotCleanupCallWndProc,
            nullptr,
            target.threadId);
        if (!getMsgHook && !callWndHook) {
            ModLog(L"Hot-unload: couldn't install GETMESSAGE/CALLWNDPROC hooks for thread=%lu required=%d: %lu",
                   target.threadId, target.required ? 1 : 0,
                   GetLastError());
            if (target.required) allRequiredTargetsCleaned = false;
            continue;
        }

        if (HHOOK oldHook = g_hotCleanupHook.exchange(getMsgHook)) {
            UnhookWindowsHookEx(oldHook);
        }
        if (HHOOK oldHook =
                g_hotCleanupCallWndHook.exchange(callWndHook)) {
            UnhookWindowsHookEx(oldHook);
        }
        g_hotCleanupPending.store(true, std::memory_order_release);

        const auto post = PostUiThreadRendezvous(
            target.threadId,
            target.hwnd,
            g_hotCleanupMessage,
            cookie);
        if (!post.postedAny) {
            g_hotCleanupPending.store(false, std::memory_order_release);
            DetachHotCleanupRendezvousHooks();
            ModLog(L"Hot-unload: couldn't post cleanup rendezvous to thread=%lu required=%d: %lu",
                   target.threadId, target.required ? 1 : 0,
                   post.lastError);
            if (target.required) allRequiredTargetsCleaned = false;
            continue;
        }

        ModLog(L"Hot-unload: rendezvous posted to thread=%lu required=%d fanout(postedHwnd=%u sentHwnd=%u thread=%d active=%p focus=%p fallback=%p)",
               target.threadId,
               target.required ? 1 : 0,
               post.hwndPosts,
               post.hwndSends,
               post.threadPost ? 1 : 0,
               post.activeHwnd,
               post.focusHwnd,
               target.hwnd);

        const DWORD waitMs = target.required ? INFINITE : 1500;
        const DWORD cleanupWait = WaitForSingleObject(
            g_hotCleanupCompletedEvent, waitMs);
        DWORD fenceWait = WAIT_FAILED;
        if (cleanupWait == WAIT_OBJECT_0) {
            fenceWait = WaitForSingleObject(
                g_hotCleanupFenceEvent, waitMs);
        }

        DetachHotCleanupRendezvousHooks();
        g_hotCleanupPending.store(false, std::memory_order_release);

        if (cleanupWait == WAIT_OBJECT_0 &&
            fenceWait == WAIT_OBJECT_0) {
            ++cleanedTargets;
            ModLog(L"Hot-unload: thread=%lu callbacks detached and dispatcher fenced",
                   target.threadId);
            continue;
        }

        if (target.required) {
            allRequiredTargetsCleaned = false;
            ModLog(L"Hot-unload: REQUIRED thread=%lu cleanup/fence confirmation failed cleanupWait=%lu fenceWait=%lu",
                   target.threadId, cleanupWait, fenceWait);
        } else {
            ModLog(L"Hot-unload: optional UI thread=%lu did not complete orphan sweep/fence; continuing",
                   target.threadId);
        }
    }

    g_hotCleanupProbeThreadId.store(0, std::memory_order_release);
    g_hotCleanupPending.store(false, std::memory_order_release);
    DetachHotCleanupRendezvousHooks();

    CloseHandle(g_hotCleanupCompletedEvent);
    CloseHandle(g_hotCleanupFenceEvent);
    g_hotCleanupCompletedEvent = nullptr;
    g_hotCleanupFenceEvent = nullptr;

    // Keep the stale-WNDPROC protection and the dual GETMESSAGE/CALLWNDPROC
    // rendezvous plus queued/sent-message fan-out active during cleanup; this
    // makes cleanup resilient to nested UI message pumps. Unregister only
    // after every owning UI thread has had a chance to destroy its window.
    UnregisterPrivateProcessWindowClass(
        kBatteryUiBridgeClass, BatteryUiBridgeWndProc);
    UnregisterPrivateProcessWindowClass(
        kLegacyVolumeUiWindowClass, LegacyVolumeUiWndProc);

    ModLog(L"Hot-unload multi-thread cleanup complete: targets=%u cleaned=%u requiredOK=%d",
           static_cast<unsigned>(targets.size()), cleanedTargets,
           allRequiredTargetsCleaned ? 1 : 0);
    return allRequiredTargetsCleaned;
}


// -----------------------------------------------------------------------------
// User-list status / remote-session module
// Derived from the validated LogonUI Remote Status Restore Lab v0.18.2a.
// It deliberately reuses the shared SystemProvider::_OnLoaded hook instead of
// installing a second hook on the same Windows.UI.Logon target. The private
// UserViewModel ABI remains independently fail-closed.
// -----------------------------------------------------------------------------
namespace UserStatus {


int ClampInt(int value, int minimum, int maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

enum class StatusBehavior : int {
    LegacyAuthUi = 0,
    Windows11UserViewModel,
};

const wchar_t* StatusBehaviorName(StatusBehavior behavior) {
    switch (behavior) {
        case StatusBehavior::LegacyAuthUi:
            return L"Legacy AuthUI-style";
        case StatusBehavior::Windows11UserViewModel:
            return L"Windows 11 UserViewModel";
    }
    return L"Unknown";
}

StatusBehavior ParseStatusBehavior(PCWSTR value) {
    if (value && _wcsicmp(value, L"windows11") == 0) {
        return StatusBehavior::Windows11UserViewModel;
    }
    return StatusBehavior::LegacyAuthUi;
}

// Unified row layout: a single registry for native VM, derived WTS and
// diagnostics. Expressions are evaluated from one row snapshot; no polling.
enum class RowField : unsigned char {
    Name, Qualified, Status, LockedStatus, RemoteStatus, NativeStatus,
    Custom, SessionId, Connection, WtsState, Station, Protocol, LockState,
    VmLoggedIn, VmStatusVisible, VmLogonStatus
};
constexpr unsigned kUnifiedMaxLines = 16;
struct RowRule {
    std::wstring expression;
    std::vector<RowField> candidates;
};
enum class RowOrder { Manual, FirstObserved, LastObserved };
struct RowLayoutPlan {
    std::vector<RowRule> rules;
    RowOrder order = RowOrder::Manual;
    uint32_t neededDetailMask = 0;
};
std::wstring TrimRowLayoutToken(std::wstring token) {
    const auto first = token.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return L"";
    const auto last = token.find_last_not_of(L" \t\r\n");
    return token.substr(first, last - first + 1);
}

bool ParseRowField(const std::wstring& raw, RowField* result) {
    if (!result) return false;
    constexpr std::pair<const wchar_t*, RowField> fields[] = {
        {L"name", RowField::Name},
        {L"qualified", RowField::Qualified},
        {L"status", RowField::Status},
        {L"lockedStatus", RowField::LockedStatus},
        {L"remoteStatus", RowField::RemoteStatus},
        {L"nativeStatus", RowField::NativeStatus},
        {L"custom", RowField::Custom},
        {L"sessionId", RowField::SessionId},
        {L"connection", RowField::Connection},
        {L"wtsState", RowField::WtsState},
        {L"station", RowField::Station},
        {L"protocol", RowField::Protocol},
        {L"lockState", RowField::LockState},
        {L"vmLoggedIn", RowField::VmLoggedIn},
        {L"vmStatusVisible", RowField::VmStatusVisible},
        {L"vmLogonStatus", RowField::VmLogonStatus},
    };
    auto field = TrimRowLayoutToken(raw);
    for (const auto& item : fields) {
        if (_wcsicmp(field.c_str(), item.first) == 0) {
            *result = item.second;
            return true;
        }
    }
    return false;
}

std::shared_ptr<const RowLayoutPlan> ParseRowLayoutPlan(
    PCWSTR text, PCWSTR orderText) {
    auto fallback = [] {
        auto plan = std::make_shared<RowLayoutPlan>();
        plan->rules = {
            {L"name", {RowField::Name}},
            {L"status", {RowField::Status}},
            {L"qualified", {RowField::Qualified}}};
        return plan;
    };
    if (!text || !*text || wcslen(text) > 1024) return fallback();
    auto plan = std::make_shared<RowLayoutPlan>();
    if (orderText && _wcsicmp(orderText, L"first") == 0)
        plan->order = RowOrder::FirstObserved;
    else if (orderText && _wcsicmp(orderText, L"last") == 0)
        plan->order = RowOrder::LastObserved;

    const std::wstring input(text);
    size_t begin = 0;
    unsigned depth = 0;
    bool haveName = false;
    for (size_t i = 0; i <= input.size(); ++i) {
        const wchar_t c = i < input.size() ? input[i] : L',';
        if (c == L'(') { if (++depth > 1) return fallback(); }
        if (c == L')') { if (depth == 0) return fallback(); --depth; }
        if (c != L',' || depth != 0) continue;
        std::wstring expression = TrimRowLayoutToken(input.substr(begin, i-begin));
        if (expression.empty() || plan->rules.size() >= kUnifiedMaxLines)
            return fallback();
        RowRule rule;
        rule.expression = expression;
        const bool isFirst = expression.size() > 7 &&
            _wcsnicmp(expression.c_str(), L"first(", 6) == 0 &&
            expression.back() == L')';
        std::wstring candidatesText = isFirst
            ? expression.substr(6, expression.size()-7) : expression;
        if (isFirst && candidatesText.find_first_of(L"()") != std::wstring::npos)
            return fallback();
        size_t a = 0;
        for (;;) {
            const size_t b = candidatesText.find(L',', a);
            RowField field{};
            if (!ParseRowField(candidatesText.substr(a,
                b == std::wstring::npos ? b : b-a), &field)) return fallback();
            if (field == RowField::Name && isFirst) return fallback();
            if (field == RowField::Name) haveName = true;
            if (std::find(rule.candidates.begin(), rule.candidates.end(), field)
                != rule.candidates.end()) return fallback();
            rule.candidates.push_back(field);
            const auto index = static_cast<unsigned>(field);
            if (index >= static_cast<unsigned>(RowField::SessionId))
                plan->neededDetailMask |= 1u <<
                    (index-static_cast<unsigned>(RowField::SessionId));
            if (rule.candidates.size() > kUnifiedMaxLines) return fallback();
            if (b == std::wstring::npos) break;
            a = b+1;
        }
        if (isFirst && rule.candidates.size() < 2) return fallback();
        if (!isFirst && rule.candidates.size() != 1) return fallback();
        for (const auto& old : plan->rules) {
            if (_wcsicmp(old.expression.c_str(), rule.expression.c_str()) == 0)
                return fallback();
            // A direct field maps to one XAML control and must never be
            // appended twice to the same children collection.
            if (old.candidates.size()==1 && rule.candidates.size()==1 &&
                old.candidates.front()==rule.candidates.front()) return fallback();
        }
        plan->rules.emplace_back(std::move(rule));
        begin = i+1;
    }
    if (depth != 0 || !haveName) return fallback();
    return plan;
}

struct CustomRowTextSettings {
    std::wstring defaultText;
    std::vector<std::pair<std::wstring, std::wstring>> accountTexts;
};

std::wstring NormalizeRowText(std::wstring text) {
    for (auto& ch : text) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
    }
    if (text.size() > 160) text.resize(160);
    return text;
}

std::shared_ptr<const CustomRowTextSettings> ParseCustomRowTexts(
    PCWSTR defaultText, PCWSTR accountText) {
    auto settings = std::make_shared<CustomRowTextSettings>();
    settings->defaultText = NormalizeRowText(defaultText ? defaultText : L"");
    if (!accountText) return settings;
    const std::wstring all(accountText);
    size_t pos = 0;
    while (pos < all.size() && settings->accountTexts.size() < 64) {
        const auto end = all.find(L';', pos);
        const auto entry = TrimRowLayoutToken(all.substr(pos,
            end == std::wstring::npos ? end : end - pos));
        const auto bar = entry.find(L'|');
        if (bar != std::wstring::npos) {
            auto key = TrimRowLayoutToken(entry.substr(0, bar));
            if (key.find(L'\\') != std::wstring::npos && key.size() <= 256) {
                settings->accountTexts.emplace_back(
                    std::move(key), NormalizeRowText(entry.substr(bar + 1)));
            }
        }
        if (end == std::wstring::npos) break;
        pos = end + 1;
    }
    return settings;
}

enum class SessionDetailsMode : int {
    Off = 0,
    Summary,
    Full,
    Custom,
};

// These flags control *only* mod-owned diagnostic presentation. The native
// UserViewModel, WTS session identity and status-selection inputs remain intact.
constexpr uint32_t kDetailSessionId     = 1u << 0;
constexpr uint32_t kDetailConnection    = 1u << 1;
constexpr uint32_t kDetailWtsState      = 1u << 2;
constexpr uint32_t kDetailStation       = 1u << 3;
constexpr uint32_t kDetailProtocol      = 1u << 4;
constexpr uint32_t kDetailLockState     = 1u << 5;
constexpr uint32_t kDetailVmLoggedIn    = 1u << 6;
constexpr uint32_t kDetailVmVisible     = 1u << 7;
constexpr uint32_t kDetailVmLogonStatus = 1u << 8;

bool ShowDetailLine(SessionDetailsMode mode, uint32_t customFlags,
                    uint32_t sourceFlag, bool isSummaryLine) {
    if (mode == SessionDetailsMode::Full) return true;
    if (mode == SessionDetailsMode::Summary) return isSummaryLine;
    return mode == SessionDetailsMode::Custom &&
           (customFlags & sourceFlag) != 0;
}

const wchar_t* SessionDetailsModeName(SessionDetailsMode mode) {
    switch (mode) {
        case SessionDetailsMode::Off:
            return L"Off";
        case SessionDetailsMode::Summary:
            return L"Summary";
        case SessionDetailsMode::Full:
            return L"Full diagnostics";
        case SessionDetailsMode::Custom:
            return L"Custom";
    }
    return L"Unknown";
}

SessionDetailsMode ParseSessionDetailsMode(PCWSTR value) {
    if (value && _wcsicmp(value, L"summary") == 0) {
        return SessionDetailsMode::Summary;
    }
    if (value && _wcsicmp(value, L"full") == 0) {
        return SessionDetailsMode::Full;
    }
    if (value && _wcsicmp(value, L"custom") == 0) {
        return SessionDetailsMode::Custom;
    }
    return SessionDetailsMode::Off;
}

constexpr wchar_t kTargetModule[] = L"Windows.UI.Logon.dll";

// Currently verified private UserViewModel ABI build.
constexpr DWORD kFastTimeDateStamp = 0xF49AD882;
constexpr DWORD kFastSizeOfImage   = 0x00402000;

// Existing LogonUI Battery/Volume mod's tested fast path for 26200.6584.

// Current matching-PDB UserViewModel ABI RVAs.
// The three status-related RVAs were independently re-confirmed at runtime
// by v0.2 against the currently loaded DLL.
constexpr uintptr_t kRvaGetDisplayName          = 0x001112A0;
constexpr uintptr_t kRvaGetEmailAddress          = 0x00111300;
constexpr uintptr_t kRvaGetIsDomainNameVisible   = 0x00111420;
constexpr uintptr_t kRvaGetIsEmailAddressVisible = 0x00111480;
constexpr uintptr_t kRvaGetIsLoggedIn           = 0x001114E0;
constexpr uintptr_t kRvaGetIsLogonStatusVisible = 0x00111540;
constexpr uintptr_t kRvaGetLogonStatus          = 0x00111720;
constexpr uintptr_t kRvaGetQualifiedName        = 0x00111780;
constexpr uintptr_t kRvaGetSmallUserTileImage   = 0x00111960;

// The user-list tile image is itself a CredentialCommon::UserTileImageViewModel.
// These matching-PDB entries let alpha4d read its BitmapImage from the same
// UserViewModel snapshot as the three visible text lines, eliminating the last
// native-bound visual that can lag behind a recycled ListViewItemPresenter.
constexpr uintptr_t kRvaUserTileImageVmPublicIid = 0x00344578;
constexpr uintptr_t kRvaUserTileImageGetBitmapImage = 0x001026E0;

// Matching PDB interface IIDs and ABI getters used to walk the modern VM chain.
constexpr uintptr_t kRvaRootViewModelPublicIid       = 0x00342268;
constexpr uintptr_t kRvaRequestCredentialVmPublicIid = 0x00344878;
constexpr uintptr_t kRvaUserViewModelPublicIid       = 0x00344AD8;

constexpr uintptr_t kRvaRootGetRequestCredentialsVm  = 0x0010F830;
constexpr uintptr_t kRvaRequestGetSelectedUserOrV1   = 0x0010EB70;
constexpr uintptr_t kRvaRequestGetUserAndV1List    = 0x0010EC90;

using GetBoolAbi = HRESULT(__cdecl*)(void* self, unsigned char* value);
using GetHStringAbi = HRESULT(__cdecl*)(void* self, void** value);
using GetObjectAbi = HRESULT(__cdecl*)(void* self, void** value);

using WindowsGetStringRawBuffer_t =
    PCWSTR(WINAPI*)(void* hstring, UINT32* length);
using WindowsDeleteString_t =
    HRESULT(WINAPI*)(void* hstring);

GetHStringAbi g_getDisplayName = nullptr;
GetHStringAbi g_getEmailAddress = nullptr;
GetBoolAbi g_getIsDomainNameVisible = nullptr;
GetBoolAbi g_getIsEmailAddressVisible = nullptr;
GetBoolAbi g_getIsLoggedIn = nullptr;
GetBoolAbi g_getIsLogonStatusVisible = nullptr;
GetHStringAbi g_getLogonStatus = nullptr;
GetHStringAbi g_getQualifiedName = nullptr;
GetObjectAbi g_getSmallUserTileImage = nullptr;
GetObjectAbi g_userTileImageGetBitmapImage = nullptr;
GetObjectAbi g_rootGetRequestCredentialsVm = nullptr;
GetObjectAbi g_requestGetSelectedUserOrV1 = nullptr;
GetObjectAbi g_requestGetUserAndV1List = nullptr;

WindowsGetStringRawBuffer_t g_windowsGetStringRawBuffer = nullptr;
WindowsDeleteString_t g_windowsDeleteString = nullptr;

GUID g_rootViewModelPublicIid{};
GUID g_requestCredentialVmPublicIid{};
GUID g_userViewModelPublicIid{};
GUID g_userTileImageVmPublicIid{};
bool g_haveVmChainIids = false;

std::atomic<bool> g_unloading{false};
std::atomic<bool> g_enabled{true};
std::atomic<bool> g_abiReady{false};
std::atomic<bool> g_transientFeatureValidated{false};

bool g_haveLastUserState = false;
void* g_lastUserVmIdentity = nullptr;
std::wstring g_lastDisplayName;
int g_lastLoggedIn = -2;
int g_lastVisible = -2;
std::wstring g_lastStatus;
unsigned g_userVmObservationCount = 0;

struct UserListState {
    std::wstring displayName;
    std::wstring qualifiedName;
    int loggedIn = -2;
    int visible = -2;
    std::wstring status;
};

std::vector<UserListState> g_lastUserListStates;
std::atomic<bool> g_userListReady{false};
std::atomic<bool> g_layoutDumped{false};
std::atomic<bool> g_showLogonStatusForAllLoggedIn{true};
std::atomic<bool> g_showNativeTransientStatus{false};
std::atomic<bool> g_showPendingRemoteAttempt{false};
std::atomic<bool> g_liveSessionRowUpdates{false};
std::atomic<bool> g_adaptiveRepairCoalescing{false};
std::atomic<StatusBehavior> g_statusBehavior{StatusBehavior::LegacyAuthUi};
std::atomic<bool> g_customRowLayoutEnabled{false};
std::shared_ptr<const RowLayoutPlan> g_rowLayoutPlan;
// First-observed means first valid value seen in this LogonUI lifetime.
// No extra subscriptions or polling; ties within the same refresh are stable.
std::mutex g_rowObservationMutex;
std::map<std::wstring, std::map<std::wstring, ULONGLONG>> g_rowFirstSeen;
std::shared_ptr<const CustomRowTextSettings> g_customRowTextSettings;
std::wstring CustomRowTextForAccount(const std::wstring& qualifiedName) {
    const auto settings = std::atomic_load_explicit(
        &g_customRowTextSettings, std::memory_order_acquire);
    if (!settings) return L"";
    for (const auto& entry : settings->accountTexts) {
        if (_wcsicmp(entry.first.c_str(), qualifiedName.c_str()) == 0)
            return entry.second;
    }
    return settings->defaultText;
}


std::atomic<bool> g_alwaysShowQualifiedName{false};
std::atomic<SessionDetailsMode> g_sessionDetailsMode{SessionDetailsMode::Off};
std::atomic<uint32_t> g_customRowDetailFlags{0};
std::atomic<bool> g_sessionDetailsEverEnabled{false};
std::atomic<int> g_fallbackRefreshIntervalMs{0};
std::atomic<bool> g_compactUnsignedRows{false};
std::atomic<bool> g_compactRowsEverEnabled{false};
std::atomic<int> g_compactUnsignedRowHeight{48};
std::atomic<bool> g_vmPropertyProbeEnabled{false};
std::atomic<bool> g_vmStateCorrelationEnabled{false};
std::atomic<bool> g_rowIdentityProbeEnabled{false};
std::atomic<bool> g_identityGuardedRowReuse{false};
std::atomic<bool> g_vmCorrelationQueued{false};
std::atomic<unsigned> g_vmCorrelationRequests{0};
std::atomic<unsigned> g_vmCorrelationSourceMask{0}; // 1 native, 2 WTS, 4 attach
std::atomic<DWORD> g_vmCorrelationLastSession{0xFFFFFFFF};
std::atomic<unsigned> g_vmCorrelationLastWtsEvent{0};
[[maybe_unused]] std::atomic<ULONGLONG> g_vmCorrelationLastSelectionTick{0};
std::atomic<unsigned> g_vmProbeCallbacksInFlight{0};
std::atomic<bool> g_enableLayoutDiagnostics{false};
std::atomic<unsigned> g_settingsGeneration{1};
unsigned g_userListScanCount = 0;

constexpr wchar_t kInjectedStatusStackName[] =
    L"WindhawkLogonUITweaksUserStatusStack";
constexpr wchar_t kInjectedDisplayTextName[] =
    L"WindhawkLogonUITweaksDisplayNameText";
constexpr wchar_t kInjectedStatusTextName[] =
    L"WindhawkLogonUITweaksUserStatusText";
constexpr wchar_t kInjectedQualifiedTextName[] =
    L"WindhawkLogonUITweaksQualifiedNameText";
constexpr wchar_t kInjectedUnifiedChoicePrefix[] = L"WindhawkLogonUITweaksUnifiedChoice";
constexpr wchar_t kInjectedSessionIdTextName[] =
    L"WindhawkLogonUITweaksSessionIdText";
constexpr wchar_t kInjectedConnectionTextName[] =
    L"WindhawkLogonUITweaksConnectionText";
constexpr wchar_t kInjectedWtsStateTextName[] =
    L"WindhawkLogonUITweaksWtsStateText";
constexpr wchar_t kInjectedStationTextName[] =
    L"WindhawkLogonUITweaksStationText";
constexpr wchar_t kInjectedProtocolTextName[] =
    L"WindhawkLogonUITweaksProtocolText";
constexpr wchar_t kInjectedLockStateTextName[] =
    L"WindhawkLogonUITweaksLockStateText";
constexpr wchar_t kInjectedVmLoggedInTextName[] =
    L"WindhawkLogonUITweaksVmLoggedInText";
constexpr wchar_t kInjectedVmStatusVisibleTextName[] =
    L"WindhawkLogonUITweaksVmStatusVisibleText";
constexpr wchar_t kInjectedVmLogonStatusTextName[] =
    L"WindhawkLogonUITweaksVmLogonStatusText";
constexpr wchar_t kNativeUserTextStashName[] =
    L"WindhawkLogonUITweaksNativeUserTextStash";
constexpr wchar_t kInjectedUserTileEllipseName[] =
    L"WindhawkLogonUITweaksUserTileEllipse";
constexpr wchar_t kPendingRemoteAttemptFooterName[] =
    L"WindhawkLogonUITweaksPendingRemoteFooter";
constexpr wchar_t kPendingRemoteAttemptTitleName[] =
    L"WindhawkLogonUITweaksPendingRemoteTitle";
constexpr wchar_t kPendingRemoteAttemptBodyName[] =
    L"WindhawkLogonUITweaksPendingRemoteBody";
constexpr wchar_t kPendingRemoteAttemptHeaderName[] =
    L"WindhawkLogonUITweaksPendingRemoteHeader";
constexpr wchar_t kPendingRemoteAttemptOverlayName[] =
    L"WindhawkLogonUITweaksPendingRemoteOverlay";

// A SystemProvider unload/load does not reliably imply that the user-row
// presenters get a matching Loaded event. Keep a cheap, retained LayoutUpdated
// bridge on the XAML root and arm bounded repair scans only when this generation
// changes. Idle LayoutUpdated callbacks return after a few atomics and do no tree
// walking.
std::atomic<unsigned> g_layoutRepairGeneration{1};

constexpr ULONGLONG kLifecycleRepairWindowMs = 10000;
constexpr ULONGLONG kLifecycleEmptyTreeThrottleMs = 100;
constexpr ULONGLONG kLifecycleRealizationGraceMs = 500;
constexpr ULONGLONG kLifecycleNormalRepairThrottleMs = 75;
constexpr ULONGLONG kLifecycleCleanBackoffFirstMs = 150;
constexpr ULONGLONG kLifecycleCleanBackoffMaximumMs = 300;
constexpr unsigned kLifecycleStablePassesRequired = 2;

enum class UiRendezvousAction : int {
    None = 0,
    Refresh,
    TransientFast,
    Cleanup,
};

struct UiThreadCandidate {
    HWND hwnd = nullptr;
    DWORD threadId = 0;
    unsigned long long area = 0;
};

SRWLOCK g_knownXamlThreadsLock = SRWLOCK_INIT;
std::vector<DWORD> g_knownXamlThreads;
std::atomic<DWORD> g_preferredXamlThreadId{0};

// XAML realization/lifecycle bridge. SystemProvider::_OnLoaded can fire before
// UsersListView has realized any user rows, and later lock-screen transitions
// can recycle row presenters without giving SystemProvider a matching Loaded.
// Bootstrap scans are bounded. After rows appear, the handler stays attached
// but is idle unless g_layoutRepairGeneration changes; lifecycle repair then
// uses a bounded realization-aware window and settles after stable row passes.
// This is event-driven, not a timer/polling loop.
struct InitialLayoutRetryRegistration {
    winrt::weak_ref<FrameworkElement> root;
    winrt::event_token token{};
    DWORD xamlThreadId = 0;
    void* rootIdentity = nullptr;
    std::atomic<bool> active{true};
    std::atomic<unsigned> callbacks{0};
    std::atomic<bool> bootstrapComplete{false};
    std::atomic<unsigned> observedRepairGeneration{0};
    std::atomic<ULONGLONG> repairDeadlineTick{0};
    std::atomic<ULONGLONG> nextRepairEligibleTick{0};
    std::atomic<ULONGLONG> firstRepairRowsTick{0};
    std::atomic<unsigned> lastRepairUserRows{0};
    std::atomic<unsigned> stableRepairPasses{0};
    // Counters cover each bounded repair generation; LayoutUpdated callbacks
    // that return during a delay are *not* counted as actual saved scans.
    std::atomic<unsigned> cleanBeforeGraceStreak{0};
    std::atomic<unsigned> repairWalkCount{0};
    std::atomic<unsigned> repairCleanWalks{0};
    std::atomic<unsigned> repairDirtyWalks{0};
    std::atomic<unsigned> throttledLayoutCallbacks{0};
    std::atomic<ULONGLONG> largestChosenThrottleMs{0};
    std::atomic<bool> repairDeferredByTransient{false};
};

SRWLOCK g_initialLayoutRetriesLock = SRWLOCK_INIT;
std::vector<std::shared_ptr<InitialLayoutRetryRegistration>>
    g_initialLayoutRetries;

// Diagnostic-only alpha5d identity comparisons. These record numeric COM
// identities; they never hold references to XAML presenters or native VMs.
// All collection happens inside existing repair walks, and the cache is
// bounded to the current repair generation and at most twelve roots.
struct RowIdentityCapture {
    std::vector<ULONG_PTR> presenters;
    unsigned queryFailures = 0;
    unsigned truncated = 0;
};
struct RowIdentitySnapshot {
    DWORD threadId = 0;
    unsigned generation = 0;
    void* rootIdentity = nullptr;
    unsigned presenterCount = 0;
    unsigned userRowCount = 0;
    unsigned queryFailures = 0;
    unsigned truncated = 0;
    std::vector<ULONG_PTR> presenters;
};
SRWLOCK g_rowIdentitySnapshotsLock = SRWLOCK_INIT;
std::vector<RowIdentitySnapshot> g_rowIdentitySnapshots;
// Alpha5f: no XAML/VM references are retained in this cache: only numeric
// identities and a brief clean result. This is checked strictly on the XAML
// owner thread, after two real walks established exact presenter overlap.
// It cannot replace the independent root traversals or lifecycle bridges.
struct PresenterCleanResult {
    ULONG_PTR presenterIdentity = 0;
    ULONG_PTR userVmInterface = 0;
    double width = 0;
    double height = 0;
    unsigned presentersSeen = 0;
    unsigned userRows = 0;
    unsigned completedRows = 0; // reached the validated row repair end
    unsigned remoteRows = 0;
    unsigned statusRows = 0;
    unsigned qualifiedNameRows = 0;
    unsigned userTileRows = 0;
    unsigned userTileMisses = 0;
};
struct RecentCleanPresenterPass {
    DWORD threadId = 0;
    unsigned generation = 0;
    unsigned settingsGeneration = 0;
    void* rootIdentity = nullptr;
    ULONGLONG capturedTick = 0;
    std::vector<PresenterCleanResult> entries;
};
std::shared_ptr<const RecentCleanPresenterPass> g_recentCleanPresenterPass;

void InvalidateRecentCleanPresenterPass() {
    std::atomic_store_explicit(&g_recentCleanPresenterPass,
        std::shared_ptr<const RecentCleanPresenterPass>{},
        std::memory_order_release);
}


// ListView virtualization can retarget an already-realized presenter to a new
// UserViewModel without a WTS or SystemProvider lifecycle event. alpha4c owned
// the visible text, so that retarget could leave the entire injected triplet on
// the previous user until some unrelated refresh happened. Watch the presenter's
// DataContext directly and arm the retained root repair bridge whenever Windows
// recycles it. These handlers are detached on the owning XAML thread at hot-unload.
struct PresenterDataContextRegistration {
    winrt::weak_ref<FrameworkElement> presenter;
    winrt::event_token token{};
    DWORD xamlThreadId = 0;
    void* presenterIdentity = nullptr;
    std::atomic<bool> active{true};
};

SRWLOCK g_presenterDataContextLock = SRWLOCK_INIT;
std::vector<std::shared_ptr<PresenterDataContextRegistration>>
    g_presenterDataContextRegistrations;

// Diagnostic-only WinRT INotifyPropertyChanged subscriptions. These are
// *not* replacements for WTS state changes and never refresh the UI.
// Both event source and presenter are weak to avoid retaining Windows VMs.
struct VmPropertyProbeRegistration {
    winrt::weak_ref<FrameworkElement> presenter;
    winrt::weak_ref<Data::INotifyPropertyChanged> model;
    winrt::event_token token{};
    winrt::event_token unloadedToken{};
    bool unloadedAttached = false;
    DWORD xamlThreadId = 0;
    void* presenterIdentity = nullptr;
    void* modelIdentity = nullptr;
    std::wstring qualifiedName;
    std::atomic<bool> active{true};
    std::atomic<unsigned> eventCount{0};
    // Accessed only on the owning XAML thread after dispatch; no callback
    // reentrancy, private ABI writes, or UI modifications.
    bool correlationObserved = false;
    int correlationLoggedIn = -2;
    int correlationVisible = -2;
    int correlationDomainVisible = -2;
    std::wstring correlationStatus;
    DWORD correlationSessionId = 0xFFFFFFFF;
    int correlationWtsState = -1;
    int correlationLockState = -1;
    USHORT correlationProtocol = 0xFFFF;
};
SRWLOCK g_vmPropertyProbeLock = SRWLOCK_INIT;
std::vector<std::shared_ptr<VmPropertyProbeRegistration>>
    g_vmPropertyProbeRegistrations;

std::atomic<HHOOK> g_uiRendezvousHook{nullptr};
std::atomic<HHOOK> g_uiRendezvousCallWndHook{nullptr};
std::atomic<bool> g_uiRendezvousPending{false};
std::atomic<int> g_uiRendezvousAction{
    static_cast<int>(UiRendezvousAction::None)};
std::atomic<DWORD> g_uiRendezvousThreadId{0};
std::atomic<bool> g_uiRendezvousMatched{false};
std::atomic<ULONG_PTR> g_uiRendezvousCookie{0};
std::atomic<ULONG_PTR> g_uiRendezvousCookieCounter{1};
UINT g_uiRendezvousMessage = 0;
HANDLE g_uiRendezvousCompletedEvent = nullptr;

constexpr wchar_t kTransientFastUiBridgeClass[] =
    L"Windhawk.LogonUI.Tweaks.TransientFastUiBridge.v1";
constexpr UINT kTransientFastUiMessage = WM_APP + 0x4A1;
constexpr UINT kVmCorrelationUiMessage = WM_APP + 0x4A2;

// Declared here so property callbacks and worker WTS notifications can queue
// the same deferred, read-only UI-thread sample without private getter calls
// from within Windows' own PropertyChanged callback.
void QueueVmCorrelationSample(unsigned sourceMask,
                              DWORD sessionId = 0xFFFFFFFF,
                              unsigned wtsEvent = 0);
std::atomic<HWND> g_transientFastUiBridgeWindow{nullptr};
std::atomic<bool> g_transientFastNeedsFullRefresh{false};

// Console-side scheduling fence for exact native StatusMessage bursts.
// Structural/WTS repairs are deliberately deferred while this is true so the
// XAML thread stays available for status-only updates. It is refreshed from
// the live shared bridge by the worker and cleared when the burst ends.
std::atomic<bool> g_transientBurstActive{false};
std::atomic<ULONGLONG> g_transientBurstSerial{0};

constexpr wchar_t kSessionNotifyWindowClass[] =
    L"Windhawk.LogonUI.Tweaks.UserStatus.SessionNotify.v1";
HWND g_sessionNotifyWindow = nullptr;
std::atomic<bool> g_sessionNotificationsActive{false};

HANDLE g_stopEvent = nullptr;
HANDLE g_refreshWakeEvent = nullptr;
HANDLE g_workerThread = nullptr;

// Exact native transient-status bridge. v0.3 proved that separate LogonUI
// processes can publish/read these slots by WTS session ID without touching
// RootViewModel::DisplayStatus. Keep the shared representation POD-only and
// serialize every cross-process mutation with the named mutex.
constexpr DWORD kTransientBridgeMagic = 0x4954534C; // "LSTI"
constexpr DWORD kTransientBridgeVersion = 1;
constexpr size_t kTransientBridgeSlotCount = 32;
constexpr size_t kTransientBridgeUserChars = 128;
constexpr size_t kTransientBridgeClientChars = 128;
constexpr size_t kTransientBridgeTextChars = 512;
constexpr ULONGLONG kTransientObserverScanThrottleMs = 75;

constexpr wchar_t kTransientBridgeMapName[] =
    L"Global\\Windhawk.LogonUITweaks.TransientStatus.Bridge.v1";
constexpr wchar_t kTransientBridgeMutexName[] =
    L"Global\\Windhawk.LogonUITweaks.TransientStatus.Mutex.v1";
constexpr wchar_t kTransientBridgeReaderEventPrefix[] =
    L"Global\\Windhawk.LogonUITweaks.TransientStatus.Reader.v1.";
constexpr wchar_t kTransientBridgeConsoleSessionEventPrefix[] =
    L"Global\\Windhawk.LogonUITweaks.TransientStatus.ConsoleSession.v1.";

struct TransientBridgeSlot {
    DWORD sessionId = 0xFFFFFFFF;
    DWORD writerPid = 0;
    ULONGLONG writerCreateTime = 0;
    ULONGLONG serial = 0;
    ULONGLONG updateTick = 0;
    DWORD wtsState = static_cast<DWORD>(WTSDown);
    USHORT protocolType = 0xFFFF;
    USHORT reserved = 0;
    wchar_t lastUser[kTransientBridgeUserChars]{};
    wchar_t lastClient[kTransientBridgeClientChars]{};
    wchar_t text[kTransientBridgeTextChars]{};
};

struct TransientBridgeShared {
    DWORD magic = 0;
    DWORD version = 0;
    DWORD structSize = 0;
    DWORD slotCount = 0;
    ULONGLONG serial = 0;
    TransientBridgeSlot slots[kTransientBridgeSlotCount]{};
};

HANDLE g_transientBridgeMapHandle = nullptr;
HANDLE g_transientBridgeMutex = nullptr;
HANDLE g_transientBridgeReaderEvent = nullptr;
TransientBridgeShared* g_transientBridge = nullptr;
ULONGLONG g_transientBridgeProcessCreateTime = 0;
std::atomic<ULONGLONG> g_lastTransientBridgeReadSerial{0};

struct TransientStatusTextObserverRegistration {
    winrt::weak_ref<Controls::TextBlock> textBlock;
    int64_t propertyToken = 0;
    winrt::event_token unloadedToken{};
    DWORD xamlThreadId = 0;
    void* textIdentity = nullptr;
    std::wstring ownerClass;
    std::atomic<bool> active{true};
    bool propertyRegistered = false;
    bool unloadedRegistered = false;
};

struct TransientStatusRootRegistration {
    winrt::weak_ref<FrameworkElement> root;
    winrt::event_token layoutToken{};
    DWORD xamlThreadId = 0;
    void* rootIdentity = nullptr;
    std::atomic<bool> active{true};
    std::atomic<ULONGLONG> nextScanTick{0};
};

SRWLOCK g_transientStatusObserverLock = SRWLOCK_INIT;
std::vector<std::shared_ptr<TransientStatusTextObserverRegistration>>
    g_transientStatusTextObservers;
SRWLOCK g_transientStatusRootLock = SRWLOCK_INIT;
std::vector<std::shared_ptr<TransientStatusRootRegistration>>
    g_transientStatusRootRegistrations;

// The bridge publication, console UI write, and XAML CompositionTarget.Rendering
// event are distinct stages. Record the first two render *passes* after each
// change (only with explicit layout diagnostics enabled). This intentionally
// does not claim that a render pass was delivered to an RDP transport/display.
struct TransientRenderProbeRegistration {
    DWORD xamlThreadId = 0;
    winrt::event_token renderingToken{};
    bool subscribed = false;
    std::atomic<bool> active{true};
    ULONGLONG lastWriterSerial = 0;
    ULONGLONG lastConsoleSerial = 0;
    unsigned pendingFrames = 0;
    unsigned sampledFrames = 0;
};
SRWLOCK g_transientRenderProbeLock = SRWLOCK_INIT;
std::vector<std::shared_ptr<TransientRenderProbeRegistration>>
    g_transientRenderProbes;
std::atomic<ULONGLONG> g_renderWriterSerial{0};
std::atomic<ULONGLONG> g_renderWriterTick{0};
std::atomic<ULONGLONG> g_renderConsoleSerial{0};
std::atomic<ULONGLONG> g_renderConsoleTick{0};

void EnsureTransientRenderProbeOnCurrentThread();
void DetachTransientRenderProbesOnCurrentThread();

void ProbeLog(const wchar_t* format, ...) {
    wchar_t message[4096]{};
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(message, _countof(message), _TRUNCATE, format, args);
    va_end(args);
    ModLog(L"UserList: %s", message);
}

// Probe callbacks do no private ABI reads, UI writes, or row repairs.
// The native view-model may send property changes on a different thread;
// logging only is safe and prevents reentrancy into LogonUX setters.
struct VmProbeCallbackLifetime {
    VmProbeCallbackLifetime() {
        g_vmProbeCallbacksInFlight.fetch_add(1, std::memory_order_acq_rel);
    }
    ~VmProbeCallbackLifetime() {
        g_vmProbeCallbacksInFlight.fetch_sub(1, std::memory_order_acq_rel);
    }
};

void RemoveVmPropertyProbeRegistration(
    const std::shared_ptr<VmPropertyProbeRegistration>& registration) {
    AcquireSRWLockExclusive(&g_vmPropertyProbeLock);
    auto& list = g_vmPropertyProbeRegistrations;
    list.erase(std::remove(list.begin(), list.end(), registration), list.end());
    ReleaseSRWLockExclusive(&g_vmPropertyProbeLock);
}

void DetachVmPropertyProbe(
    const std::shared_ptr<VmPropertyProbeRegistration>& registration,
    bool removeHandler) {
    if (!registration || !registration->active.exchange(
            false, std::memory_order_acq_rel)) {
        return;
    }
    // WinRT event removal must occur on the original XAML thread.
    if (removeHandler &&
        registration->xamlThreadId == GetCurrentThreadId()) {
        try {
            if (registration->unloadedAttached) {
                if (auto presenter = registration->presenter.get()) {
                    presenter.Unloaded(registration->unloadedToken);
                }
            }
        } catch (...) {}
        try {
            if (auto notify = registration->model.get()) {
                notify.PropertyChanged(registration->token);
            }
        } catch (const winrt::hresult_error& e) {
            ProbeLog(L"VM_PROPERTY_PROBE detach exception hr=0x%08X model=%p",
                static_cast<unsigned>(e.code().value), registration->modelIdentity);
        } catch (...) {
            ProbeLog(L"VM_PROPERTY_PROBE detach exception model=%p",
                registration->modelIdentity);
        }
    }
    ProbeLog(L"VM_PROPERTY_PROBE detached account=[%s] model=%p events=%u",
        registration->qualifiedName.c_str(), registration->modelIdentity,
        registration->eventCount.load(std::memory_order_relaxed));
    RemoveVmPropertyProbeRegistration(registration);
}

[[maybe_unused]] void DetachVmPropertyProbesForPresenter(void* presenterIdentity) {
    if (!presenterIdentity ||
        !g_vmPropertyProbeEnabled.load(std::memory_order_acquire)) return;
    std::vector<std::shared_ptr<VmPropertyProbeRegistration>> matches;
    AcquireSRWLockShared(&g_vmPropertyProbeLock);
    for (const auto& reg : g_vmPropertyProbeRegistrations) {
        if (reg && reg->presenterIdentity == presenterIdentity &&
            reg->xamlThreadId == GetCurrentThreadId()) matches.push_back(reg);
    }
    ReleaseSRWLockShared(&g_vmPropertyProbeLock);
    for (const auto& reg : matches) DetachVmPropertyProbe(reg, true);
}

void DetachVmPropertyProbesOnCurrentThread() {
    const DWORD tid = GetCurrentThreadId();
    std::vector<std::shared_ptr<VmPropertyProbeRegistration>> matches;
    AcquireSRWLockShared(&g_vmPropertyProbeLock);
    for (const auto& reg : g_vmPropertyProbeRegistrations) {
        if (reg && reg->xamlThreadId == tid) matches.push_back(reg);
    }
    ReleaseSRWLockShared(&g_vmPropertyProbeLock);
    for (const auto& reg : matches) DetachVmPropertyProbe(reg, true);
}

void AttachVmPropertyProbeForPresenter(
    const FrameworkElement& presenter,
    const std::wstring& qualifiedName) {
    if (!presenter || qualifiedName.empty() ||
        !g_vmPropertyProbeEnabled.load(std::memory_order_acquire) ||
        g_unloading.load(std::memory_order_acquire)) return;

    Data::INotifyPropertyChanged notify{nullptr};
    try {
        const auto dc = presenter.DataContext();
        if (dc) notify = dc.try_as<Data::INotifyPropertyChanged>();
    } catch (...) {}
    if (!notify) return; // Fail closed on builds without WinRT notification.

    const DWORD tid = GetCurrentThreadId();
    void* presenterIdentity = reinterpret_cast<void*>(winrt::get_abi(presenter));
    void* modelIdentity = reinterpret_cast<void*>(winrt::get_abi(notify));
    if (!presenterIdentity || !modelIdentity) return;
    std::vector<std::shared_ptr<VmPropertyProbeRegistration>> stale;
    bool alreadySubscribed = false;
    AcquireSRWLockShared(&g_vmPropertyProbeLock);
    for (const auto& reg : g_vmPropertyProbeRegistrations) {
        if (!reg || reg->xamlThreadId != tid) continue;
        // A recycled presenter must not retain its old subscription.
        const bool missing = !reg->presenter.get() || !reg->model.get();
        if (missing || reg->presenterIdentity == presenterIdentity) {
            if (!missing && reg->active.load(std::memory_order_acquire) &&
                reg->presenterIdentity == presenterIdentity &&
                reg->modelIdentity == modelIdentity) {
                alreadySubscribed = true;
            } else {
                stale.push_back(reg);
            }
        }
    }
    ReleaseSRWLockShared(&g_vmPropertyProbeLock);
    for (const auto& old : stale) DetachVmPropertyProbe(old, true);
    if (alreadySubscribed) return;

    auto reg = std::make_shared<VmPropertyProbeRegistration>();
    reg->presenter = winrt::make_weak(presenter);
    reg->model = winrt::make_weak(notify);
    if (!reg->model.get()) {
        ProbeLog(L"VM_PROPERTY_PROBE unavailable: model does not support weak refs");
        return;
    }
    reg->xamlThreadId = tid;
    reg->presenterIdentity = presenterIdentity;
    reg->modelIdentity = modelIdentity;
    reg->qualifiedName = qualifiedName;
    std::weak_ptr<VmPropertyProbeRegistration> weakReg = reg;
    try {
        reg->token = notify.PropertyChanged(
            [weakReg](auto const&, Data::PropertyChangedEventArgs const& args) {
                VmProbeCallbackLifetime lifetime;
                auto reg = weakReg.lock();
                if (!reg || !reg->active.load(std::memory_order_acquire) ||
                    g_unloading.load(std::memory_order_acquire)) return;
                const unsigned index = reg->eventCount.fetch_add(
                    1, std::memory_order_acq_rel);
                if (index > 256) return;
                if (index == 256) {
                    ProbeLog(L"VM_PROPERTY_PROBE capped at 256 events model=%p account=\"%s\"",
                        reg->modelIdentity, reg->qualifiedName.c_str());
                    return;
                }
                std::wstring property = L"<all>";
                try {
                    const auto name = args.PropertyName();
                    if (!name.empty()) property.assign(name.c_str(), name.size());
                } catch (...) { property = L"<unavailable>"; }
                if (property.size() > 128) property.resize(128);
                for (auto& ch : property) {
                    if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
                }
                ProbeLog(L"VM_PROPERTY_PROBE event=%u account=\"%s\" property=\"%s\" model=%p presenter=%p ownerTid=%lu eventTid=%lu",
                    index + 1, reg->qualifiedName.c_str(), property.c_str(),
                    reg->modelIdentity, reg->presenterIdentity,
                    reg->xamlThreadId, GetCurrentThreadId());
                if (g_vmStateCorrelationEnabled.load(std::memory_order_acquire)) {
                    // A selected-credential broadcast can target all six
                    // models in one frame. Throttle these non-status changes;
                    // always record actual status/visibility changes.
                    const bool selected = (property == L"SelectedCredential");
                    bool shouldQueue = !selected;
                    if (selected) {
                        const ULONGLONG now = GetTickCount64();
                        ULONGLONG last = g_vmCorrelationLastSelectionTick.load(
                            std::memory_order_acquire);
                        while ((last == 0 || now - last >= 250) &&
                               !g_vmCorrelationLastSelectionTick.compare_exchange_weak(
                                   last, now, std::memory_order_acq_rel)) {}
                        shouldQueue = (last == 0 || now - last >= 250);
                    }
                    if (shouldQueue) QueueVmCorrelationSample(1);
                }
            });
    } catch (const winrt::hresult_error& e) {
        reg->active.store(false, std::memory_order_release);
        ProbeLog(L"VM_PROPERTY_PROBE subscribe failed hr=0x%08X account=\"%s\"",
            static_cast<unsigned>(e.code().value), qualifiedName.c_str());
        return;
    } catch (...) {
        reg->active.store(false, std::memory_order_release);
        ProbeLog(L"VM_PROPERTY_PROBE subscribe failed account=\"%s\"",
            qualifiedName.c_str());
        return;
    }
    try {
        reg->unloadedToken = presenter.Unloaded(
            [weakReg](auto const&, auto const&) {
                // Virtualization can unload a row without changing its
                // DataContext; retire the native event subscription promptly.
                if (auto reg = weakReg.lock()) {
                    DetachVmPropertyProbe(reg, true);
                }
            });
        reg->unloadedAttached = true;
    } catch (...) {
        // The DataContext watcher and mod-unload cleanup still detach us.
        ProbeLog(L"VM_PROPERTY_PROBE presenter Unloaded watcher unavailable");
    }
    AcquireSRWLockExclusive(&g_vmPropertyProbeLock);
    g_vmPropertyProbeRegistrations.push_back(reg);
    ReleaseSRWLockExclusive(&g_vmPropertyProbeLock);
    ProbeLog(L"VM_PROPERTY_PROBE attached account=\"%s\" model=%p presenter=%p thread=%lu",
        qualifiedName.c_str(), modelIdentity, presenterIdentity, tid);
    if (g_vmStateCorrelationEnabled.load(std::memory_order_acquire)) {
        QueueVmCorrelationSample(4);
    }
}

void RemovePresenterDataContextRegistration(
    const std::shared_ptr<PresenterDataContextRegistration>& registration) {

    AcquireSRWLockExclusive(
        &g_presenterDataContextLock);

    auto it = std::remove(
        g_presenterDataContextRegistrations.begin(),
        g_presenterDataContextRegistrations.end(),
        registration);

    g_presenterDataContextRegistrations.erase(
        it,
        g_presenterDataContextRegistrations.end());

    ReleaseSRWLockExclusive(
        &g_presenterDataContextLock);
}

void DetachPresenterDataContextWatcher(
    const std::shared_ptr<PresenterDataContextRegistration>& registration,
    bool removeHandler) {

    if (!registration ||
        !registration->active.exchange(
            false,
            std::memory_order_acq_rel)) {
        return;
    }

    if (removeHandler) {
        try {
            if (auto presenter =
                    registration->presenter.get()) {
                presenter.DataContextChanged(
                    registration->token);
            }
        } catch (...) {
        }
    }

    RemovePresenterDataContextRegistration(
        registration);
}

void DetachPresenterDataContextWatchersOnCurrentThread() {
    const DWORD currentThreadId =
        GetCurrentThreadId();

    std::vector<
        std::shared_ptr<PresenterDataContextRegistration>>
        registrations;

    AcquireSRWLockShared(
        &g_presenterDataContextLock);
    registrations =
        g_presenterDataContextRegistrations;
    ReleaseSRWLockShared(
        &g_presenterDataContextLock);

    for (const auto& registration :
         registrations) {
        if (registration &&
            registration->xamlThreadId ==
                currentThreadId) {
            DetachPresenterDataContextWatcher(
                registration,
                true);
        }
    }
}

void PruneExpiredPresenterDataContextWatchersOnCurrentThread() {
    const DWORD currentThreadId =
        GetCurrentThreadId();

    std::vector<
        std::shared_ptr<PresenterDataContextRegistration>>
        registrations;

    AcquireSRWLockShared(
        &g_presenterDataContextLock);
    registrations =
        g_presenterDataContextRegistrations;
    ReleaseSRWLockShared(
        &g_presenterDataContextLock);

    for (const auto& registration :
         registrations) {
        if (!registration ||
            registration->xamlThreadId !=
                currentThreadId ||
            !registration->active.load(
                std::memory_order_acquire)) {
            continue;
        }

        bool expired = false;
        try {
            expired =
                !registration->presenter.get();
        } catch (...) {
            expired = true;
        }

        if (expired) {
            // The XAML source is already gone, so there is no event token left
            // to detach. Retire only our bookkeeping entry.
            DetachPresenterDataContextWatcher(
                registration,
                false);
        }
    }
}

bool HasPresenterDataContextWatcher(
    DWORD threadId,
    void* presenterIdentity) {

    if (!threadId || !presenterIdentity) {
        return false;
    }

    bool found = false;

    AcquireSRWLockShared(
        &g_presenterDataContextLock);

    for (const auto& registration :
         g_presenterDataContextRegistrations) {
        if (registration &&
            registration->active.load(
                std::memory_order_acquire) &&
            registration->xamlThreadId ==
                threadId &&
            registration->presenterIdentity ==
                presenterIdentity) {

            // A destroyed presenter drops its event source, but the weak
            // registration can outlive it until module cleanup. Do not let
            // allocator address reuse make a new presenter look already
            // watched.
            try {
                if (registration->presenter.get()) {
                    found = true;
                    break;
                }
            } catch (...) {
            }
        }
    }

    ReleaseSRWLockShared(
        &g_presenterDataContextLock);

    return found;
}

void AttachPresenterDataContextWatcher(
    const FrameworkElement& presenter) {

    if (!presenter ||
        g_unloading.load(
            std::memory_order_acquire)) {
        return;
    }

    const DWORD threadId =
        GetCurrentThreadId();

    PruneExpiredPresenterDataContextWatchersOnCurrentThread();

    void* presenterIdentity =
        reinterpret_cast<void*>(
            winrt::get_abi(presenter));

    if (!presenterIdentity ||
        HasPresenterDataContextWatcher(
            threadId,
            presenterIdentity)) {
        return;
    }

    auto registration =
        std::make_shared<
            PresenterDataContextRegistration>();

    registration->presenter =
        winrt::make_weak(presenter);
    registration->xamlThreadId =
        threadId;
    registration->presenterIdentity =
        presenterIdentity;

    std::weak_ptr<PresenterDataContextRegistration>
        weakRegistration =
            registration;

    try {
        registration->token =
            presenter.DataContextChanged(
                [weakRegistration](
                    auto const&,
                    auto const&) {

                    auto registration =
                        weakRegistration.lock();

                    if (!registration ||
                        !registration->active.load(
                            std::memory_order_acquire) ||
                        g_unloading.load(
                            std::memory_order_acquire)) {
                        return;
                    }

                    // Drop previous VM event association immediately when
                    // Windows recycles this presenter to a different account.
                    DetachVmPropertyProbesForPresenter(
                        registration->presenterIdentity);

                    // Do not refresh synchronously inside Windows' own
                    // DataContext mutation. The retained LayoutUpdated bridge
                    // will run after bindings/template state have caught up,
                    // and the generation makes that repair bounded/coalesced.
                    const unsigned generation =
                        g_layoutRepairGeneration.fetch_add(
                            1,
                            std::memory_order_acq_rel) + 1;
                    InvalidateRecentCleanPresenterPass();

                    if (g_enableLayoutDiagnostics.load(
                            std::memory_order_relaxed)) {
                        ProbeLog(
                            L"ROW_DATACONTEXT changed presenter=%p generation=%u thread=%lu",
                            registration->presenterIdentity,
                            generation,
                            GetCurrentThreadId());
                    }
                });
    } catch (...) {
        registration->active.store(
            false,
            std::memory_order_release);
        return;
    }

    AcquireSRWLockExclusive(
        &g_presenterDataContextLock);
    g_presenterDataContextRegistrations.push_back(
        registration);
    ReleaseSRWLockExclusive(
        &g_presenterDataContextLock);

    if (g_enableLayoutDiagnostics.load(
            std::memory_order_relaxed)) {
        ProbeLog(
            L"ROW_DATACONTEXT watcher attached presenter=%p thread=%lu",
            presenterIdentity,
            threadId);
    }
}

bool GetOsBuild(DWORD* build, DWORD* ubr) {
    if (!build || !ubr) {
        return false;
    }

    *build = 0;
    *ubr = 0;

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return false;
    }

    using RtlGetVersion_t = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    auto rtlGetVersion = reinterpret_cast<RtlGetVersion_t>(
        GetProcAddress(ntdll, "RtlGetVersion"));

    if (!rtlGetVersion) {
        return false;
    }

    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);

    if (rtlGetVersion(&version) != 0) {
        return false;
    }

    DWORD value = 0;
    DWORD valueSize = sizeof(value);
    RegGetValueW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        L"UBR",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &valueSize);

    *build = version.dwBuildNumber;
    *ubr = value;
    return true;
}

bool GetPeIdentity(
    HMODULE module,
    DWORD* timeDateStamp,
    DWORD* sizeOfImage) {

    if (!module || !timeDateStamp || !sizeOfImage) {
        return false;
    }

    auto base = reinterpret_cast<const BYTE*>(module);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);

    if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
        dos->e_lfanew <= 0 ||
        static_cast<size_t>(dos->e_lfanew) > 0x100000) {
        return false;
    }

    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        base + dos->e_lfanew);

    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return false;
    }

    *timeDateStamp = nt->FileHeader.TimeDateStamp;
    *sizeOfImage = nt->OptionalHeader.SizeOfImage;
    return true;
}

bool IsExecutableAddress(void* address) {
    if (!address) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(address, &mbi, sizeof(mbi))) {
        return false;
    }

    if (mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        return false;
    }

    const DWORD protect = mbi.Protect & 0xFF;
    return protect == PAGE_EXECUTE ||
           protect == PAGE_EXECUTE_READ ||
           protect == PAGE_EXECUTE_READWRITE ||
           protect == PAGE_EXECUTE_WRITECOPY;
}



void ResolveWinStringApis() {
    HMODULE module = GetModuleHandleW(L"combase.dll");
    if (!module) {
        module = LoadLibraryExW(
            L"combase.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }

    if (!module) {
        ProbeLog(L"WINSTRING combase.dll unavailable error=%lu",
                 GetLastError());
        return;
    }

    g_windowsGetStringRawBuffer =
        reinterpret_cast<WindowsGetStringRawBuffer_t>(
            GetProcAddress(module, "WindowsGetStringRawBuffer"));
    g_windowsDeleteString =
        reinterpret_cast<WindowsDeleteString_t>(
            GetProcAddress(module, "WindowsDeleteString"));

    ProbeLog(
        L"WINSTRING WindowsGetStringRawBuffer=%p WindowsDeleteString=%p",
        reinterpret_cast<void*>(g_windowsGetStringRawBuffer),
        reinterpret_cast<void*>(g_windowsDeleteString));
}

std::wstring CopyAndDeleteHString(void* hstring) {
    if (!hstring) {
        return L"";
    }

    std::wstring result;

    if (g_windowsGetStringRawBuffer) {
        UINT32 length = 0;
        PCWSTR raw = g_windowsGetStringRawBuffer(hstring, &length);
        if (raw) {
            result.assign(raw, raw + length);
        }
    } else {
        result = L"<WindowsGetStringRawBuffer unavailable>";
    }

    if (g_windowsDeleteString) {
        g_windowsDeleteString(hstring);
    }

    return result;
}

std::wstring ReadStringProperty(
    GetHStringAbi getter,
    void* userVm,
    HRESULT* hrOut = nullptr) {

    if (!getter || !userVm) {
        if (hrOut) {
            *hrOut = E_POINTER;
        }
        return L"<unavailable>";
    }

    void* hstring = nullptr;
    HRESULT hr = getter(userVm, &hstring);

    if (hrOut) {
        *hrOut = hr;
    }

    if (FAILED(hr)) {
        wchar_t buffer[64]{};
        _snwprintf_s(
            buffer, _countof(buffer), _TRUNCATE,
            L"<failed 0x%08X>", static_cast<unsigned>(hr));
        return buffer;
    }

    return CopyAndDeleteHString(hstring);
}

int ReadBoolProperty(
    GetBoolAbi getter,
    void* userVm,
    HRESULT* hrOut = nullptr) {

    if (!getter || !userVm) {
        if (hrOut) {
            *hrOut = E_POINTER;
        }
        return -1;
    }

    unsigned char value = 0;
    HRESULT hr = getter(userVm, &value);

    if (hrOut) {
        *hrOut = hr;
    }

    return SUCCEEDED(hr) ? (value ? 1 : 0) : -1;
}

Media::Imaging::BitmapImage ReadSmallUserTileBitmapImage(
    void* userVm,
    HRESULT* hrOut = nullptr) {

    if (hrOut) {
        *hrOut = E_FAIL;
    }

    if (!userVm ||
        !g_getSmallUserTileImage ||
        !g_userTileImageGetBitmapImage) {
        if (hrOut) {
            *hrOut = E_POINTER;
        }
        return nullptr;
    }

    void* tileObject = nullptr;
    HRESULT hr =
        g_getSmallUserTileImage(
            userVm,
            &tileObject);

    if (FAILED(hr) || !tileObject) {
        if (hrOut) {
            *hrOut = hr;
        }
        return nullptr;
    }

    void* tileVm = nullptr;
    hr = reinterpret_cast<IUnknown*>(
             tileObject)->QueryInterface(
                 g_userTileImageVmPublicIid,
                 &tileVm);

    reinterpret_cast<IUnknown*>(
        tileObject)->Release();
    tileObject = nullptr;

    if (FAILED(hr) || !tileVm) {
        if (hrOut) {
            *hrOut = hr;
        }
        return nullptr;
    }

    void* bitmapRaw = nullptr;
    hr = g_userTileImageGetBitmapImage(
        tileVm,
        &bitmapRaw);

    reinterpret_cast<IUnknown*>(
        tileVm)->Release();
    tileVm = nullptr;

    if (hrOut) {
        *hrOut = hr;
    }

    if (FAILED(hr) || !bitmapRaw) {
        return nullptr;
    }

    // ABI getter returns the default BitmapImage interface with an owning
    // reference. Hand that reference directly to the projected object.
    return Media::Imaging::BitmapImage(
        bitmapRaw,
        winrt::take_ownership_from_abi);
}

std::wstring SafeClassName(
    winrt::Windows::Foundation::IInspectable const& object) {

    if (!object) {
        return L"<null>";
    }

    try {
        auto name = winrt::get_class_name(object);
        return std::wstring(name.c_str(), name.size());
    } catch (...) {
        return L"<class-name-error>";
    }
}

FrameworkElement FindVisualRoot(FrameworkElement element) {
    if (!element) {
        return nullptr;
    }

    FrameworkElement current = element;

    try {
        for (int i = 0; i < 128; ++i) {
            auto parent =
                Media::VisualTreeHelper::GetParent(current);
            if (!parent) {
                break;
            }

            auto parentElement =
                parent.try_as<FrameworkElement>();

            if (!parentElement) {
                break;
            }

            current = parentElement;
        }
    } catch (...) {
    }

    return current;
}

std::wstring RuntimeClassNameFromUnknown(IUnknown* object) {
    if (!object) {
        return L"<null>";
    }

    try {
        winrt::Windows::Foundation::IInspectable inspectable{nullptr};
        winrt::copy_from_abi(inspectable, object);
        return SafeClassName(inspectable);
    } catch (...) {
        return L"<class-name-error>";
    }
}

void LogUserViewModel(void* userVm, const wchar_t* source) {
    if (!userVm) {
        ProbeLog(L"USERVM source=%s iface=<null>",
                 source ? source : L"<unknown>");
        return;
    }

    HRESULT displayNameHr = E_FAIL;
    HRESULT loggedInHr = E_FAIL;
    HRESULT visibleHr = E_FAIL;
    HRESULT statusHr = E_FAIL;

    const std::wstring displayName =
        ReadStringProperty(
            g_getDisplayName,
            userVm,
            &displayNameHr);

    const int loggedIn =
        ReadBoolProperty(
            g_getIsLoggedIn,
            userVm,
            &loggedInHr);

    const int visible =
        ReadBoolProperty(
            g_getIsLogonStatusVisible,
            userVm,
            &visibleHr);

    const std::wstring status =
        ReadStringProperty(
            g_getLogonStatus,
            userVm,
            &statusHr);

    ++g_userVmObservationCount;

    const bool changed =
        !g_haveLastUserState ||
        g_lastUserVmIdentity != userVm ||
        g_lastDisplayName != displayName ||
        g_lastLoggedIn != loggedIn ||
        g_lastVisible != visible ||
        g_lastStatus != status;

    if (changed) {
        ProbeLog(
            L"USERVM_STATE observation=%u source=%s iface=%p "
            L"DisplayName=\"%s\" hr=0x%08X "
            L"IsLoggedIn=%d hr=0x%08X "
            L"IsLogonStatusVisible=%d hr=0x%08X "
            L"LogonStatus=\"%s\" hr=0x%08X",
            g_userVmObservationCount,
            source ? source : L"<unknown>",
            userVm,
            displayName.c_str(),
            static_cast<unsigned>(displayNameHr),
            loggedIn,
            static_cast<unsigned>(loggedInHr),
            visible,
            static_cast<unsigned>(visibleHr),
            status.c_str(),
            static_cast<unsigned>(statusHr));

        g_haveLastUserState = true;
        g_lastUserVmIdentity = userVm;
        g_lastDisplayName = displayName;
        g_lastLoggedIn = loggedIn;
        g_lastVisible = visible;
        g_lastStatus = status;
    }
}



enum class WtsLockState : int {
    Unknown = -1,
    Locked = 0,
    Unlocked = 1,
};

const wchar_t* WtsLockStateName(WtsLockState state) {
    switch (state) {
        case WtsLockState::Locked: return L"Locked";
        case WtsLockState::Unlocked: return L"Unlocked";
        case WtsLockState::Unknown:
        default: return L"Unknown";
    }
}

struct WtsLockEventStateEntry {
    DWORD sessionId = 0;
    WtsLockState state = WtsLockState::Unknown;
};

SRWLOCK g_wtsLockEventStateLock = SRWLOCK_INIT;
std::vector<WtsLockEventStateEntry> g_wtsLockEventStates;

void SetWtsLockEventState(
    DWORD sessionId,
    WtsLockState state) {

    AcquireSRWLockExclusive(
        &g_wtsLockEventStateLock);

    try {
        for (auto& entry : g_wtsLockEventStates) {
            if (entry.sessionId == sessionId) {
                entry.state = state;
                ReleaseSRWLockExclusive(
                    &g_wtsLockEventStateLock);
                return;
            }
        }

        if (state != WtsLockState::Unknown) {
            g_wtsLockEventStates.push_back(
                WtsLockEventStateEntry{
                    sessionId,
                    state});
        }
    } catch (...) {
    }

    ReleaseSRWLockExclusive(
        &g_wtsLockEventStateLock);
}

void ForgetWtsLockEventState(DWORD sessionId) {
    AcquireSRWLockExclusive(
        &g_wtsLockEventStateLock);

    for (auto it = g_wtsLockEventStates.begin();
         it != g_wtsLockEventStates.end();) {
        if (it->sessionId == sessionId) {
            it = g_wtsLockEventStates.erase(it);
        } else {
            ++it;
        }
    }

    ReleaseSRWLockExclusive(
        &g_wtsLockEventStateLock);
}

WtsLockState ReadWtsLockEventState(DWORD sessionId) {
    WtsLockState result = WtsLockState::Unknown;

    AcquireSRWLockShared(
        &g_wtsLockEventStateLock);

    for (const auto& entry : g_wtsLockEventStates) {
        if (entry.sessionId == sessionId) {
            result = entry.state;
            break;
        }
    }

    ReleaseSRWLockShared(
        &g_wtsLockEventStateLock);
    return result;
}

struct WtsSessionSnapshot {
    DWORD sessionId = 0;
    WTS_CONNECTSTATE_CLASS state = WTSDown;
    std::wstring stationName;
    std::wstring userName;
    std::wstring domainName;
    std::wstring clientName;
    USHORT protocolType = 0xFFFF;
    WtsLockState lockState = WtsLockState::Unknown;
    std::wstring nativeTransientStatus;
    DWORD nativeTransientWriterPid = 0;
    ULONGLONG nativeTransientSerial = 0;
};

// alpha4v: legacy AuthUI-like user-row status is a process-local snapshot.
// Keep the cached WTS information separate from live unassigned-session/footer
// diagnostics and the optional native transient bridge. Never freeze a fake
// UserViewModel or insert an item into the Windows-owned user list.
SRWLOCK g_rowSessionSnapshotLock = SRWLOCK_INIT;
std::vector<WtsSessionSnapshot> g_rowSessionSnapshot;
bool g_rowSessionSnapshotReady = false;
DWORD g_rowSnapshotActiveConsoleSession = 0xFFFFFFFF;

void ClearRowSessionSnapshot() {
    AcquireSRWLockExclusive(&g_rowSessionSnapshotLock);
    g_rowSessionSnapshot.clear();
    g_rowSessionSnapshotReady = false;
    g_rowSnapshotActiveConsoleSession = 0xFFFFFFFF;
    ReleaseSRWLockExclusive(&g_rowSessionSnapshotLock);
}

DWORD GetRowActiveConsoleSession() {
    if (g_liveSessionRowUpdates.load(std::memory_order_acquire)) {
        return WTSGetActiveConsoleSessionId();
    }
    AcquireSRWLockShared(&g_rowSessionSnapshotLock);
    const DWORD cached = g_rowSnapshotActiveConsoleSession;
    const bool ready = g_rowSessionSnapshotReady;
    ReleaseSRWLockShared(&g_rowSessionSnapshotLock);
    return ready ? cached : WTSGetActiveConsoleSessionId();
}

const wchar_t* WtsStateName(WTS_CONNECTSTATE_CLASS state) {
    switch (state) {
        case WTSActive: return L"Active";
        case WTSConnected: return L"Connected";
        case WTSConnectQuery: return L"ConnectQuery";
        case WTSShadow: return L"Shadow";
        case WTSDisconnected: return L"Disconnected";
        case WTSIdle: return L"Idle";
        case WTSListen: return L"Listen";
        case WTSReset: return L"Reset";
        case WTSDown: return L"Down";
        case WTSInit: return L"Init";
        default: return L"<unknown>";
    }
}

std::wstring QueryWtsString(
    DWORD sessionId,
    WTS_INFO_CLASS infoClass) {

    LPWSTR buffer = nullptr;
    DWORD bytes = 0;

    if (!WTSQuerySessionInformationW(
            WTS_CURRENT_SERVER_HANDLE,
            sessionId,
            infoClass,
            &buffer,
            &bytes) ||
        !buffer) {
        return {};
    }

    std::wstring value(buffer);
    WTSFreeMemory(buffer);
    return value;
}

USHORT QueryWtsProtocolType(DWORD sessionId) {
    LPWSTR buffer = nullptr;
    DWORD bytes = 0;

    if (!WTSQuerySessionInformationW(
            WTS_CURRENT_SERVER_HANDLE,
            sessionId,
            WTSClientProtocolType,
            &buffer,
            &bytes) ||
        !buffer ||
        bytes < sizeof(USHORT)) {

        if (buffer) {
            WTSFreeMemory(buffer);
        }
        return 0xFFFF;
    }

    const USHORT value =
        *reinterpret_cast<const USHORT*>(buffer);

    WTSFreeMemory(buffer);
    return value;
}

WtsLockState QueryWtsLockState(DWORD sessionId) {
    LPWSTR buffer = nullptr;
    DWORD bytes = 0;

    if (!WTSQuerySessionInformationW(
            WTS_CURRENT_SERVER_HANDLE,
            sessionId,
            WTSSessionInfoEx,
            &buffer,
            &bytes) ||
        !buffer ||
        bytes < sizeof(WTSINFOEXW)) {

        if (buffer) {
            WTSFreeMemory(buffer);
        }
        return WtsLockState::Unknown;
    }

    const auto* info =
        reinterpret_cast<const WTSINFOEXW*>(buffer);

    WtsLockState result = WtsLockState::Unknown;

    if (info->Level == 1) {
        const LONG flags =
            info->Data.WTSInfoExLevel1.SessionFlags;

        // Support starts at Windows 10 1809, where the documented meanings
        // are 0=locked and 1=unlocked. The Windows 7 reversal bug is outside
        // this mod's supported OS floor.
        if (flags == 0) {
            result = WtsLockState::Locked;
        } else if (flags == 1) {
            result = WtsLockState::Unlocked;
        }
    }

    WTSFreeMemory(buffer);
    return result;
}


ULONGLONG FileTimeKey(const FILETIME& value) {
    ULARGE_INTEGER key{};
    key.LowPart = value.dwLowDateTime;
    key.HighPart = value.dwHighDateTime;
    return key.QuadPart;
}

ULONGLONG QueryCurrentProcessCreateTimeKey() {
    FILETIME createTime{};
    FILETIME exitTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};

    if (!GetProcessTimes(
            GetCurrentProcess(),
            &createTime,
            &exitTime,
            &kernelTime,
            &userTime)) {
        return 0;
    }

    return FileTimeKey(createTime);
}

bool IsSameProcessInstanceAlive(
    DWORD processId,
    ULONGLONG expectedCreateTime) {

    if (!processId) {
        return false;
    }

    if (processId == GetCurrentProcessId()) {
        return expectedCreateTime == 0 ||
               expectedCreateTime == g_transientBridgeProcessCreateTime;
    }

    HANDLE process = OpenProcess(
        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        processId);

    if (!process) {
        return false;
    }

    bool alive =
        WaitForSingleObject(process, 0) == WAIT_TIMEOUT;

    if (alive && expectedCreateTime != 0) {
        FILETIME createTime{};
        FILETIME exitTime{};
        FILETIME kernelTime{};
        FILETIME userTime{};

        if (!GetProcessTimes(
                process,
                &createTime,
                &exitTime,
                &kernelTime,
                &userTime) ||
            FileTimeKey(createTime) != expectedCreateTime) {
            alive = false;
        }
    }

    CloseHandle(process);
    return alive;
}

template <size_t N>
void CopyTransientFixedString(
    wchar_t (&destination)[N],
    const std::wstring& value) {

    if constexpr (N != 0) {
        wcsncpy_s(
            destination,
            N,
            value.c_str(),
            _TRUNCATE);
    }
}

std::wstring TransientReaderEventName(DWORD processId) {
    wchar_t name[192]{};
    _snwprintf_s(
        name,
        _countof(name),
        _TRUNCATE,
        L"%s%lu",
        kTransientBridgeReaderEventPrefix,
        processId);
    return name;
}

std::wstring TransientConsoleSessionEventName(DWORD sessionId) {
    wchar_t name[192]{};
    _snwprintf_s(
        name,
        _countof(name),
        _TRUNCATE,
        L"%s%lu",
        kTransientBridgeConsoleSessionEventPrefix,
        sessionId);
    return name;
}

bool LockTransientBridge(DWORD timeoutMs = 1000) {
    if (!g_transientBridgeMutex || !g_transientBridge) {
        return false;
    }

    const DWORD wait =
        WaitForSingleObject(
            g_transientBridgeMutex,
            timeoutMs);

    return wait == WAIT_OBJECT_0 ||
           wait == WAIT_ABANDONED;
}

void UnlockTransientBridge() {
    if (g_transientBridgeMutex) {
        ReleaseMutex(g_transientBridgeMutex);
    }
}

ULONGLONG ReadTransientBridgeSerial() {
    if (!g_transientBridge || !LockTransientBridge()) {
        return 0;
    }

    const ULONGLONG serial =
        g_transientBridge->serial;
    UnlockTransientBridge();
    return serial;
}

struct TransientBridgeSnapshot {
    ULONGLONG serial = 0;
    std::vector<TransientBridgeSlot> slots;
};

TransientBridgeSnapshot CaptureTransientBridgeSnapshot() {
    TransientBridgeSnapshot snapshot{};

    if (!g_transientBridge ||
        !LockTransientBridge(100)) {
        return snapshot;
    }

    snapshot.serial =
        g_transientBridge->serial;
    snapshot.slots.resize(
        kTransientBridgeSlotCount);
    memcpy(
        snapshot.slots.data(),
        g_transientBridge->slots,
        sizeof(g_transientBridge->slots));

    UnlockTransientBridge();
    return snapshot;
}

// Shared predicate is defined below with the other bridge classification
// helpers. Declare it here so the full row-path lookup shares the same
// disconnected-session exclusion as the fast path and session snapshot merge.
bool IsDisconnectedGenericNativeTransientStatusSlot(
    const TransientBridgeSlot& slot);

struct LiveTransientForUser {
    bool found = false;
    std::wstring text;
    DWORD writerPid = 0;
    ULONGLONG serial = 0;
};

LiveTransientForUser CaptureLiveTransientForQualifiedName(
    const std::wstring& qualifiedName,
    DWORD preferredSessionId = 0xFFFFFFFF) {

    LiveTransientForUser result{};

    if (qualifiedName.empty() ||
        !g_transientBridge ||
        !g_showNativeTransientStatus.load(
            std::memory_order_acquire)) {
        return result;
    }

    const auto snapshot =
        CaptureTransientBridgeSnapshot();

    const TransientBridgeSlot* newest = nullptr;

    auto consider = [&](const TransientBridgeSlot& slot) {
        if (slot.sessionId == 0xFFFFFFFF ||
            slot.serial == 0 ||
            slot.text[0] == L'\0' ||
            slot.lastUser[0] == L'\0' ||
            _wcsicmp(
                slot.lastUser,
                qualifiedName.c_str()) != 0 ||
            !IsSameProcessInstanceAlive(
                slot.writerPid,
                slot.writerCreateTime) ||
            IsDisconnectedGenericNativeTransientStatusSlot(slot)) {
            // Do not resurface the native "Please wait" / "Locking"
            // of an already-disconnected account through the full row path.
            return;
        }

        if (!newest ||
            slot.serial > newest->serial) {
            newest = &slot;
        }
    };

    // Prefer the WTS session that the row was matched to. This matters when
    // one account owns more than one session: a transient from a different
    // session must not jump rows merely because the qualified name matches.
    if (preferredSessionId != 0xFFFFFFFF) {
        for (const auto& slot : snapshot.slots) {
            if (slot.sessionId == preferredSessionId) {
                consider(slot);
            }
        }
    }

    if (!newest &&
        preferredSessionId == 0xFFFFFFFF) {
        for (const auto& slot : snapshot.slots) {
            consider(slot);
        }
    }

    if (!newest) {
        return result;
    }

    result.found = true;
    result.text = newest->text;
    result.writerPid = newest->writerPid;
    result.serial = newest->serial;
    return result;
}

std::vector<HANDLE> OpenTransientWriterWatchesForConsoleReader() {
    std::vector<HANDLE> handles;

    if (!g_transientBridge ||
        g_unloading.load(std::memory_order_acquire)) {
        return handles;
    }

    DWORD processSession = 0xFFFFFFFF;
    if (!ProcessIdToSessionId(
            GetCurrentProcessId(),
            &processSession) ||
        processSession == 0xFFFFFFFF ||
        processSession != WTSGetActiveConsoleSessionId()) {
        return handles;
    }

    std::vector<TransientBridgeSlot> slots(
        kTransientBridgeSlotCount);

    if (!LockTransientBridge()) {
        return handles;
    }

    memcpy(
        slots.data(),
        g_transientBridge->slots,
        sizeof(g_transientBridge->slots));
    UnlockTransientBridge();

    struct WriterKey {
        DWORD pid = 0;
        ULONGLONG createTime = 0;
    };
    std::vector<WriterKey> writers;

    for (const auto& slot : slots) {
        if (slot.sessionId == 0xFFFFFFFF ||
            slot.writerPid == 0 ||
            slot.writerPid == GetCurrentProcessId() ||
            slot.writerCreateTime == 0 ||
            slot.text[0] == L'\0') {
            continue;
        }

        const auto duplicate =
            std::find_if(
                writers.begin(),
                writers.end(),
                [&](const WriterKey& item) {
                    return item.pid == slot.writerPid &&
                           item.createTime == slot.writerCreateTime;
                });
        if (duplicate == writers.end()) {
            writers.push_back(
                WriterKey{
                    slot.writerPid,
                    slot.writerCreateTime});
        }
    }

    handles.reserve(writers.size());

    for (const auto& writer : writers) {
        HANDLE process =
            OpenProcess(
                SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE,
                writer.pid);
        if (!process) {
            continue;
        }

        bool correctInstance =
            WaitForSingleObject(process, 0) == WAIT_TIMEOUT;

        if (correctInstance) {
            FILETIME createTime{};
            FILETIME exitTime{};
            FILETIME kernelTime{};
            FILETIME userTime{};
            correctInstance =
                GetProcessTimes(
                    process,
                    &createTime,
                    &exitTime,
                    &kernelTime,
                    &userTime) &&
                FileTimeKey(createTime) == writer.createTime;
        }

        if (correctInstance) {
            handles.push_back(process);
        } else {
            CloseHandle(process);
        }
    }

    return handles;
}

void CloseTransientWriterWatches(
    std::vector<HANDLE>* handles) {

    if (!handles) {
        return;
    }

    for (HANDLE handle : *handles) {
        if (handle) {
            CloseHandle(handle);
        }
    }
    handles->clear();
}

bool QueryWtsConnectStateForBridge(
    DWORD sessionId,
    WTS_CONNECTSTATE_CLASS* state) {

    if (!state) {
        return false;
    }

    LPWSTR buffer = nullptr;
    DWORD bytes = 0;

    if (!WTSQuerySessionInformationW(
            WTS_CURRENT_SERVER_HANDLE,
            sessionId,
            WTSConnectState,
            &buffer,
            &bytes) ||
        !buffer ||
        bytes < sizeof(WTS_CONNECTSTATE_CLASS)) {

        if (buffer) {
            WTSFreeMemory(buffer);
        }
        return false;
    }

    *state =
        *reinterpret_cast<const WTS_CONNECTSTATE_CLASS*>(buffer);
    WTSFreeMemory(buffer);
    return true;
}

bool InitializeTransientBridge() {
    if (!g_showNativeTransientStatus.load(
            std::memory_order_acquire)) {
        return true;
    }

    g_transientBridgeProcessCreateTime =
        QueryCurrentProcessCreateTimeKey();

    g_transientBridgeMutex =
        CreateMutexW(
            nullptr,
            FALSE,
            kTransientBridgeMutexName);

    if (!g_transientBridgeMutex) {
        ProbeLog(
            L"TRANSIENT_BRIDGE CreateMutex failed gle=%lu; native transient status disabled",
            GetLastError());
        return false;
    }

    g_transientBridgeMapHandle =
        CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            static_cast<DWORD>(sizeof(TransientBridgeShared)),
            kTransientBridgeMapName);

    const DWORD mapCreateError = GetLastError();

    if (!g_transientBridgeMapHandle) {
        ProbeLog(
            L"TRANSIENT_BRIDGE CreateFileMapping failed gle=%lu; native transient status disabled",
            mapCreateError);
        CloseHandle(g_transientBridgeMutex);
        g_transientBridgeMutex = nullptr;
        return false;
    }

    g_transientBridge =
        reinterpret_cast<TransientBridgeShared*>(
            MapViewOfFile(
                g_transientBridgeMapHandle,
                FILE_MAP_ALL_ACCESS,
                0,
                0,
                sizeof(TransientBridgeShared)));

    if (!g_transientBridge) {
        ProbeLog(
            L"TRANSIENT_BRIDGE MapViewOfFile failed gle=%lu; native transient status disabled",
            GetLastError());
        CloseHandle(g_transientBridgeMapHandle);
        g_transientBridgeMapHandle = nullptr;
        CloseHandle(g_transientBridgeMutex);
        g_transientBridgeMutex = nullptr;
        return false;
    }

    bool initialized = false;
    if (LockTransientBridge()) {
        if (mapCreateError != ERROR_ALREADY_EXISTS ||
            g_transientBridge->magic != kTransientBridgeMagic ||
            g_transientBridge->version != kTransientBridgeVersion ||
            g_transientBridge->structSize != sizeof(TransientBridgeShared) ||
            g_transientBridge->slotCount != kTransientBridgeSlotCount) {

            ZeroMemory(
                g_transientBridge,
                sizeof(*g_transientBridge));
            g_transientBridge->magic = kTransientBridgeMagic;
            g_transientBridge->version = kTransientBridgeVersion;
            g_transientBridge->structSize =
                static_cast<DWORD>(sizeof(TransientBridgeShared));
            g_transientBridge->slotCount =
                static_cast<DWORD>(kTransientBridgeSlotCount);
            for (auto& slot : g_transientBridge->slots) {
                slot.sessionId = 0xFFFFFFFF;
                slot.protocolType = 0xFFFF;
                slot.wtsState = static_cast<DWORD>(WTSDown);
            }
        }
        initialized = true;
        UnlockTransientBridge();
    }

    if (!initialized) {
        ProbeLog(
            L"TRANSIENT_BRIDGE couldn't acquire initialization mutex; native transient status disabled");
        UnmapViewOfFile(g_transientBridge);
        g_transientBridge = nullptr;
        CloseHandle(g_transientBridgeMapHandle);
        g_transientBridgeMapHandle = nullptr;
        CloseHandle(g_transientBridgeMutex);
        g_transientBridgeMutex = nullptr;
        return false;
    }

    DWORD processSession = 0xFFFFFFFF;
    ProcessIdToSessionId(
        GetCurrentProcessId(),
        &processSession);

    std::wstring eventName;
    const bool activeConsoleReader =
        processSession != 0xFFFFFFFF &&
        processSession == WTSGetActiveConsoleSessionId();

    if (activeConsoleReader) {
        // Fast cross-process signal: the writer already knows the active console
        // session ID, so a session-named event avoids enumerating every process
        // for each 30-100 ms StatusMessage change. Fall back to the old per-PID
        // event only if the Global session event can't be created.
        eventName =
            TransientConsoleSessionEventName(
                processSession);
        g_transientBridgeReaderEvent =
            CreateEventW(
                nullptr,
                FALSE,
                FALSE,
                eventName.c_str());
    }

    if (!g_transientBridgeReaderEvent) {
        eventName =
            TransientReaderEventName(
                GetCurrentProcessId());
        g_transientBridgeReaderEvent =
            CreateEventW(
                nullptr,
                FALSE,
                FALSE,
                eventName.c_str());
    }

    if (!g_transientBridgeReaderEvent) {
        ProbeLog(
            L"TRANSIENT_BRIDGE CreateEvent failed gle=%lu; publishing remains available but this process won't receive bridge wakeups",
            GetLastError());
    }

    ProbeLog(
        L"TRANSIENT_BRIDGE ready mapCreated=%u map=\"%s\" readerEvent=\"%s\" fastConsoleReader=%u processCreateTime=0x%llX",
        mapCreateError == ERROR_ALREADY_EXISTS ? 0u : 1u,
        kTransientBridgeMapName,
        eventName.c_str(),
        activeConsoleReader ? 1u : 0u,
        static_cast<unsigned long long>(
            g_transientBridgeProcessCreateTime));

    return true;
}

void SignalActiveConsoleTransientReaders(
    ULONGLONG serial) {

    // Always wake our own worker, including before the first user signs in
    // and during Switch User when Windows reports no active console session.
    // The named console event below continues waking other LogonUI readers.
    if (g_transientBridgeReaderEvent) {
        SetEvent(g_transientBridgeReaderEvent);
    }

    const DWORD activeConsole =
        WTSGetActiveConsoleSessionId();

    // alpha4k fast path: one named event per active console session. This keeps
    // bridge->console delivery in the sub-frame path without paying for
    // WTSEnumerateProcessesW on every native TextProperty callback.
    if (activeConsole != 0xFFFFFFFF) {
        const std::wstring fastEventName =
            TransientConsoleSessionEventName(
                activeConsole);
        HANDLE fastEvent =
            OpenEventW(
                EVENT_MODIFY_STATE,
                FALSE,
                fastEventName.c_str());
        if (fastEvent) {
            const bool signaled =
                SetEvent(fastEvent) != FALSE;
            CloseHandle(fastEvent);

            ProbeLog(
                L"TRANSIENT_BRIDGE signal serial=%llu activeConsole=%lu fastSessionEvent=%u",
                static_cast<unsigned long long>(serial),
                activeConsole,
                signaled ? 1u : 0u);
            if (signaled) {
                return;
            }
        }
    }

    // Compatibility fallback for a console instance that had to create the old
    // per-PID reader event (or for a transient console-session event race).
    PWTS_PROCESS_INFOW processes = nullptr;
    DWORD count = 0;
    unsigned candidates = 0;
    unsigned signaled = 0;

    if (WTSEnumerateProcessesW(
            WTS_CURRENT_SERVER_HANDLE,
            0,
            1,
            &processes,
            &count) &&
        processes) {

        for (DWORD i = 0; i < count; ++i) {
            const auto& process = processes[i];

            if (process.SessionId != activeConsole ||
                !process.pProcessName ||
                _wcsicmp(
                    process.pProcessName,
                    L"LogonUI.exe") != 0) {
                continue;
            }

            ++candidates;

            const std::wstring eventName =
                TransientReaderEventName(
                    process.ProcessId);

            HANDLE eventHandle =
                OpenEventW(
                    EVENT_MODIFY_STATE,
                    FALSE,
                    eventName.c_str());

            if (eventHandle) {
                if (SetEvent(eventHandle)) {
                    ++signaled;
                }
                CloseHandle(eventHandle);
            }
        }

        WTSFreeMemory(processes);
    }

    ProbeLog(
        L"TRANSIENT_BRIDGE signal serial=%llu activeConsole=%lu fastSessionEvent=0 candidateLogonUI=%u signaled=%u",
        static_cast<unsigned long long>(serial),
        activeConsole,
        candidates,
        signaled);
}

TransientBridgeSlot* FindTransientBridgeSlotLocked(
    DWORD sessionId) {

    if (!g_transientBridge) {
        return nullptr;
    }

    for (auto& slot : g_transientBridge->slots) {
        if (slot.sessionId == sessionId) {
            return &slot;
        }
    }

    for (auto& slot : g_transientBridge->slots) {
        if (slot.sessionId == 0xFFFFFFFF) {
            return &slot;
        }
    }

    // Fixed table: evict the oldest slot if every entry is occupied.
    auto* oldest = &g_transientBridge->slots[0];
    for (auto& slot : g_transientBridge->slots) {
        if (slot.updateTick < oldest->updateTick) {
            oldest = &slot;
        }
    }
    return oldest;
}

bool PublishNativeTransientStatus(
    const std::wstring& textValue,
    const wchar_t* reason) {

    if (!g_showNativeTransientStatus.load(
            std::memory_order_acquire) ||
        !g_transientBridge ||
        g_unloading.load(
            std::memory_order_acquire)) {
        return false;
    }

    DWORD sessionId = 0xFFFFFFFF;
    if (!ProcessIdToSessionId(
            GetCurrentProcessId(),
            &sessionId) ||
        sessionId == 0xFFFFFFFF) {
        return false;
    }

    std::wstring user =
        QueryWtsString(
            sessionId,
            WTSUserName);
    const std::wstring domain =
        QueryWtsString(
            sessionId,
            WTSDomainName);
    if (!user.empty() && !domain.empty()) {
        user = domain + L"\\" + user;
    }

    const std::wstring client =
        QueryWtsString(
            sessionId,
            WTSClientName);
    const USHORT protocol =
        QueryWtsProtocolType(
            sessionId);
    WTS_CONNECTSTATE_CLASS state = WTSDown;
    QueryWtsConnectStateForBridge(
        sessionId,
        &state);

    ULONGLONG serial = 0;
    bool changed = false;

    if (!LockTransientBridge()) {
        ProbeLog(
            L"TRANSIENT_BRIDGE publish mutex timeout session=%lu",
            sessionId);
        return false;
    }

    auto* slot =
        FindTransientBridgeSlotLocked(
            sessionId);

    if (slot) {
        const bool sameSession =
            slot->sessionId == sessionId;

        std::wstring rememberedUser =
            sameSession
                ? std::wstring(slot->lastUser)
                : std::wstring();
        std::wstring rememberedClient =
            sameSession
                ? std::wstring(slot->lastClient)
                : std::wstring();
        USHORT rememberedProtocol =
            sameSession
                ? slot->protocolType
                : 0xFFFF;

        const std::wstring effectiveUser =
            !user.empty()
                ? user
                : rememberedUser;
        const std::wstring effectiveClient =
            !client.empty()
                ? client
                : rememberedClient;
        // v0.3 runtime testing showed WTSClientProtocolType can briefly
        // collapse from RDP (2) to console (0) while an RDP LogonUI is being
        // torn down. Preserve the already-observed remote transport in that
        // narrow case instead of misclassifying the transient status as local.
        USHORT effectiveProtocol = rememberedProtocol;
        if (protocol != 0xFFFF) {
            if (protocol != 0 ||
                rememberedProtocol == 0xFFFF ||
                rememberedProtocol == 0) {
                effectiveProtocol = protocol;
            }
        }

        const bool sameWriter =
            sameSession &&
            slot->writerPid == GetCurrentProcessId() &&
            slot->writerCreateTime ==
                g_transientBridgeProcessCreateTime;

        // A LogonUI replacement for one session can overlap the old process.
        // Once the newer process has published, don't let the older process
        // reclaim the slot with a late callback. If the newer writer has died,
        // the surviving older process may take ownership again.
        const bool newerLiveWriterOwnsSlot =
            sameSession &&
            !sameWriter &&
            slot->writerCreateTime != 0 &&
            g_transientBridgeProcessCreateTime != 0 &&
            slot->writerCreateTime >
                g_transientBridgeProcessCreateTime &&
            IsSameProcessInstanceAlive(
                slot->writerPid,
                slot->writerCreateTime);

        if (newerLiveWriterOwnsSlot) {
            UnlockTransientBridge();
            return false;
        }

        const bool identical =
            sameWriter &&
            std::wstring(slot->text) == textValue &&
            std::wstring(slot->lastUser) == effectiveUser &&
            std::wstring(slot->lastClient) == effectiveClient &&
            slot->protocolType == effectiveProtocol &&
            slot->wtsState == static_cast<DWORD>(state);

        if (!identical) {
            if (!sameSession) {
                ZeroMemory(slot, sizeof(*slot));
            }

            slot->sessionId = sessionId;
            slot->writerPid = GetCurrentProcessId();
            slot->writerCreateTime =
                g_transientBridgeProcessCreateTime;
            slot->updateTick = GetTickCount64();
            slot->wtsState = static_cast<DWORD>(state);
            slot->protocolType = effectiveProtocol;
            CopyTransientFixedString(
                slot->lastUser,
                effectiveUser);
            CopyTransientFixedString(
                slot->lastClient,
                effectiveClient);
            CopyTransientFixedString(
                slot->text,
                textValue);

            serial = ++g_transientBridge->serial;
            slot->serial = serial;
            changed = true;
        }
    }

    UnlockTransientBridge();

    if (!changed) {
        return false;
    }

    // Record raw native source timing separately from the console XAML write.
    // Use atomics only, never wait for a frame from the native callback.
    g_renderWriterTick.store(
        GetTickCount64(), std::memory_order_relaxed);
    g_renderWriterSerial.store(
        serial, std::memory_order_release);

    // Signal the console before diagnostics. Shared-file logging opens/writes
    // a file per line and used to sit directly in the critical path between a
    // native TextProperty change and the console wakeup.
    SignalActiveConsoleTransientReaders(serial);

    ProbeLog(
        L"TRANSIENT_BRIDGE publish reason=%s serial=%llu session=%lu writerPid=%lu user=\"%s\" client=\"%s\" protocol=%u state=%s text=\"%s\"",
        reason ? reason : L"<unknown>",
        static_cast<unsigned long long>(serial),
        sessionId,
        GetCurrentProcessId(),
        user.empty() ? L"<remembered-or-empty>" : user.c_str(),
        client.empty() ? L"<remembered-or-empty>" : client.c_str(),
        static_cast<unsigned>(protocol),
        WtsStateName(state),
        textValue.c_str());
    return true;
}

// A disconnected account may briefly spin up another LogonUI that displays
// its own generic "Please wait"/"Locking" screen. This is not an incoming
// sign-in attempt and must not overwrite the console user row's Disconnected
// state. Keep sign-out/service-specific native statuses intact.
bool IsDisconnectedGenericNativeTransientStatusSlot(
    const TransientBridgeSlot& slot) {
    return slot.wtsState == static_cast<DWORD>(WTSDisconnected) &&
           slot.lastUser[0] != L'\0' &&
           (wcscmp(slot.text, L"Please wait") == 0 ||
            wcscmp(slot.text, L"Locking") == 0);
}

void MergeNativeTransientStatusIntoSessions(
    std::vector<WtsSessionSnapshot>* sessions) {

    if (!sessions ||
        !g_showNativeTransientStatus.load(
            std::memory_order_acquire) ||
        !g_transientBridge) {
        return;
    }

    std::vector<TransientBridgeSlot> slots(
        kTransientBridgeSlotCount);

    if (!LockTransientBridge()) {
        return;
    }

    memcpy(
        slots.data(),
        g_transientBridge->slots,
        sizeof(g_transientBridge->slots));

    UnlockTransientBridge();

    auto applyRememberedIdentity = [](
            WtsSessionSnapshot* session,
            const TransientBridgeSlot& slot) {
        if (!session) {
            return;
        }

        if (session->userName.empty() &&
            slot.lastUser[0] != L'\0') {
            const std::wstring rememberedUser =
                slot.lastUser;
            const size_t separator =
                rememberedUser.find(L'\\');
            if (separator != std::wstring::npos) {
                if (session->domainName.empty()) {
                    session->domainName =
                        rememberedUser.substr(0, separator);
                }
                session->userName =
                    rememberedUser.substr(separator + 1);
            } else {
                session->userName =
                    rememberedUser;
            }
        }

        if (session->clientName.empty() &&
            slot.lastClient[0] != L'\0') {
            session->clientName =
                slot.lastClient;
        }

        if (slot.protocolType != 0xFFFF &&
            (session->protocolType == 0xFFFF ||
             (session->protocolType == 0 &&
              slot.protocolType == 2))) {
            session->protocolType =
                slot.protocolType;
        }
    };

    for (const auto& slot : slots) {
        if (slot.sessionId == 0xFFFFFFFF ||
            slot.text[0] == L'\0' ||
            slot.serial == 0 ||
            !IsSameProcessInstanceAlive(
                slot.writerPid,
                slot.writerCreateTime) ||
            IsDisconnectedGenericNativeTransientStatusSlot(slot)) {
            continue;
        }

        auto existing =
            std::find_if(
                sessions->begin(),
                sessions->end(),
                [&](const WtsSessionSnapshot& session) {
                    return session.sessionId ==
                           slot.sessionId;
                });

        if (existing != sessions->end()) {
            // The exact text belongs to the LogonUI process that owns the
            // native StatusMessage visual. Remembered user/client/protocol
            // metadata only fills values that WTS temporarily blanked or
            // downgraded while that exact live transient owns the session.
            applyRememberedIdentity(
                &*existing,
                slot);
            existing->nativeTransientStatus =
                slot.text;
            existing->nativeTransientWriterPid =
                slot.writerPid;
            existing->nativeTransientSerial =
                slot.serial;
            continue;
        }

        // During native sign-out, WTS can remove the session from
        // WTSEnumerateSessions before LogonUI has finished displaying
        // StatusMessage="Signing out". Keep a bridge-backed *shadow snapshot*
        // only while a live native writer still owns non-empty exact text and
        // we have a previously proven account identity for that session ID.
        // This does not synthesize a status or invent an account mapping.
        if (slot.lastUser[0] == L'\0') {
            continue;
        }

        WtsSessionSnapshot shadow{};
        shadow.sessionId = slot.sessionId;

        const DWORD rawState = slot.wtsState;
        shadow.state =
            rawState <= static_cast<DWORD>(WTSInit)
                ? static_cast<WTS_CONNECTSTATE_CLASS>(rawState)
                : WTSDown;
        shadow.protocolType = slot.protocolType;
        shadow.lockState =
            ReadWtsLockEventState(
                slot.sessionId);

        applyRememberedIdentity(
            &shadow,
            slot);

        shadow.nativeTransientStatus =
            slot.text;
        shadow.nativeTransientWriterPid =
            slot.writerPid;
        shadow.nativeTransientSerial =
            slot.serial;

        ProbeLog(
            L"TRANSIENT_BRIDGE shadow session=%lu writerPid=%lu serial=%llu qualified=\"%s\" protocol=%u state=%s text=\"%s\"",
            shadow.sessionId,
            shadow.nativeTransientWriterPid,
            static_cast<unsigned long long>(
                shadow.nativeTransientSerial),
            (shadow.domainName.empty()
                 ? shadow.userName
                 : shadow.domainName + L"\\" + shadow.userName).c_str(),
            static_cast<unsigned>(shadow.protocolType),
            WtsStateName(shadow.state),
            shadow.nativeTransientStatus.c_str());

        sessions->push_back(
            std::move(shadow));
    }
}

void ClearTransientBridgeSession(
    DWORD sessionId,
    const wchar_t* reason) {

    if (!g_transientBridge ||
        sessionId == 0xFFFFFFFF) {
        return;
    }

    bool changed = false;
    ULONGLONG serial = 0;

    if (!LockTransientBridge()) {
        return;
    }

    for (auto& slot : g_transientBridge->slots) {
        if (slot.sessionId != sessionId) {
            continue;
        }

        ZeroMemory(&slot, sizeof(slot));
        slot.sessionId = 0xFFFFFFFF;
        slot.protocolType = 0xFFFF;
        slot.wtsState = static_cast<DWORD>(WTSDown);
        serial = ++g_transientBridge->serial;
        changed = true;
        break;
    }

    UnlockTransientBridge();

    if (changed) {
        ProbeLog(
            L"TRANSIENT_BRIDGE retired session=%lu reason=%s serial=%llu",
            sessionId,
            reason ? reason : L"<unknown>",
            static_cast<unsigned long long>(serial));
        SignalActiveConsoleTransientReaders(serial);
    }
}

void ClearOwnedTransientBridgeSlots(
    const wchar_t* reason) {

    if (!g_transientBridge) {
        return;
    }

    bool changed = false;
    ULONGLONG serial = 0;

    if (!LockTransientBridge()) {
        return;
    }

    for (auto& slot : g_transientBridge->slots) {
        if (slot.writerPid == GetCurrentProcessId() &&
            slot.writerCreateTime ==
                g_transientBridgeProcessCreateTime) {

            slot.text[0] = L'\0';
            slot.writerPid = 0;
            slot.writerCreateTime = 0;
            slot.updateTick = GetTickCount64();
            serial = ++g_transientBridge->serial;
            slot.serial = serial;
            changed = true;
        }
    }

    UnlockTransientBridge();

    if (changed) {
        ProbeLog(
            L"TRANSIENT_BRIDGE cleared owned slot(s) reason=%s serial=%llu",
            reason ? reason : L"<unknown>",
            static_cast<unsigned long long>(serial));
        SignalActiveConsoleTransientReaders(serial);
    }
}

void ShutdownTransientBridge() {
    if (g_transientBridgeReaderEvent) {
        CloseHandle(g_transientBridgeReaderEvent);
        g_transientBridgeReaderEvent = nullptr;
    }

    if (g_transientBridge) {
        UnmapViewOfFile(g_transientBridge);
        g_transientBridge = nullptr;
    }

    if (g_transientBridgeMapHandle) {
        CloseHandle(g_transientBridgeMapHandle);
        g_transientBridgeMapHandle = nullptr;
    }

    if (g_transientBridgeMutex) {
        CloseHandle(g_transientBridgeMutex);
        g_transientBridgeMutex = nullptr;
    }

    g_transientBridgeProcessCreateTime = 0;
}

std::vector<WtsSessionSnapshot> CaptureWtsSessions(bool includeNativeTransient = true) {
    std::vector<WtsSessionSnapshot> result;

    PWTS_SESSION_INFOW sessions = nullptr;
    DWORD count = 0;

    if (!WTSEnumerateSessionsW(
            WTS_CURRENT_SERVER_HANDLE,
            0,
            1,
            &sessions,
            &count) ||
        !sessions) {

        ProbeLog(
            L"WTS WTSEnumerateSessionsW failed gle=%lu",
            GetLastError());
        return result;
    }

    result.reserve(count);

    for (DWORD i = 0; i < count; ++i) {
        WtsSessionSnapshot item{};
        item.sessionId = sessions[i].SessionId;
        item.state = sessions[i].State;

        if (sessions[i].pWinStationName) {
            item.stationName =
                sessions[i].pWinStationName;
        }

        item.userName =
            QueryWtsString(
                item.sessionId,
                WTSUserName);
        item.domainName =
            QueryWtsString(
                item.sessionId,
                WTSDomainName);
        item.clientName =
            QueryWtsString(
                item.sessionId,
                WTSClientName);
        item.protocolType =
            QueryWtsProtocolType(
                item.sessionId);
        const WtsLockState queriedLockState =
            QueryWtsLockState(
                item.sessionId);
        const WtsLockState eventLockState =
            ReadWtsLockEventState(
                item.sessionId);

        // WTSSessionInfoEx can lag (and on some RDP paths remain stale) after
        // WTS_SESSION_LOCK/UNLOCK. The notification itself is authoritative
        // until the matching opposite event arrives; fall back to the queried
        // flag when this LogonUI process has not observed a lock event.
        item.lockState =
            eventLockState != WtsLockState::Unknown
                ? eventLockState
                : queriedLockState;

        result.push_back(std::move(item));
    }

    WTSFreeMemory(sessions);

    if (includeNativeTransient) {
        MergeNativeTransientStatusIntoSessions(&result);
    }

    return result;
}

// Read-only on normal refreshes: latch only after the first real account
// presenter is found. A WTS notification can still repair the XAML tree, but
// does not change the legacy status snapshot unless explicitly opted in.
std::vector<WtsSessionSnapshot> CaptureWtsSessionsForRows() {
    if (g_liveSessionRowUpdates.load(std::memory_order_acquire)) {
        return CaptureWtsSessions();
    }
    AcquireSRWLockShared(&g_rowSessionSnapshotLock);
    if (g_rowSessionSnapshotReady) {
        auto cached = g_rowSessionSnapshot;
        ReleaseSRWLockShared(&g_rowSessionSnapshotLock);
        return cached;
    }
    ReleaseSRWLockShared(&g_rowSessionSnapshotLock);
    return CaptureWtsSessions(false);
}

void CommitFirstRowSessionSnapshot(
    const std::vector<WtsSessionSnapshot>& sessions,
    unsigned realizedUserRows,
    DWORD capturedConsoleSession) {
    if (realizedUserRows == 0 ||
        g_liveSessionRowUpdates.load(std::memory_order_acquire)) {
        return;
    }
    bool committed = false;
    AcquireSRWLockExclusive(&g_rowSessionSnapshotLock);
    if (!g_rowSessionSnapshotReady) {
        g_rowSessionSnapshot = sessions;
        g_rowSnapshotActiveConsoleSession = capturedConsoleSession;
        g_rowSessionSnapshotReady = true;
        committed = true;
    }
    ReleaseSRWLockExclusive(&g_rowSessionSnapshotLock);
    if (committed) {
        ProbeLog(
            L"WTS_ROW_SNAPSHOT captured rows=%u sessions=%u consoleSession=%lu updates=on-LogonUI-creation",
            realizedUserRows,
            static_cast<unsigned>(sessions.size()),
            capturedConsoleSession);
    }
}

bool EqualsInsensitive(
    const std::wstring& a,
    const std::wstring& b) {

    return _wcsicmp(
               a.c_str(),
               b.c_str()) == 0;
}

std::wstring WtsQualifiedName(
    const WtsSessionSnapshot& session) {

    if (session.userName.empty()) {
        return {};
    }

    if (session.domainName.empty()) {
        return session.userName;
    }

    return session.domainName +
           L"\\" +
           session.userName;
}

const WtsSessionSnapshot* FindWtsSessionForQualifiedName(
    const std::wstring& qualifiedName,
    const std::vector<WtsSessionSnapshot>& sessions) {

    if (qualifiedName.empty()) {
        return nullptr;
    }

    for (const auto& session : sessions) {
        const auto candidate =
            WtsQualifiedName(session);

        if (!candidate.empty() &&
            EqualsInsensitive(
                candidate,
                qualifiedName)) {
            return &session;
        }
    }

    return nullptr;
}

void LogWtsSnapshot(
    const std::vector<WtsSessionSnapshot>& sessions,
    unsigned scan) {

    ProbeLog(
        L"WTS snapshot scan=%u sessions=%u",
        scan,
        static_cast<unsigned>(
            sessions.size()));

    for (const auto& session : sessions) {
        ProbeLog(
            L"WTS_SESSION scan=%u id=%lu state=%s station=\"%s\" "
            L"domain=\"%s\" user=\"%s\" qualified=\"%s\" "
            L"protocol=%u client=\"%s\" lockState=%s",
            scan,
            session.sessionId,
            WtsStateName(session.state),
            session.stationName.c_str(),
            session.domainName.c_str(),
            session.userName.c_str(),
            WtsQualifiedName(session).c_str(),
            static_cast<unsigned>(
                session.protocolType),
            session.clientName.c_str(),
            WtsLockStateName(session.lockState));
    }
}

void ProbeUserList(void* requestVm) {
    if (!requestVm || !g_requestGetUserAndV1List) {
        return;
    }

    void* listObject = nullptr;
    HRESULT listHr =
        g_requestGetUserAndV1List(
            requestVm,
            &listObject);

    if (FAILED(listHr) || !listObject) {
        ProbeLog(
            L"USERLIST getter hr=0x%08X object=%p",
            static_cast<unsigned>(listHr),
            listObject);

        if (listObject) {
            reinterpret_cast<IUnknown*>(listObject)->Release();
        }
        return;
    }

    try {
        winrt::Windows::Foundation::IInspectable listInspectable{nullptr};
        winrt::copy_from_abi(
            listInspectable,
            listObject);

        reinterpret_cast<IUnknown*>(listObject)->Release();
        listObject = nullptr;

        auto vector =
            listInspectable.try_as<
                winrt::Windows::UI::Xaml::Interop::IBindableVector>();

        if (!vector) {
            ProbeLog(
                L"USERLIST object class=\"%s\" does not QI as IBindableVector",
                SafeClassName(listInspectable).c_str());
            return;
        }

        ++g_userListScanCount;

        const uint32_t size = vector.Size();
        ProbeLog(
            L"USERLIST scan=%u size=%u",
            g_userListScanCount,
            size);

        if (size != 0) {
            g_userListReady.store(
                true,
                std::memory_order_release);
        }

        const auto wtsSessions =
            size != 0
                ? CaptureWtsSessions()
                : std::vector<WtsSessionSnapshot>{};

        if (size != 0 &&
            (g_lastUserListStates.empty() ||
             g_userListScanCount == 1)) {
            LogWtsSnapshot(
                wtsSessions,
                g_userListScanCount);
        }

        std::vector<UserListState> currentStates;
        currentStates.reserve(size);

        for (uint32_t i = 0; i < size; ++i) {
            auto item = vector.GetAt(i);

            if (!item) {
                ProbeLog(
                    L"USERLIST_ITEM index=%u <null>",
                    i);
                continue;
            }

            void* userVm = nullptr;
            auto raw =
                reinterpret_cast<IUnknown*>(
                    winrt::get_abi(item));

            HRESULT qiHr =
                raw->QueryInterface(
                    g_userViewModelPublicIid,
                    &userVm);

            const std::wstring itemClass =
                SafeClassName(item);

            if (FAILED(qiHr) || !userVm) {
                ProbeLog(
                    L"USERLIST_ITEM index=%u class=\"%s\" userVM_QI=0x%08X",
                    i,
                    itemClass.c_str(),
                    static_cast<unsigned>(qiHr));
                continue;
            }

            HRESULT displayNameHr = E_FAIL;
            HRESULT qualifiedNameHr = E_FAIL;
            HRESULT loggedInHr = E_FAIL;
            HRESULT visibleHr = E_FAIL;
            HRESULT statusHr = E_FAIL;

            UserListState state{};
            state.displayName =
                ReadStringProperty(
                    g_getDisplayName,
                    userVm,
                    &displayNameHr);
            state.qualifiedName =
                ReadStringProperty(
                    g_getQualifiedName,
                    userVm,
                    &qualifiedNameHr);
            state.loggedIn =
                ReadBoolProperty(
                    g_getIsLoggedIn,
                    userVm,
                    &loggedInHr);
            state.visible =
                ReadBoolProperty(
                    g_getIsLogonStatusVisible,
                    userVm,
                    &visibleHr);
            state.status =
                ReadStringProperty(
                    g_getLogonStatus,
                    userVm,
                    &statusHr);

            bool changed = true;

            if (i < g_lastUserListStates.size()) {
                const auto& previous =
                    g_lastUserListStates[i];

                changed =
                    previous.displayName !=
                        state.displayName ||
                    previous.qualifiedName !=
                        state.qualifiedName ||
                    previous.loggedIn !=
                        state.loggedIn ||
                    previous.visible !=
                        state.visible ||
                    previous.status !=
                        state.status;
            }

            if (changed) {
                ProbeLog(
                    L"USERLIST_USER scan=%u index=%u class=\"%s\" iface=%p "
                    L"DisplayName=\"%s\" hr=0x%08X "
                    L"QualifiedName=\"%s\" hr=0x%08X "
                    L"IsLoggedIn=%d hr=0x%08X "
                    L"IsLogonStatusVisible=%d hr=0x%08X "
                    L"LogonStatus=\"%s\" hr=0x%08X",
                    g_userListScanCount,
                    i,
                    itemClass.c_str(),
                    userVm,
                    state.displayName.c_str(),
                    static_cast<unsigned>(
                        displayNameHr),
                    state.qualifiedName.c_str(),
                    static_cast<unsigned>(
                        qualifiedNameHr),
                    state.loggedIn,
                    static_cast<unsigned>(
                        loggedInHr),
                    state.visible,
                    static_cast<unsigned>(
                        visibleHr),
                    state.status.c_str(),
                    static_cast<unsigned>(
                        statusHr));

                const auto* session =
                    FindWtsSessionForQualifiedName(
                        state.qualifiedName,
                        wtsSessions);

                if (session) {
                    const bool remote =
                        session->protocolType != 0 &&
                        session->protocolType !=
                            0xFFFF &&
                        !session->clientName.empty();

                    std::wstring reconstructed;

                    if (remote) {
                        reconstructed =
                            L"Signed in remotely from " +
                            session->clientName;
                    } else if (state.loggedIn == 1) {
                        reconstructed =
                            L"<not remote; old AuthUI would "
                            L"choose Signed in/Locked from "
                            L"its logon state>";
                    }

                    ProbeLog(
                        L"WIN8_RECON scan=%u index=%u DisplayName=\"%s\" "
                        L"QualifiedName=\"%s\" matched=1 session=%lu "
                        L"state=%s station=\"%s\" protocol=%u client=\"%s\" "
                        L"remote=%u reconstructed=\"%s\"",
                        g_userListScanCount,
                        i,
                        state.displayName.c_str(),
                        state.qualifiedName.c_str(),
                        session->sessionId,
                        WtsStateName(session->state),
                        session->stationName.c_str(),
                        static_cast<unsigned>(
                            session->protocolType),
                        session->clientName.c_str(),
                        remote ? 1u : 0u,
                        reconstructed.c_str());
                } else {
                    ProbeLog(
                        L"WIN8_RECON scan=%u index=%u DisplayName=\"%s\" "
                        L"QualifiedName=\"%s\" matched=0",
                        g_userListScanCount,
                        i,
                        state.displayName.c_str(),
                        state.qualifiedName.c_str());
                }
            }

            currentStates.push_back(std::move(state));
            reinterpret_cast<IUnknown*>(userVm)->Release();
        }

        g_lastUserListStates = std::move(currentStates);

    } catch (const winrt::hresult_error& e) {
        ProbeLog(
            L"USERLIST WinRT exception hr=0x%08X",
            static_cast<unsigned>(e.code().value));
    } catch (...) {
        ProbeLog(
            L"USERLIST non-WinRT exception");
    }

    if (listObject) {
        reinterpret_cast<IUnknown*>(listObject)->Release();
    }
}


std::wstring ElementName(
    const FrameworkElement& element) {

    if (!element) {
        return {};
    }

    try {
        auto value = element.Name();
        return std::wstring(
            value.c_str(),
            value.size());
    } catch (...) {
        return L"<name-error>";
    }
}

std::wstring DataContextClass(
    const FrameworkElement& element) {

    if (!element) {
        return L"<null>";
    }

    try {
        auto dc = element.DataContext();
        return dc
            ? SafeClassName(dc)
            : L"<null>";
    } catch (...) {
        return L"<dc-error>";
    }
}

std::wstring TextBlockText(
    const FrameworkElement& element) {

    try {
        auto tb =
            element.try_as<
                winrt::Windows::UI::Xaml::Controls::TextBlock>();

        if (!tb) {
            return {};
        }

        auto value = tb.Text();
        return std::wstring(
            value.c_str(),
            value.size());
    } catch (...) {
        return L"<text-error>";
    }
}

void DumpLayoutSubtree(
    const FrameworkElement& element,
    int depth,
    int maxDepth,
    int& nodeBudget) {

    if (!element ||
        depth > maxDepth ||
        nodeBudget <= 0) {
        return;
    }

    --nodeBudget;

    try {
        const std::wstring className =
            SafeClassName(element);
        const std::wstring name =
            ElementName(element);
        const std::wstring dcClass =
            DataContextClass(element);
        const std::wstring text =
            TextBlockText(element);

        int visibility = -1;
        double actualWidth = -1.0;
        double actualHeight = -1.0;

        try {
            visibility =
                static_cast<int>(
                    element.Visibility());
            actualWidth =
                element.ActualWidth();
            actualHeight =
                element.ActualHeight();
        } catch (...) {
        }

        ProbeLog(
            L"LAYOUT_NODE depth=%d class=\"%s\" name=\"%s\" "
            L"dc=\"%s\" visibility=%d size=%.1fx%.1f text=\"%s\"",
            depth,
            className.c_str(),
            name.c_str(),
            dcClass.c_str(),
            visibility,
            actualWidth,
            actualHeight,
            text.c_str());

        const int count =
            Media::VisualTreeHelper::GetChildrenCount(
                element);

        for (int i = 0;
             i < count &&
             nodeBudget > 0;
             ++i) {

            auto child =
                Media::VisualTreeHelper::GetChild(
                    element,
                    i);

            auto childElement =
                child.try_as<FrameworkElement>();

            if (childElement) {
                DumpLayoutSubtree(
                    childElement,
                    depth + 1,
                    maxDepth,
                    nodeBudget);
            }
        }
    } catch (const winrt::hresult_error& e) {
        ProbeLog(
            L"LAYOUT_NODE exception depth=%d hr=0x%08X",
            depth,
            static_cast<unsigned>(
                e.code().value));
    } catch (...) {
        ProbeLog(
            L"LAYOUT_NODE exception depth=%d",
            depth);
    }
}

void FindAndDumpLayoutTargets(
    const FrameworkElement& element,
    int depth,
    int& scanBudget) {

    if (!element ||
        depth > 64 ||
        scanBudget <= 0) {
        return;
    }

    --scanBudget;

    try {
        const std::wstring className =
            SafeClassName(element);

        const bool target =
            className.find(
                L"LogonUX.SelectedUserTileUserControl") !=
                    std::wstring::npos ||
            className.find(
                L"LogonUX.UsersListUserControl") !=
                    std::wstring::npos ||
            className.find(
                L"LogonUX.UsersListView") !=
                    std::wstring::npos;

        if (target) {
            ProbeLog(
                L"LAYOUT_TARGET class=\"%s\" name=\"%s\" depth=%d",
                className.c_str(),
                ElementName(element).c_str(),
                depth);

            int nodeBudget = 350;
            DumpLayoutSubtree(
                element,
                0,
                12,
                nodeBudget);

            ProbeLog(
                L"LAYOUT_TARGET_END class=\"%s\" remainingBudget=%d",
                className.c_str(),
                nodeBudget);
        }

        const int count =
            Media::VisualTreeHelper::GetChildrenCount(
                element);

        for (int i = 0;
             i < count &&
             scanBudget > 0;
             ++i) {

            auto child =
                Media::VisualTreeHelper::GetChild(
                    element,
                    i);

            auto childElement =
                child.try_as<FrameworkElement>();

            if (childElement) {
                FindAndDumpLayoutTargets(
                    childElement,
                    depth + 1,
                    scanBudget);
            }
        }
    } catch (...) {
    }
}


struct RestoreStats {
    unsigned presentersSeen = 0;
    unsigned userRows = 0;
    unsigned completedRows = 0; // reached the validated row repair end
    unsigned remoteRows = 0;
    unsigned statusRows = 0;
    unsigned qualifiedNameRows = 0;
    unsigned userTileRows = 0;
    unsigned userTileMisses = 0;
    unsigned patchedRows = 0;
    unsigned signatureMisses = 0;
};

bool TryGetUserViewModelFromDataContext(
    const FrameworkElement& element,
    void** userVmOut) {

    if (!userVmOut) {
        return false;
    }

    *userVmOut = nullptr;

    if (!element ||
        !g_haveVmChainIids) {
        return false;
    }

    try {
        auto dataContext =
            element.DataContext();

        if (!dataContext) {
            return false;
        }

        auto raw =
            reinterpret_cast<IUnknown*>(
                winrt::get_abi(dataContext));

        if (!raw) {
            return false;
        }

        return SUCCEEDED(
                   raw->QueryInterface(
                       g_userViewModelPublicIid,
                       userVmOut)) &&
               *userVmOut;
    } catch (...) {
        return false;
    }
}

void CollectDirectTextBlocks(
    const FrameworkElement& element,
    std::vector<
        winrt::Windows::UI::Xaml::Controls::TextBlock>& out) {

    if (!element) {
        return;
    }

    try {
        const int count =
            Media::VisualTreeHelper::GetChildrenCount(
                element);

        for (int i = 0; i < count; ++i) {
            auto child =
                Media::VisualTreeHelper::GetChild(
                    element,
                    i);

            auto textBlock =
                child.try_as<
                    winrt::Windows::UI::Xaml::Controls::TextBlock>();

            if (textBlock) {
                out.push_back(
                    textBlock);
            }
        }
    } catch (...) {
    }
}


bool IsInjectedStatusStack(
    const winrt::Windows::UI::Xaml::Controls::StackPanel& stack) {

    if (!stack) {
        return false;
    }

    try {
        return ElementName(stack) ==
               kInjectedStatusStackName;
    } catch (...) {
        return false;
    }
}

bool IsNativeUserTextStash(
    const winrt::Windows::UI::Xaml::Controls::StackPanel& stack) {

    if (!stack) {
        return false;
    }

    try {
        return ElementName(stack) ==
               kNativeUserTextStashName;
    } catch (...) {
        return false;
    }
}

void CopyUserTextTypography(
    const winrt::Windows::UI::Xaml::Controls::TextBlock& source,
    const winrt::Windows::UI::Xaml::Controls::TextBlock& target) {

    if (!source || !target) {
        return;
    }

    try { target.FontFamily(source.FontFamily()); } catch (...) {}
    try { target.FontSize(source.FontSize()); } catch (...) {}
    try { target.FontWeight(source.FontWeight()); } catch (...) {}
    try { target.FontStyle(source.FontStyle()); } catch (...) {}
    try { target.FontStretch(source.FontStretch()); } catch (...) {}
    try { target.CharacterSpacing(source.CharacterSpacing()); } catch (...) {}
    try { target.HorizontalAlignment(source.HorizontalAlignment()); } catch (...) {}
    try { target.VerticalAlignment(source.VerticalAlignment()); } catch (...) {}
    try { target.TextAlignment(source.TextAlignment()); } catch (...) {}
    try { target.TextWrapping(source.TextWrapping()); } catch (...) {}
    try { target.TextTrimming(source.TextTrimming()); } catch (...) {}
    try { target.Opacity(source.Opacity()); } catch (...) {}

    try {
        auto foreground = source.Foreground();
        if (foreground) {
            target.Foreground(foreground);
        }
    } catch (...) {
    }

    target.Margin(
        Thickness{
            0.0, 0.0, 0.0, 0.0});

    try {
        Media::TranslateTransform identity;
        identity.X(0.0);
        identity.Y(0.0);
        target.RenderTransform(identity);
    } catch (...) {
    }
}

void CopyUserTileEllipseLayout(
    const Shapes::Ellipse& source,
    const Shapes::Ellipse& target) {

    if (!source || !target) {
        return;
    }

    try {
        double width = source.Width();
        if (!std::isfinite(width) || width <= 0.0) {
            width = source.ActualWidth();
        }
        if (std::isfinite(width) && width > 0.0) {
            target.Width(width);
        }
    } catch (...) {}

    try {
        double height = source.Height();
        if (!std::isfinite(height) || height <= 0.0) {
            height = source.ActualHeight();
        }
        if (std::isfinite(height) && height > 0.0) {
            target.Height(height);
        }
    } catch (...) {}

    try { target.Margin(source.Margin()); } catch (...) {}
    try { target.HorizontalAlignment(source.HorizontalAlignment()); } catch (...) {}
    try { target.VerticalAlignment(source.VerticalAlignment()); } catch (...) {}
    try { target.Opacity(source.Opacity()); } catch (...) {}
    try { target.Stroke(source.Stroke()); } catch (...) {}
    try { target.StrokeThickness(source.StrokeThickness()); } catch (...) {}

    try {
        Controls::Grid::SetRow(
            target,
            Controls::Grid::GetRow(source));
        Controls::Grid::SetColumn(
            target,
            Controls::Grid::GetColumn(source));
        Controls::Grid::SetRowSpan(
            target,
            Controls::Grid::GetRowSpan(source));
        Controls::Grid::SetColumnSpan(
            target,
            Controls::Grid::GetColumnSpan(source));
    } catch (...) {}

    target.IsHitTestVisible(false);
}

bool EnsureOwnedUserTileVisual(
    const FrameworkElement& textGridElement,
    const Media::Imaging::BitmapImage& bitmap,
    const std::wstring& displayName,
    bool* structureChanged = nullptr) {

    if (structureChanged) {
        *structureChanged = false;
    }

    if (!textGridElement || !bitmap) {
        return false;
    }

    try {
        auto parent =
            Media::VisualTreeHelper::GetParent(
                textGridElement);
        auto rowGrid =
            parent.try_as<Controls::Grid>();

        if (!rowGrid) {
            return false;
        }

        auto children = rowGrid.Children();
        Shapes::Ellipse nativeEllipse{nullptr};
        Shapes::Ellipse injectedEllipse{nullptr};
        uint32_t nativeIndex = 0;
        unsigned nativeCount = 0;

        for (uint32_t i = 0;
             i < children.Size();
             ++i) {

            auto ellipse =
                children.GetAt(i)
                    .try_as<Shapes::Ellipse>();

            if (!ellipse) {
                continue;
            }

            if (ElementName(ellipse) ==
                kInjectedUserTileEllipseName) {
                injectedEllipse = ellipse;
                continue;
            }

            nativeEllipse = ellipse;
            nativeIndex = i;
            ++nativeCount;
        }

        // The validated UsersList row template has one direct 48x48 native
        // Ellipse beside the text Grid. Fail closed if Windows changes that
        // shape rather than guessing which visual is the account picture.
        if (!nativeEllipse || nativeCount != 1) {
            return false;
        }

        if (!injectedEllipse) {
            injectedEllipse = Shapes::Ellipse();
            injectedEllipse.Name(
                kInjectedUserTileEllipseName);
            CopyUserTileEllipseLayout(
                nativeEllipse,
                injectedEllipse);

            // Insert immediately after the native ellipse. The Microsoft
            // control and binding remain completely untouched underneath; the
            // mod-owned overlay can therefore be removed cleanly on hot-disable.
            children.InsertAt(
                nativeIndex + 1,
                injectedEllipse);

            if (structureChanged) {
                *structureChanged = true;
            }

            ProbeLog(
                L"USER_TILE overlay created DisplayName=\"%s\" size=%.1fx%.1f",
                displayName.c_str(),
                injectedEllipse.Width(),
                injectedEllipse.Height());
        }

        Media::ImageBrush brush{nullptr};
        try {
            brush =
                injectedEllipse.Fill()
                    .try_as<Media::ImageBrush>();
        } catch (...) {
        }

        if (!brush) {
            brush = Media::ImageBrush();

            // Mirror Microsoft's crop/alignment policy where possible while
            // replacing only the source image.
            try {
                auto nativeBrush =
                    nativeEllipse.Fill()
                        .try_as<Media::ImageBrush>();
                if (nativeBrush) {
                    brush.Stretch(
                        nativeBrush.Stretch());
                    brush.AlignmentX(
                        nativeBrush.AlignmentX());
                    brush.AlignmentY(
                        nativeBrush.AlignmentY());
                } else {
                    brush.Stretch(
                        Media::Stretch::UniformToFill);
                }
            } catch (...) {
                brush.Stretch(
                    Media::Stretch::UniformToFill);
            }

            injectedEllipse.Fill(brush);
        }

        // Always refresh from this pass's exact UserViewModel snapshot. This is
        // intentionally not a binding to the recycled presenter: the overlay,
        // DisplayName, Status and QualifiedName now all advance atomically from
        // the same VM identity during the repair pass.
        brush.ImageSource(bitmap);
        injectedEllipse.Visibility(
            Visibility::Visible);

        return true;
    } catch (const winrt::hresult_error& e) {
        ProbeLog(
            L"USER_TILE overlay exception DisplayName=\"%s\" hr=0x%08X",
            displayName.c_str(),
            static_cast<unsigned>(
                e.code().value));
    } catch (...) {
        ProbeLog(
            L"USER_TILE overlay exception DisplayName=\"%s\"",
            displayName.c_str());
    }

    return false;
}

bool CollectUserTextBlocksFromContainer(
    const FrameworkElement& textGridElement,
    const std::wstring& displayName,
    const std::wstring& qualifiedName,
    std::vector<
        winrt::Windows::UI::Xaml::Controls::TextBlock>& out,
    bool* alreadyPatched) {

    out.clear();
    if (alreadyPatched) {
        *alreadyPatched = false;
    }

    auto grid =
        textGridElement.try_as<
            winrt::Windows::UI::Xaml::Controls::Grid>();

    if (!grid) {
        return false;
    }

    // First accept the Microsoft template: exactly three direct TextBlocks,
    // with DisplayName and QualifiedName identifiable. Do NOT require the
    // remaining secondary slot to be empty/collapsed. During lock-screen
    // recycling LogonUX can legitimately repopulate that native slot with
    // "Locked" before our repair runs. DisplayName + QualifiedName + exact
    // three-direct-TextBlock shape is the fail-closed signature here.
    CollectDirectTextBlocks(
        textGridElement,
        out);

    if (out.size() == 3) {
        bool haveDisplay = false;
        bool haveQualified = false;
        unsigned otherCount = 0;

        for (const auto& tb : out) {
            const std::wstring value =
                TextBlockText(tb);

            if (value == displayName) {
                haveDisplay = true;
            } else if (value == qualifiedName) {
                haveQualified = true;
            } else {
                ++otherCount;
            }
        }

        if (haveDisplay &&
            haveQualified &&
            otherCount == 1) {
            return true;
        }
    }

    out.clear();

    // Then accept only our own named StackPanel. This makes refresh
    // idempotent and prevents a second reparenting pass.
    try {
        auto children =
            grid.Children();

        for (uint32_t i = 0;
             i < children.Size();
             ++i) {

            auto stack =
                children.GetAt(i)
                    .try_as<
                        winrt::Windows::UI::Xaml::Controls::StackPanel>();

            if (!IsInjectedStatusStack(stack)) {
                continue;
            }

            auto stackChildren =
                stack.Children();

            for (uint32_t j = 0;
                 j < stackChildren.Size() && out.size() < 3;
                 ++j) {
                auto tb = stackChildren.GetAt(j)
                    .try_as<winrt::Windows::UI::Xaml::Controls::TextBlock>();
                if (!tb) continue;
                const auto name = ElementName(tb);
                if (name == kInjectedDisplayTextName ||
                    name == kInjectedStatusTextName ||
                    name == kInjectedQualifiedTextName) {
                    out.push_back(tb);
                }
            }

            if (out.size() != 3) {
                out.clear();
                return false;
            }

            // Identify our visible controls by Name, not child index: optional
            // custom layout rearranges these three controls inside our own
            // StackPanel. Native controls remain intact in the stash.
            if (alreadyPatched) {
                *alreadyPatched = true;
            }

            return true;
        }
    } catch (...) {
    }

    out.clear();
    return false;
}

bool IdentifyUserTextSlots(
    const std::vector<
        winrt::Windows::UI::Xaml::Controls::TextBlock>& textBlocks,
    const std::wstring& displayName,
    const std::wstring& qualifiedName,
    bool alreadyPatched,
    winrt::Windows::UI::Xaml::Controls::TextBlock* displaySlot,
    winrt::Windows::UI::Xaml::Controls::TextBlock* statusSlot,
    winrt::Windows::UI::Xaml::Controls::TextBlock* qualifiedSlot) {

    if (!displaySlot ||
        !statusSlot ||
        !qualifiedSlot ||
        textBlocks.size() != 3) {
        return false;
    }

    *displaySlot = nullptr;
    *statusSlot = nullptr;
    *qualifiedSlot = nullptr;

    if (alreadyPatched) {
        for (const auto& tb : textBlocks) {
            const auto name = ElementName(tb);
            if (name == kInjectedDisplayTextName) *displaySlot = tb;
            else if (name == kInjectedStatusTextName) *statusSlot = tb;
            else if (name == kInjectedQualifiedTextName) *qualifiedSlot = tb;
        }
        return *displaySlot && *statusSlot && *qualifiedSlot;
    }

    for (const auto& tb : textBlocks) {
        const std::wstring value =
            TextBlockText(tb);

        if (value == displayName) {
            *displaySlot = tb;
            continue;
        }

        if (value == qualifiedName) {
            *qualifiedSlot = tb;
            continue;
        }

        if (!*statusSlot) {
            *statusSlot = tb;
        } else {
            return false;
        }
    }

    return *displaySlot &&
           *statusSlot &&
           *qualifiedSlot;
}

FrameworkElement FindExpectedUserTextGrid(
    const FrameworkElement& element,
    const std::wstring& displayName,
    const std::wstring& qualifiedName,
    int depth) {

    if (!element ||
        depth > 5) {
        return nullptr;
    }

    try {
        if (auto grid =
                element.try_as<
                    winrt::Windows::UI::Xaml::Controls::Grid>()) {

            std::vector<
                winrt::Windows::UI::Xaml::Controls::TextBlock>
                textBlocks;
            bool alreadyPatched = false;

            if (CollectUserTextBlocksFromContainer(
                    grid,
                    displayName,
                    qualifiedName,
                    textBlocks,
                    &alreadyPatched)) {
                return grid;
            }
        }

        const int count =
            Media::VisualTreeHelper::GetChildrenCount(
                element);

        for (int i = 0;
             i < count;
             ++i) {

            auto child =
                Media::VisualTreeHelper::GetChild(
                    element,
                    i)
                    .try_as<FrameworkElement>();

            if (!child) {
                continue;
            }

            auto found =
                FindExpectedUserTextGrid(
                    child,
                    displayName,
                    qualifiedName,
                    depth + 1);

            if (found) {
                return found;
            }
        }
    } catch (...) {
    }

    return nullptr;
}

unsigned EnsureStackedUserTextLayout(
    const FrameworkElement& textGridElement,
    const std::wstring& displayName,
    const std::wstring& qualifiedName,
    bool* structureChanged = nullptr) {

    if (structureChanged) {
        *structureChanged = false;
    }

    auto textGrid =
        textGridElement.try_as<
            winrt::Windows::UI::Xaml::Controls::Grid>();

    if (!textGrid) {
        return 0;
    }

    std::vector<
        winrt::Windows::UI::Xaml::Controls::TextBlock>
        textBlocks;
    bool alreadyPatched = false;

    if (!CollectUserTextBlocksFromContainer(
            textGrid,
            displayName,
            qualifiedName,
            textBlocks,
            &alreadyPatched)) {

        CollectDirectTextBlocks(
            textGrid,
            textBlocks);

        if (textBlocks.size() != 3) {
            ProbeLog(
                L"LAYOUT_STACKPANEL fail: template signature no longer matches");
            return 0;
        }

        alreadyPatched = false;
    }

    winrt::Windows::UI::Xaml::Controls::TextBlock
        displaySlot{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock
        statusSlot{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock
        qualifiedSlot{nullptr};

    if (!IdentifyUserTextSlots(
            textBlocks,
            displayName,
            qualifiedName,
            alreadyPatched,
            &displaySlot,
            &statusSlot,
            &qualifiedSlot)) {
        return 0;
    }

    winrt::Windows::UI::Xaml::Controls::StackPanel
        stack{nullptr};
    winrt::Windows::UI::Xaml::Controls::StackPanel
        nativeStash{nullptr};

    const wchar_t* layoutAction =
        alreadyPatched ? L"refresh" : L"created";

    if (!alreadyPatched) {
        auto children =
            textGrid.Children();

        if (children.Size() != 3) {
            ProbeLog(
                L"LAYOUT_STACKPANEL fail: native Grid child count=%u",
                children.Size());
            return 0;
        }

        // Own all three visible strings. ListViewItemPresenter containers are
        // recycled and their DataContext can move to another UserViewModel
        // before Microsoft's native DisplayName binding catches up. Mixing one
        // native visible line with two mod-owned lines can therefore cross-pair
        // identities. Keep all three Microsoft controls alive, hidden and
        // binding-owned, and render one coherent mod-owned core triplet instead; optional alpha4h diagnostic rows are appended after it.
        auto nativeDisplaySlot = displaySlot;
        auto nativeStatusSlot = statusSlot;
        auto nativeQualifiedSlot = qualifiedSlot;

        winrt::Windows::UI::Xaml::Controls::TextBlock
            injectedDisplay;
        injectedDisplay.Name(
            kInjectedDisplayTextName);
        CopyUserTextTypography(
            nativeDisplaySlot,
            injectedDisplay);
        injectedDisplay.Text(
            winrt::hstring(
                displayName));
        injectedDisplay.Visibility(
            Visibility::Visible);

        winrt::Windows::UI::Xaml::Controls::TextBlock
            injectedStatus;
        injectedStatus.Name(
            kInjectedStatusTextName);
        CopyUserTextTypography(
            nativeStatusSlot,
            injectedStatus);
        injectedStatus.Text(
            winrt::hstring());
        injectedStatus.Visibility(
            Visibility::Collapsed);

        winrt::Windows::UI::Xaml::Controls::TextBlock
            injectedQualified;
        injectedQualified.Name(
            kInjectedQualifiedTextName);
        CopyUserTextTypography(
            nativeQualifiedSlot,
            injectedQualified);
        injectedQualified.Text(
            winrt::hstring(
                qualifiedName));
        injectedQualified.Visibility(
            nativeQualifiedSlot.Visibility());

        stack =
            winrt::Windows::UI::Xaml::Controls::StackPanel();
        stack.Name(
            kInjectedStatusStackName);
        stack.Orientation(
            winrt::Windows::UI::Xaml::Controls::Orientation::Vertical);
        stack.HorizontalAlignment(
            HorizontalAlignment::Left);
        stack.VerticalAlignment(
            VerticalAlignment::Center);

        nativeStash =
            winrt::Windows::UI::Xaml::Controls::StackPanel();
        nativeStash.Name(
            kNativeUserTextStashName);
        nativeStash.Visibility(
            Visibility::Collapsed);
        nativeStash.IsHitTestVisible(
            false);

        children.Clear();

        stack.Children().Append(
            injectedDisplay);
        stack.Children().Append(
            injectedStatus);
        stack.Children().Append(
            injectedQualified);

        // Preserve all three original Microsoft controls with their native
        // bindings intact. Hot-disable restores these exact objects.
        nativeStash.Children().Append(
            nativeDisplaySlot);
        nativeStash.Children().Append(
            nativeStatusSlot);
        nativeStash.Children().Append(
            nativeQualifiedSlot);

        children.Append(
            stack);
        children.Append(
            nativeStash);

        displaySlot =
            injectedDisplay;
        statusSlot =
            injectedStatus;
        qualifiedSlot =
            injectedQualified;

        if (structureChanged) {
            *structureChanged = true;
        }
    } else {
        auto children =
            textGrid.Children();

        for (uint32_t i = 0;
             i < children.Size();
             ++i) {

            auto candidate =
                children.GetAt(i)
                    .try_as<
                        winrt::Windows::UI::Xaml::Controls::StackPanel>();

            if (IsInjectedStatusStack(candidate)) {
                stack = candidate;
            } else if (IsNativeUserTextStash(candidate)) {
                nativeStash = candidate;
            }
        }

        if (!stack) {
            return 0;
        }

        auto stackChildren =
            stack.Children();

        // Upgrade an alpha4b row in place if a hot reload ever leaves one
        // around: alpha4b stashed only native Status + QualifiedName and kept
        // Microsoft's DisplayName visible. Move that DisplayName into the stash
        // and replace it with our own text before any refresh writes to it.
        if (nativeStash &&
            stackChildren.Size() == 3 &&
            nativeStash.Children().Size() == 2) {

            auto nativeDisplaySlot =
                stackChildren.GetAt(0)
                    .try_as<
                        winrt::Windows::UI::Xaml::Controls::TextBlock>();

            if (!nativeDisplaySlot) {
                return 0;
            }

            winrt::Windows::UI::Xaml::Controls::TextBlock
                injectedDisplay;
            injectedDisplay.Name(
                kInjectedDisplayTextName);
            CopyUserTextTypography(
                nativeDisplaySlot,
                injectedDisplay);
            injectedDisplay.Text(
                winrt::hstring(
                    displayName));
            injectedDisplay.Visibility(
                Visibility::Visible);

            stackChildren.RemoveAt(0);
            nativeStash.Children().InsertAt(
                0,
                nativeDisplaySlot);
            stackChildren.InsertAt(
                0,
                injectedDisplay);

            displaySlot =
                injectedDisplay;
            layoutAction =
                L"upgraded-alpha4b";

            if (structureChanged) {
                *structureChanged = true;
            }

            ProbeLog(
                L"LAYOUT_STACKPANEL upgraded alpha4b row to all-owned visible text DisplayName=\"%s\"",
                displayName.c_str());
        } else if (!nativeStash &&
                   stackChildren.Size() == 3) {

            // Compatibility upgrade for the older pre-alpha4b shape where our
            // named StackPanel contained all three Microsoft TextBlocks and no
            // native stash existed.
            auto nativeDisplaySlot = displaySlot;
            auto nativeStatusSlot = statusSlot;
            auto nativeQualifiedSlot = qualifiedSlot;

            winrt::Windows::UI::Xaml::Controls::TextBlock
                injectedDisplay;
            injectedDisplay.Name(
                kInjectedDisplayTextName);
            CopyUserTextTypography(
                nativeDisplaySlot,
                injectedDisplay);
            injectedDisplay.Text(
                winrt::hstring(
                    displayName));
            injectedDisplay.Visibility(
                Visibility::Visible);

            winrt::Windows::UI::Xaml::Controls::TextBlock
                injectedStatus;
            injectedStatus.Name(
                kInjectedStatusTextName);
            CopyUserTextTypography(
                nativeStatusSlot,
                injectedStatus);
            injectedStatus.Text(
                winrt::hstring());
            injectedStatus.Visibility(
                Visibility::Collapsed);

            winrt::Windows::UI::Xaml::Controls::TextBlock
                injectedQualified;
            injectedQualified.Name(
                kInjectedQualifiedTextName);
            CopyUserTextTypography(
                nativeQualifiedSlot,
                injectedQualified);
            injectedQualified.Text(
                winrt::hstring(
                    qualifiedName));
            injectedQualified.Visibility(
                nativeQualifiedSlot.Visibility());

            nativeStash =
                winrt::Windows::UI::Xaml::Controls::StackPanel();
            nativeStash.Name(
                kNativeUserTextStashName);
            nativeStash.Visibility(
                Visibility::Collapsed);
            nativeStash.IsHitTestVisible(
                false);

            stackChildren.Clear();
            stackChildren.Append(
                injectedDisplay);
            stackChildren.Append(
                injectedStatus);
            stackChildren.Append(
                injectedQualified);

            nativeStash.Children().Append(
                nativeDisplaySlot);
            nativeStash.Children().Append(
                nativeStatusSlot);
            nativeStash.Children().Append(
                nativeQualifiedSlot);

            children.Append(
                nativeStash);

            displaySlot =
                injectedDisplay;
            statusSlot =
                injectedStatus;
            qualifiedSlot =
                injectedQualified;
            layoutAction =
                L"upgraded-legacy";

            if (structureChanged) {
                *structureChanged = true;
            }

            ProbeLog(
                L"LAYOUT_STACKPANEL upgraded legacy row to all-owned visible text DisplayName=\"%s\"",
                displayName.c_str());
        } else if (!nativeStash ||
                   nativeStash.Children().Size() != 3 ||
                   stackChildren.Size() < 3 ||
                   ElementName(displaySlot) !=
                       kInjectedDisplayTextName) {

            ProbeLog(
                L"LAYOUT_STACKPANEL fail: patched row shape isn't alpha4c-compatible");
            return 0;
        }
    }

    unsigned visibleLines = 0;

    for (const auto& tb :
         {displaySlot, statusSlot, qualifiedSlot}) {
        if (tb.Visibility() ==
            Visibility::Visible) {
            ++visibleLines;
        }
    }

    try {
        textGrid.UpdateLayout();
    } catch (...) {
    }

    if (g_enableLayoutDiagnostics.load(
            std::memory_order_relaxed)) {

        ProbeLog(
            L"LAYOUT_STACKPANEL %s visibleLines=%u "
            L"textGrid=%.1fx%.1f stack=%.1fx%.1f ownedAll=1",
            layoutAction,
            visibleLines,
            textGrid.ActualWidth(),
            textGrid.ActualHeight(),
            stack
                ? stack.ActualWidth()
                : 0.0,
            stack
                ? stack.ActualHeight()
                : 0.0);
    }

    return visibleLines;
}

// Only reorder our three named core TextBlocks; preserve diagnostic lines
// (which follow them) and the separate native Microsoft TextBlock stash.
// Identity is always recovered by element Name, never by visual index.
bool ArrangeOwnedUserRowCore(
    const Controls::StackPanel& stack, uint32_t templateSpec) {
    if (!stack) return false;
    try {
        const auto children = stack.Children();
        if (children.Size() < 3) return false;
        std::array<Controls::TextBlock, 4> core{nullptr, nullptr, nullptr, nullptr};
        for (uint32_t i = 0; i < children.Size(); ++i) {
            auto tb = children.GetAt(i).try_as<Controls::TextBlock>();
            if (!tb) continue;
            const auto name = ElementName(tb);
            if (name == kInjectedDisplayTextName) core[1] = tb;
            else if (name == kInjectedStatusTextName) core[2] = tb;
            else if (name == kInjectedQualifiedTextName) core[3] = tb;
        }
        if (!core[1] || !core[2] || !core[3]) return false;
        std::vector<winrt::Windows::UI::Xaml::UIElement> ordered;
        unsigned seen = 0;
        for (unsigned i = 0; i < 3; ++i) {
            const unsigned token = (templateSpec >> (3 * i)) & 7u;
            if (token >= 1 && token <= 3 && !(seen & (1u << token))) {
                ordered.push_back(core[token]);
                seen |= 1u << token;
            }
        }
        for (unsigned token : {1u, 2u, 3u}) {
            if (!(seen & (1u << token))) ordered.push_back(core[token]);
        }
        if (ordered.size() != 3) return false;
        bool identical = true;
        for (uint32_t i = 0; i < 3; ++i) {
            if (children.GetAt(i) != ordered[i]) {
                identical = false;
                break;
            }
        }
        if (identical) return false;
        for (uint32_t i = 3; i < children.Size(); ++i)
            ordered.push_back(children.GetAt(i));
        children.Clear();
        for (const auto& element : ordered) children.Append(element);
        return true;
    } catch (...) { return false; }
}

struct SessionDetailPresentation {
    std::wstring sessionLine;
    std::wstring connectionLine;
    std::wstring wtsStateLine;
    std::wstring stationLine;
    std::wstring protocolLine;
    std::wstring lockStateLine;
    std::wstring vmLoggedInLine;
    std::wstring vmStatusVisibleLine;
    std::wstring vmLogonStatusLine;

    bool Empty() const {
        return sessionLine.empty() &&
               connectionLine.empty() &&
               wtsStateLine.empty() &&
               stationLine.empty() &&
               protocolLine.empty() &&
               lockStateLine.empty() &&
               vmLoggedInLine.empty() &&
               vmStatusVisibleLine.empty() &&
               vmLogonStatusLine.empty();
    }
};

bool SynchronizeSessionDetailLine(
    const Controls::StackPanel& stack,
    const Controls::TextBlock& styleSource,
    const wchar_t* name,
    const std::wstring& text) {

    if (!stack || !name || !*name) {
        return false;
    }

    bool changed = false;

    try {
        auto children = stack.Children();
        Controls::TextBlock slot{nullptr};
        uint32_t slotIndex = 0;
        bool haveSlotIndex = false;

        for (uint32_t i = 0; i < children.Size(); ++i) {
            auto candidate =
                children.GetAt(i)
                    .try_as<Controls::TextBlock>();

            if (candidate &&
                ElementName(candidate) == name) {
                slot = candidate;
                slotIndex = i;
                haveSlotIndex = true;
                break;
            }
        }

        if (text.empty()) {
            if (slot && haveSlotIndex) {
                children.RemoveAt(slotIndex);
                changed = true;
            }
            return changed;
        }

        if (!slot) {
            slot = Controls::TextBlock();
            slot.Name(name);
            CopyUserTextTypography(
                styleSource,
                slot);
            slot.IsHitTestVisible(false);
            children.Append(slot);
            changed = true;
        }

        if (TextBlockText(slot) != text) {
            slot.Text(winrt::hstring(text));
            changed = true;
        }

        if (slot.Visibility() != Visibility::Visible) {
            slot.Visibility(Visibility::Visible);
            changed = true;
        }
    } catch (...) {
    }

    return changed;
}

bool SynchronizeSessionDetailLines(
    const Controls::StackPanel& stack,
    const Controls::TextBlock& styleSource,
    const SessionDetailPresentation& details) {

    bool changed = false;

    changed = SynchronizeSessionDetailLine(
                  stack, styleSource,
                  kInjectedSessionIdTextName,
                  details.sessionLine) || changed;
    changed = SynchronizeSessionDetailLine(
                  stack, styleSource,
                  kInjectedConnectionTextName,
                  details.connectionLine) || changed;
    changed = SynchronizeSessionDetailLine(
                  stack, styleSource,
                  kInjectedWtsStateTextName,
                  details.wtsStateLine) || changed;
    changed = SynchronizeSessionDetailLine(
                  stack, styleSource,
                  kInjectedStationTextName,
                  details.stationLine) || changed;
    changed = SynchronizeSessionDetailLine(
                  stack, styleSource,
                  kInjectedProtocolTextName,
                  details.protocolLine) || changed;
    changed = SynchronizeSessionDetailLine(
                  stack, styleSource,
                  kInjectedLockStateTextName,
                  details.lockStateLine) || changed;
    changed = SynchronizeSessionDetailLine(
                  stack, styleSource,
                  kInjectedVmLoggedInTextName,
                  details.vmLoggedInLine) || changed;
    changed = SynchronizeSessionDetailLine(
                  stack, styleSource,
                  kInjectedVmStatusVisibleTextName,
                  details.vmStatusVisibleLine) || changed;
    changed = SynchronizeSessionDetailLine(
                  stack, styleSource,
                  kInjectedVmLogonStatusTextName,
                  details.vmLogonStatusLine) || changed;

    return changed;
}


struct UnifiedRowValues {
    std::wstring lockedStatus;
    std::wstring remoteStatus;
    std::wstring nativeStatus;
    std::wstring customText;
};
std::wstring UnifiedRowFieldText(
    RowField field, const std::wstring& displayName,
    const std::wstring& qualifiedName, const std::wstring& status,
    const SessionDetailPresentation& details, const UnifiedRowValues& extra) {
    switch (field) {
        case RowField::Name: return displayName;
        case RowField::Qualified: return qualifiedName;
        case RowField::Status: return status;
        case RowField::LockedStatus: return extra.lockedStatus;
        case RowField::RemoteStatus: return extra.remoteStatus;
        case RowField::NativeStatus: return extra.nativeStatus;
        case RowField::Custom: return extra.customText;
        case RowField::SessionId: return details.sessionLine;
        case RowField::Connection: return details.connectionLine;
        case RowField::WtsState: return details.wtsStateLine;
        case RowField::Station: return details.stationLine;
        case RowField::Protocol: return details.protocolLine;
        case RowField::LockState: return details.lockStateLine;
        case RowField::VmLoggedIn: return details.vmLoggedInLine;
        case RowField::VmStatusVisible: return details.vmStatusVisibleLine;
        case RowField::VmLogonStatus: return details.vmLogonStatusLine;
    }
    return L"";
}

const wchar_t* UnifiedRowFieldOwnedName(RowField field) {
    switch (field) {
        case RowField::Name: return kInjectedDisplayTextName;
        case RowField::Status: return kInjectedStatusTextName;
        case RowField::Qualified: return kInjectedQualifiedTextName;
        case RowField::SessionId: return kInjectedSessionIdTextName;
        case RowField::Connection: return kInjectedConnectionTextName;
        case RowField::WtsState: return kInjectedWtsStateTextName;
        case RowField::Station: return kInjectedStationTextName;
        case RowField::Protocol: return kInjectedProtocolTextName;
        case RowField::LockState: return kInjectedLockStateTextName;
        case RowField::VmLoggedIn: return kInjectedVmLoggedInTextName;
        case RowField::VmStatusVisible: return kInjectedVmStatusVisibleTextName;
        case RowField::VmLogonStatus: return kInjectedVmLogonStatusTextName;
        default: return nullptr;
    }
}

// All children of this stack are mod-owned; the three native TextBlocks live
// separately in the untouched stash. This function never mutates ItemsStackPanel
// or any Microsoft-owned user-list child.
bool ApplyUnifiedUserRowLayout(
    const Controls::StackPanel& stack,
    const Controls::TextBlock& typography,
    const RowLayoutPlan& plan,
    const std::wstring& displayName,
    const std::wstring& qualifiedName,
    const std::wstring& status,
    const SessionDetailPresentation& details,
    const UnifiedRowValues& extra) {
    if (!stack || plan.rules.empty()) return false;
    try {
        auto children = stack.Children();
        std::vector<winrt::Windows::UI::Xaml::UIElement> all;
        std::map<std::wstring, Controls::TextBlock> byName;
        for (uint32_t i=0; i<children.Size(); ++i) {
            auto element=children.GetAt(i);
            all.push_back(element);
            auto tb=element.try_as<Controls::TextBlock>();
            if (tb) byName.emplace(ElementName(tb),tb);
            else return false; // fail closed on unexpected stack shape
        }
        if (!byName.count(kInjectedDisplayTextName) ||
            !byName.count(kInjectedStatusTextName) ||
            !byName.count(kInjectedQualifiedTextName)) return false;

        struct Item {
            Controls::TextBlock block{nullptr};
            std::wstring ruleName;
            ULONGLONG firstSeen=0;
            size_t configuredIndex=0;
        };
        std::vector<Item> visible;
        const ULONGLONG batch=GetTickCount64();
        std::map<std::wstring, ULONGLONG> firstSeen;
        const bool chronological=plan.order!=RowOrder::Manual;
        if (chronological) {
            std::lock_guard<std::mutex> guard(g_rowObservationMutex);
            const auto found=g_rowFirstSeen.find(qualifiedName);
            if (found!=g_rowFirstSeen.end()) firstSeen=found->second;
        }
        bool changed=false;
        for (size_t i=0;i<plan.rules.size();++i) {
            const auto& rule=plan.rules[i];
            std::wstring value;
            RowField selected=rule.candidates.front();
            for (RowField field:rule.candidates) {
                value=UnifiedRowFieldText(field,displayName,qualifiedName,
                                          status,details,extra);
                if (!value.empty()) {selected=field;break;}
            }
            if (value.empty()) continue;
            std::wstring elementName;
            if (rule.candidates.size()==1) {
                const wchar_t* known=UnifiedRowFieldOwnedName(selected);
                if (known) elementName=known;
            }
            if (elementName.empty()) elementName=
                std::wstring(kInjectedUnifiedChoicePrefix)+std::to_wstring(i);
            Controls::TextBlock slot{nullptr};
            const auto it=byName.find(elementName);
            if (it!=byName.end()) slot=it->second;
            else {
                slot=Controls::TextBlock();
                slot.Name(elementName);
                CopyUserTextTypography(typography,slot);
                slot.IsHitTestVisible(false);
                byName.emplace(elementName,slot);
                all.push_back(slot);
                changed=true;
            }
            if (TextBlockText(slot)!=value) {
                slot.Text(winrt::hstring(value)); changed=true;
            }
            if (slot.Visibility()!=Visibility::Visible) {
                slot.Visibility(Visibility::Visible); changed=true;
            }
            ULONGLONG seen=0;
            if (chronological) {
                const auto found=firstSeen.find(rule.expression);
                if (found!=firstSeen.end()) seen=found->second;
                else { seen=batch; firstSeen.emplace(rule.expression,seen); }
            }
            visible.push_back({slot,rule.expression,seen,i});
        }
        if (chronological) {
            std::lock_guard<std::mutex> guard(g_rowObservationMutex);
            auto it=g_rowFirstSeen.find(qualifiedName);
            if (it!=g_rowFirstSeen.end() || g_rowFirstSeen.size()<64) {
                auto& storage=g_rowFirstSeen[qualifiedName];
                for (const auto& p:firstSeen) storage.emplace(p.first,p.second);
            }
            std::stable_sort(visible.begin(),visible.end(),
                [&](const Item& a,const Item& b) {
                    return plan.order==RowOrder::FirstObserved
                        ? a.firstSeen < b.firstSeen : a.firstSeen > b.firstSeen;
                });
        }
        std::vector<winrt::Windows::UI::Xaml::UIElement> ordered;
        for (const auto& item:visible) ordered.push_back(item.block);
        for (const auto& item:all) {
            if (std::find(ordered.begin(),ordered.end(),item)!=ordered.end()) continue;
            if (auto tb=item.try_as<Controls::TextBlock>()) {
                if (tb.Visibility()!=Visibility::Collapsed) {
                    tb.Visibility(Visibility::Collapsed); changed=true;
                }
            }
            ordered.push_back(item);
        }
        bool orderChanged = ordered.size()!=children.Size();
        if (!orderChanged) for (uint32_t i=0;i<children.Size();++i) {
            if (children.GetAt(i)!=ordered[i]) {orderChanged=true;break;}
        }
        if (orderChanged) {
            // Only the mod-owned StackPanel is re-ordered. Do not tear down
            // its children merely because a status TextBlock changed text.
            children.Clear();
            for (const auto& element:ordered) children.Append(element);
        }
        return changed || orderChanged;
    } catch (...) { return false; }
}

unsigned CountVisibleInjectedUserTextLines(
    const FrameworkElement& textGridElement) {

    auto grid = textGridElement.try_as<Controls::Grid>();
    if (!grid) {
        return 0;
    }

    try {
        auto children = grid.Children();
        for (uint32_t i = 0; i < children.Size(); ++i) {
            auto stack =
                children.GetAt(i)
                    .try_as<Controls::StackPanel>();

            if (!IsInjectedStatusStack(stack)) {
                continue;
            }

            unsigned visible = 0;
            auto stackChildren = stack.Children();
            for (uint32_t j = 0; j < stackChildren.Size(); ++j) {
                auto tb =
                    stackChildren.GetAt(j)
                        .try_as<Controls::TextBlock>();
                if (tb && tb.Visibility() == Visibility::Visible) {
                    ++visible;
                }
            }
            return visible;
        }
    } catch (...) {
    }

    return 0;
}

struct OwnedUserVisualSyncResult {
    unsigned branches = 0;
    unsigned visibleBranches = 0;
    unsigned malformedBranches = 0;
    unsigned tileBranches = 0;
    bool changed = false;
};

void CollectOwnedUserTextGridsRecursive(
    const FrameworkElement& element,
    std::vector<FrameworkElement>& out,
    int depth) {

    if (!element || depth > 16) {
        return;
    }

    try {
        if (auto grid =
                element.try_as<Controls::Grid>()) {

            auto children = grid.Children();
            for (uint32_t i = 0;
                 i < children.Size();
                 ++i) {

                auto stack =
                    children.GetAt(i)
                        .try_as<Controls::StackPanel>();

                if (IsInjectedStatusStack(stack)) {
                    out.push_back(grid);
                    break;
                }
            }
        }

        const int count =
            Media::VisualTreeHelper::GetChildrenCount(
                element);

        for (int i = 0;
             i < count;
             ++i) {

            auto child =
                Media::VisualTreeHelper::GetChild(
                    element,
                    i)
                    .try_as<FrameworkElement>();

            if (child) {
                CollectOwnedUserTextGridsRecursive(
                    child,
                    out,
                    depth + 1);
            }
        }
    } catch (...) {
    }
}

OwnedUserVisualSyncResult SynchronizeOwnedUserRowVisuals(
    const FrameworkElement& presenter,
    const std::wstring& displayName,
    const std::wstring& qualifiedName,
    const std::wstring& desiredStatus,
    bool desiredQualifiedVisible,
    const SessionDetailPresentation& sessionDetails,
    const Media::Imaging::BitmapImage& smallUserTileBitmap,
    bool customLayout,
    const std::shared_ptr<const RowLayoutPlan>& rowPlan,
    const UnifiedRowValues& extra) {

    OwnedUserVisualSyncResult result{};

    std::vector<FrameworkElement> ownedTextGrids;
    CollectOwnedUserTextGridsRecursive(
        presenter,
        ownedTextGrids,
        0);

    for (const auto& gridElement : ownedTextGrids) {
        if (!gridElement) {
            continue;
        }

        // A recycled ListViewItemPresenter can temporarily retain more than one
        // realized row branch. Upgrade/synchronize every branch we own instead
        // of trusting the first matching Grid returned by the normal signature
        // search. Whichever branch LogonUX/composition is actually presenting
        // therefore receives the same UserViewModel snapshot.
        bool structureChanged = false;
        EnsureStackedUserTextLayout(
            gridElement,
            displayName,
            qualifiedName,
            &structureChanged);
        if (structureChanged) {
            result.changed = true;
        }

        auto grid =
            gridElement.try_as<Controls::Grid>();
        if (!grid) {
            ++result.malformedBranches;
            continue;
        }

        Controls::StackPanel stack{nullptr};
        auto gridChildren = grid.Children();
        for (uint32_t i = 0;
             i < gridChildren.Size();
             ++i) {

            auto candidate =
                gridChildren.GetAt(i)
                    .try_as<Controls::StackPanel>();
            if (IsInjectedStatusStack(candidate)) {
                stack = candidate;
                break;
            }
        }

        if (!stack) {
            ++result.malformedBranches;
            continue;
        }

        auto stackChildren = stack.Children();
        if (stackChildren.Size() < 3) {
            ++result.malformedBranches;
            continue;
        }

        Controls::TextBlock displaySlot{nullptr};
        Controls::TextBlock statusSlot{nullptr};
        Controls::TextBlock qualifiedSlot{nullptr};
        for (uint32_t j = 0; j < stackChildren.Size(); ++j) {
            auto candidate = stackChildren.GetAt(j).try_as<Controls::TextBlock>();
            if (!candidate) continue;
            const auto name = ElementName(candidate);
            if (name == kInjectedDisplayTextName) displaySlot = candidate;
            else if (name == kInjectedStatusTextName) statusSlot = candidate;
            else if (name == kInjectedQualifiedTextName) qualifiedSlot = candidate;
        }

        if (!displaySlot ||
            !statusSlot ||
            !qualifiedSlot ||
            ElementName(displaySlot) !=
                kInjectedDisplayTextName ||
            ElementName(statusSlot) !=
                kInjectedStatusTextName ||
            ElementName(qualifiedSlot) !=
                kInjectedQualifiedTextName) {

            ++result.malformedBranches;
            continue;
        }

        ++result.branches;

        const bool branchVisible =
            grid.Visibility() == Visibility::Visible &&
            stack.Visibility() == Visibility::Visible &&
            grid.ActualWidth() > 0.0 &&
            grid.ActualHeight() > 0.0 &&
            stack.ActualWidth() > 0.0 &&
            stack.ActualHeight() > 0.0;

        if (branchVisible) {
            ++result.visibleBranches;
        }

        if (TextBlockText(displaySlot) !=
            displayName) {

            displaySlot.Text(
                winrt::hstring(
                    displayName));
            result.changed = true;
        }

        if (displaySlot.Visibility() !=
            Visibility::Visible) {

            displaySlot.Visibility(
                Visibility::Visible);
            result.changed = true;
        }

        if (TextBlockText(qualifiedSlot) !=
            qualifiedName) {

            qualifiedSlot.Text(
                winrt::hstring(
                    qualifiedName));
            result.changed = true;
        }

        const auto qualifiedVisibility =
            desiredQualifiedVisible
                ? Visibility::Visible
                : Visibility::Collapsed;

        if (qualifiedSlot.Visibility() !=
            qualifiedVisibility) {

            qualifiedSlot.Visibility(
                qualifiedVisibility);
            result.changed = true;
        }

        if (!desiredStatus.empty()) {
            if (TextBlockText(statusSlot) !=
                desiredStatus) {

                statusSlot.Text(
                    winrt::hstring(
                        desiredStatus));
                result.changed = true;
            }

            if (statusSlot.Visibility() !=
                Visibility::Visible) {

                statusSlot.Visibility(
                    Visibility::Visible);
                result.changed = true;
            }
        } else {
            if (!TextBlockText(statusSlot).empty()) {
                statusSlot.Text(
                    winrt::hstring());
                result.changed = true;
            }

            if (statusSlot.Visibility() !=
                Visibility::Collapsed) {

                statusSlot.Visibility(
                    Visibility::Collapsed);
                result.changed = true;
            }
        }

        if (SynchronizeSessionDetailLines(
                stack,
                qualifiedSlot,
                sessionDetails)) {
            result.changed = true;
        }
        if (customLayout && rowPlan) {
            if (ApplyUnifiedUserRowLayout(
                    stack, qualifiedSlot, *rowPlan,
                    displayName, qualifiedName, desiredStatus,
                    sessionDetails, extra)) result.changed = true;
        } else if (ArrangeOwnedUserRowCore(stack, 1u | (2u << 3) | (3u << 6))) {
            result.changed = true;
        }

        if (smallUserTileBitmap) {
            bool tileStructureChanged = false;
            if (EnsureOwnedUserTileVisual(
                    gridElement,
                    smallUserTileBitmap,
                    displayName,
                    &tileStructureChanged)) {

                ++result.tileBranches;
                if (tileStructureChanged) {
                    result.changed = true;
                }
            }
        }

        try {
            grid.UpdateLayout();
        } catch (...) {
        }
    }

    if (result.branches > 1 ||
        result.malformedBranches != 0 ||
        result.changed ||
        g_enableLayoutDiagnostics.load(
            std::memory_order_relaxed)) {

        ProbeLog(
            L"OWNED_VISUAL_SYNC DisplayName=\"%s\" branches=%u "
            L"visibleBranches=%u malformed=%u tileBranches=%u changed=%u "
            L"status=\"%s\"",
            displayName.c_str(),
            result.branches,
            result.visibleBranches,
            result.malformedBranches,
            result.tileBranches,
            result.changed ? 1u : 0u,
            desiredStatus.empty()
                ? L"<hidden>"
                : desiredStatus.c_str());
    }

    return result;
}

void ApplyUserRowHeightMode(
    const FrameworkElement& presenter,
    bool loggedIn,
    unsigned visibleTextLines) {

    // Leave Windows sizing untouched until either compact rows or session
    // detail expansion has actually been enabled in this process.
    if (!g_compactRowsEverEnabled.load(
            std::memory_order_acquire) &&
        !g_sessionDetailsEverEnabled.load(
            std::memory_order_acquire) &&
        !g_customRowLayoutEnabled.load(std::memory_order_acquire)) {
        return;
    }

    try {
        auto parent =
            Media::VisualTreeHelper::GetParent(
                presenter);

        auto item =
            parent.try_as<
                winrt::Windows::UI::Xaml::Controls::ListViewItem>();

        if (!item) {
            return;
        }

        // The current template's text rows measure about 19 DIP each. Once
        // diagnostics add more than the normal three lines, explicitly grow
        // the ListViewItem so the StackPanel cannot overlap the next user row.
        if (visibleTextLines > 3) {
            const int expandedHeight =
                std::max(
                    60,
                    static_cast<int>(visibleTextLines * 19u + 3u));

            item.MinHeight(0.0);
            item.Height(
                static_cast<double>(
                    expandedHeight));
            return;
        }

        const bool compact =
            !loggedIn &&
            g_compactUnsignedRows.load(
                std::memory_order_relaxed);

        if (compact) {
            int height =
                g_compactUnsignedRowHeight.load(
                    std::memory_order_relaxed);

            height =
                ClampInt(
                    height,
                    48,
                    60);

            item.MinHeight(0.0);
            item.Height(
                static_cast<double>(
                    height));
        } else {
            // Return control to the current Windows style/template instead of
            // assuming that the native row height will always remain 60 DIP.
            item.ClearValue(
                FrameworkElement::HeightProperty());
            item.ClearValue(
                FrameworkElement::MinHeightProperty());
        }
    } catch (...) {
    }
}

bool TryRestoreRemoteStatusInPresenter(
    const FrameworkElement& presenter,
    const std::vector<WtsSessionSnapshot>& sessions,
    RestoreStats& stats) {

    ++stats.presentersSeen;

    if (!presenter ||
        presenter.Visibility() !=
            Visibility::Visible ||
        presenter.ActualWidth() <= 0.0 ||
        presenter.ActualHeight() <= 0.0) {
        return false;
    }

    void* userVm = nullptr;

    if (!TryGetUserViewModelFromDataContext(
            presenter,
            &userVm)) {
        return false;
    }

    ++stats.userRows;

    // Container virtualization can change this presenter's DataContext long
    // after the initial/lifecycle repair window has settled. Arm a direct
    // watcher so every future recycle schedules a coherent row repair.
    AttachPresenterDataContextWatcher(
        presenter);

    HRESULT displayNameHr = E_FAIL;
    HRESULT qualifiedNameHr = E_FAIL;
    HRESULT emailAddressHr = E_FAIL;
    HRESULT loggedInHr = E_FAIL;
    HRESULT statusVisibleHr = E_FAIL;
    HRESULT logonStatusHr = E_FAIL;
    HRESULT domainVisibleHr = E_FAIL;
    HRESULT emailVisibleHr = E_FAIL;

    const std::wstring displayName =
        ReadStringProperty(
            g_getDisplayName,
            userVm,
            &displayNameHr);

    const std::wstring qualifiedName =
        ReadStringProperty(
            g_getQualifiedName,
            userVm,
            &qualifiedNameHr);

    const std::wstring emailAddress =
        ReadStringProperty(
            g_getEmailAddress,
            userVm,
            &emailAddressHr);

    const std::wstring builtInStatus =
        ReadStringProperty(
            g_getLogonStatus,
            userVm,
            &logonStatusHr);

    const int loggedIn =
        ReadBoolProperty(
            g_getIsLoggedIn,
            userVm,
            &loggedInHr);

    const int statusVisible =
        ReadBoolProperty(
            g_getIsLogonStatusVisible,
            userVm,
            &statusVisibleHr);

    const int domainVisible =
        ReadBoolProperty(
            g_getIsDomainNameVisible,
            userVm,
            &domainVisibleHr);

    const int emailVisible =
        ReadBoolProperty(
            g_getIsEmailAddressVisible,
            userVm,
            &emailVisibleHr);

    HRESULT smallUserTileHr = E_FAIL;
    auto smallUserTileBitmap =
        ReadSmallUserTileBitmapImage(
            userVm,
            &smallUserTileHr);

    reinterpret_cast<IUnknown*>(
        userVm)->Release();
    userVm = nullptr;

    if (FAILED(displayNameHr) ||
        FAILED(qualifiedNameHr) ||
        qualifiedName.empty()) {
        return false;
    }

    // Diagnostic observer is independent of status composition and never
    // triggers extra row updates. Uses documented XAML event contracts only.
    AttachVmPropertyProbeForPresenter(presenter, qualifiedName);

    const bool isLoggedIn =
        SUCCEEDED(loggedInHr) &&
        loggedIn == 1;

    const auto* session =
        FindWtsSessionForQualifiedName(
            qualifiedName,
            sessions);

    // WTS is the authoritative membership source for legacy AuthUI-style
    // status. UserViewModel.IsLoggedIn is intentionally *not* required here:
    // on this build it can lag a real WTS_SESSION_LOGON/LOGOFF by several
    // seconds while the already-visible user list keeps the same UserViewModel.
    // Requiring both sources caused a freshly logged-on account to keep no
    // status row, and a freshly logged-off account to retain a stale "Locked"
    // row after its WTS session had already disappeared.
    const bool hasWtsSession =
        session != nullptr;

    const bool remote =
        session &&
        session->protocolType == 2 &&
        !session->clientName.empty() &&
        (session->state == WTSActive ||
         session->state == WTSConnected);

    if (remote) {
        ++stats.remoteRows;
    }

    std::wstring desiredStatus;

    const bool showStatus =
        g_showLogonStatusForAllLoggedIn.load(
            std::memory_order_relaxed);

    const StatusBehavior statusBehavior =
        g_statusBehavior.load(
            std::memory_order_relaxed);

    const SessionDetailsMode sessionDetailsMode =
        g_sessionDetailsMode.load(std::memory_order_relaxed);
    const bool customLayout =
        g_customRowLayoutEnabled.load(std::memory_order_acquire);
    const auto rowPlan = std::atomic_load_explicit(
        &g_rowLayoutPlan, std::memory_order_acquire);

    // A full row refresh can outlive a bridge publication. Never trust the
    // nativeTransientStatus copied into the earlier WTS snapshot for visible
    // text: a newer fast-path status (or clear) may already have reached XAML.
    // Re-read the live slot for this exact account/session at row-apply time.
    // This shrinks the stale-overwrite race from a whole tree-refresh window to
    // only the tiny per-row application window.
    LiveTransientForUser liveTransient =
        CaptureLiveTransientForQualifiedName(
            qualifiedName,
            session
                ? session->sessionId
                : 0xFFFFFFFF);

    // WTS membership/state wins over a transient writer's potentially stale
    // last state during RDP/local disconnection. A disconnected account may
    // still own a live LogonUI with the generic "Please wait" or "Locking"
    // TextBlock, but those words describe its own desktop transition, not
    // a new incoming sign-in. Preserve all specific service names and
    // terminal statuses (including "Signing out") as native observations.
    if (session &&
        session->state == WTSDisconnected &&
        (liveTransient.text == L"Please wait" ||
         liveTransient.text == L"Locking")) {
        liveTransient = LiveTransientForUser{};
    }

    const bool hasNativeTransientStatus =
        liveTransient.found &&
        !liveTransient.text.empty();

    const bool effectiveLoggedIn =
        (statusBehavior == StatusBehavior::LegacyAuthUi
            ? hasWtsSession
            : isLoggedIn) ||
        hasNativeTransientStatus;

    const DWORD activeConsoleSessionId =
        GetRowActiveConsoleSession();

    const bool isActiveConsoleSession =
        session &&
        session->sessionId == activeConsoleSessionId &&
        session->protocolType == 0;

    const bool builtInReportsLocked =
        SUCCEEDED(statusVisibleHr) &&
        SUCCEEDED(logonStatusHr) &&
        statusVisible == 1 &&
        EqualsInsensitive(builtInStatus, L"Locked");

    const bool consoleLocked =
        isActiveConsoleSession &&
        session &&
        (session->lockState == WtsLockState::Locked ||
         (session->lockState == WtsLockState::Unknown &&
          builtInReportsLocked));

    const bool anySessionLocked =
        session &&
        (session->lockState == WtsLockState::Locked ||
         (session->lockState == WtsLockState::Unknown &&
          builtInReportsLocked));

    const bool splitSessionSummary =
        session &&
        statusBehavior == StatusBehavior::LegacyAuthUi &&
        (sessionDetailsMode == SessionDetailsMode::Summary ||
         sessionDetailsMode == SessionDetailsMode::Full);

    if (showStatus && splitSessionSummary) {
        // Diagnostic/session-summary mode deliberately separates session
        // lifecycle state from connection origin. A disconnected local
        // session must not look identical to the active console session just
        // because both report protocol type 0. Lock state is meaningful while
        // the WTS session itself is Active; non-Active WTS states take
        // precedence in the summary line.
        switch (session->state) {
            case WTSActive:
                desiredStatus =
                    anySessionLocked
                        ? L"Locked"
                        : L"Signed in";
                break;
            case WTSConnected:
                desiredStatus = L"Connected";
                break;
            case WTSConnectQuery:
                desiredStatus = L"Connecting";
                break;
            case WTSShadow:
                desiredStatus = L"Shadowed";
                break;
            case WTSDisconnected:
                desiredStatus = L"Disconnected";
                break;
            case WTSIdle:
                desiredStatus = L"Idle";
                break;
            case WTSReset:
                desiredStatus = L"Resetting";
                break;
            case WTSDown:
                desiredStatus = L"Down";
                break;
            case WTSInit:
                desiredStatus = L"Initializing";
                break;
            case WTSListen:
            default:
                desiredStatus =
                    std::wstring(WtsStateName(session->state));
                break;
        }
    } else if (showStatus && remote) {
        desiredStatus =
            L"Signed in remotely from " +
            session->clientName;
    } else if (showStatus &&
               statusBehavior ==
                   StatusBehavior::LegacyAuthUi) {
        if (session) {
            // Legacy AuthUI semantics are session semantics. Normally, once
            // WTS membership disappears the status row disappears immediately
            // even if modern UserViewModel state is stale. alpha4j permits one
            // narrow exception: a live bridge-backed shadow snapshot can keep
            // the row just long enough to carry Windows' exact terminal
            // transient (for example, Signing out).
            desiredStatus =
                consoleLocked
                    ? L"Locked"
                    : L"Signed in";
        }
    } else if (showStatus && isLoggedIn) {
        // Windows 11 mode deliberately follows the current UserViewModel
        // visibility/text contract. Remote-client text above remains WTS-backed.
        if (SUCCEEDED(statusVisibleHr) &&
            SUCCEEDED(logonStatusHr) &&
            statusVisible == 1 &&
            !builtInStatus.empty()) {
            desiredStatus = builtInStatus;
        }
    }

    // alpha4j: if Windows itself currently owns a native transient message for
    // this exact WTS session, that HSTRING/TextBlock text is authoritative and
    // temporarily outranks reconstructed steady-state wording. Dead writers
    // are rejected immediately; empty/unloaded native messages clear the live
    // override. Remembered identity can persist until the next CREATE boundary,
    // but identity alone never creates a visible transient row.
    if (showStatus &&
        g_showNativeTransientStatus.load(
            std::memory_order_relaxed) &&
        hasNativeTransientStatus) {
        desiredStatus =
            liveTransient.text;
    }

    // A Custom diagnostic source never silently changes primary status
    // wording. The existing Summary/Full presets retain alpha4w behavior.
    const uint32_t customDetailFlags =
        g_customRowDetailFlags.load(std::memory_order_relaxed);
    const uint32_t rowDetailMask =
        customLayout && rowPlan ? rowPlan->neededDetailMask : 0;

    const bool detailSessionId = customLayout
        ? (rowDetailMask & kDetailSessionId) != 0
        : ShowDetailLine(sessionDetailsMode, customDetailFlags, kDetailSessionId, true);
    const bool detailConnection = customLayout
        ? (rowDetailMask & kDetailConnection) != 0
        : ShowDetailLine(sessionDetailsMode, customDetailFlags, kDetailConnection, true);
    const bool detailWtsState = customLayout
        ? (rowDetailMask & kDetailWtsState) != 0
        : ShowDetailLine(sessionDetailsMode, customDetailFlags, kDetailWtsState, false);
    const bool detailStation = customLayout
        ? (rowDetailMask & kDetailStation) != 0
        : ShowDetailLine(sessionDetailsMode, customDetailFlags, kDetailStation, false);
    const bool detailProtocol = customLayout
        ? (rowDetailMask & kDetailProtocol) != 0
        : ShowDetailLine(sessionDetailsMode, customDetailFlags, kDetailProtocol, false);
    const bool detailLockState = customLayout
        ? (rowDetailMask & kDetailLockState) != 0
        : ShowDetailLine(sessionDetailsMode, customDetailFlags, kDetailLockState, false);
    const bool detailVmLoggedIn = customLayout
        ? (rowDetailMask & kDetailVmLoggedIn) != 0
        : ShowDetailLine(sessionDetailsMode, customDetailFlags, kDetailVmLoggedIn, false);
    const bool detailVmVisible = customLayout
        ? (rowDetailMask & kDetailVmVisible) != 0
        : ShowDetailLine(sessionDetailsMode, customDetailFlags, kDetailVmVisible, false);
    const bool detailVmLogonStatus = customLayout
        ? (rowDetailMask & kDetailVmLogonStatus) != 0
        : ShowDetailLine(sessionDetailsMode, customDetailFlags, kDetailVmLogonStatus, false);

    SessionDetailPresentation sessionDetails;

    if (session) {
        if (detailSessionId) {
            sessionDetails.sessionLine =
                L"Session " + std::to_wstring(session->sessionId);
        }
        if (detailConnection) {
            if (session->state == WTSDisconnected) {
                // A disconnected session must not look like the active console.
                if (session->protocolType == 2) {
                    sessionDetails.connectionLine =
                        session->clientName.empty()
                            ? L"Remote session (disconnected)"
                            : L"Remote session from " +
                                  session->clientName + L" (disconnected)";
                } else if (session->protocolType == 0) {
                    sessionDetails.connectionLine =
                        L"Local session (disconnected)";
                } else {
                    sessionDetails.connectionLine =
                        L"Session disconnected (protocol " +
                        std::to_wstring(
                            static_cast<unsigned>(session->protocolType)) +
                        L")";
                }
            } else if (session->protocolType == 2) {
                sessionDetails.connectionLine =
                    session->clientName.empty()
                        ? L"Signed in remotely"
                        : L"Signed in remotely from " + session->clientName;
            } else if (session->protocolType == 0 &&
                       isActiveConsoleSession &&
                       session->state == WTSActive) {
                sessionDetails.connectionLine = L"Active console session";
            } else if (session->protocolType == 0) {
                sessionDetails.connectionLine =
                    L"Local session (" +
                    std::wstring(WtsStateName(session->state)) + L")";
            } else {
                sessionDetails.connectionLine =
                    L"Session (protocol " +
                    std::to_wstring(
                        static_cast<unsigned>(session->protocolType)) +
                    L", " + std::wstring(WtsStateName(session->state)) +
                    L")";
            }
        }
        if (detailWtsState) {
            sessionDetails.wtsStateLine =
                L"WTS state: " + std::wstring(WtsStateName(session->state));
        }
        if (detailStation && !session->stationName.empty()) {
            sessionDetails.stationLine = L"Station: " + session->stationName;
        }
        if (detailProtocol) {
            if (session->protocolType == 2) {
                sessionDetails.protocolLine = L"Protocol: RDP (2)";
            } else if (session->protocolType == 0) {
                sessionDetails.protocolLine = L"Protocol: Local console (0)";
            } else {
                sessionDetails.protocolLine =
                    L"Protocol: " +
                    std::to_wstring(static_cast<unsigned>(session->protocolType));
            }
        }
        if (detailLockState) {
            sessionDetails.lockStateLine =
                L"Lock state: " +
                std::wstring(WtsLockStateName(session->lockState));
        }
    }

    // In Full mode, preserve alpha4w's matched-WTS-session requirement.
    // In Custom mode, explicitly requested UserViewModel lines may appear
    // even for an account that has not yet acquired any WTS session.
    const bool includeVmDiagnostics =
        (sessionDetailsMode == SessionDetailsMode::Full && session) ||
        sessionDetailsMode == SessionDetailsMode::Custom || customLayout;
    if (includeVmDiagnostics) {
        if (detailVmLoggedIn) {
            sessionDetails.vmLoggedInLine =
                L"VM IsLoggedIn: " +
                (SUCCEEDED(loggedInHr)
                    ? std::to_wstring(loggedIn)
                    : std::wstring(L"<unavailable>"));
        }
        if (detailVmVisible) {
            sessionDetails.vmStatusVisibleLine =
                L"VM StatusVisible: " +
                (SUCCEEDED(statusVisibleHr)
                    ? std::to_wstring(statusVisible)
                    : std::wstring(L"<unavailable>"));
        }
        if (detailVmLogonStatus) {
            sessionDetails.vmLogonStatusLine =
                L"VM LogonStatus: " +
                (SUCCEEDED(logonStatusHr)
                    ? (builtInStatus.empty()
                        ? std::wstring(L"<empty>")
                        : builtInStatus)
                    : std::wstring(L"<unavailable>"));
        }
    }

    const bool forceQualifiedName =
        g_alwaysShowQualifiedName.load(
            std::memory_order_relaxed);

    const bool nativeQualifiedVisible =
        SUCCEEDED(domainVisibleHr) &&
        domainVisible == 1;

    bool desiredQualifiedVisible =
        forceQualifiedName ||
        nativeQualifiedVisible;

    // Layout rules affect only mod-owned presentation. Neither WTS matching
    // nor the computed Windows-oriented steady-state status is rewritten.
    UnifiedRowValues unifiedExtra;
    if (customLayout) {
        if (session && session->state == WTSActive && anySessionLocked)
            unifiedExtra.lockedStatus = L"Locked";
        if (session && session->protocolType == 2 &&
            (session->state == WTSActive || session->state == WTSConnected)) {
            unifiedExtra.remoteStatus = session->clientName.empty()
                ? L"Signed in remotely"
                : L"Signed in remotely from " + session->clientName;
        }
        if (SUCCEEDED(statusVisibleHr) && statusVisible == 1 &&
            SUCCEEDED(logonStatusHr)) unifiedExtra.nativeStatus = builtInStatus;
        unifiedExtra.customText = CustomRowTextForAccount(qualifiedName);
    }

    const bool wantsCompactRow =
        !effectiveLoggedIn &&
        g_compactUnsignedRows.load(
            std::memory_order_relaxed);

    auto textGrid =
        FindExpectedUserTextGrid(
            presenter,
            displayName,
            qualifiedName,
            0);

    if (!textGrid) {
        // If this row doesn't need any feature right now, absence of our
        // exact template signature is not an error.
        if (desiredStatus.empty() &&
            sessionDetails.Empty() &&
            !customLayout &&
            !forceQualifiedName &&
            !wantsCompactRow) {
            return false;
        }

        ++stats.signatureMisses;

        ProbeLog(
            L"RESTORE_ROW signature-miss DisplayName=\"%s\" "
            L"QualifiedName=\"%s\" builtInStatus=\"%s\" "
            L"domainVisible=%d emailVisible=%d email=\"%s\"",
            displayName.c_str(),
            qualifiedName.c_str(),
            builtInStatus.c_str(),
            domainVisible,
            emailVisible,
            emailAddress.c_str());

        return false;
    }

    std::vector<
        winrt::Windows::UI::Xaml::Controls::TextBlock>
        textBlocks;
    bool alreadyPatched = false;

    if (!CollectUserTextBlocksFromContainer(
            textGrid,
            displayName,
            qualifiedName,
            textBlocks,
            &alreadyPatched)) {

        ++stats.signatureMisses;
        return false;
    }

    winrt::Windows::UI::Xaml::Controls::TextBlock
        displayNameSlot{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock
        statusSlot{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock
        qualifiedNameSlot{nullptr};

    if (!IdentifyUserTextSlots(
            textBlocks,
            displayName,
            qualifiedName,
            alreadyPatched,
            &displayNameSlot,
            &statusSlot,
            &qualifiedNameSlot)) {

        ++stats.signatureMisses;
        return false;
    }

    bool changed = false;

    const bool needStackLayout =
        alreadyPatched ||
        !desiredStatus.empty() ||
        customLayout ||
        !sessionDetails.Empty() ||
        (forceQualifiedName &&
         !nativeQualifiedVisible) ||
        wantsCompactRow;

    // Once we take ownership of the visible text triplet, the native 48x48
    // user-tile Ellipse becomes the only remaining binding-owned visual that
    // can lag behind a recycled presenter. Overlay it with the BitmapImage from
    // the exact same UserViewModel snapshot. The native ellipse stays untouched
    // underneath for lossless hot-disable restoration.
    bool ownedUserTile = false;
    if (needStackLayout) {
        bool tileStructureChanged = false;
        if (smallUserTileBitmap &&
            EnsureOwnedUserTileVisual(
                textGrid,
                smallUserTileBitmap,
                displayName,
                &tileStructureChanged)) {

            ownedUserTile = true;
            ++stats.userTileRows;
            if (tileStructureChanged) {
                changed = true;
            }
        } else {
            ++stats.userTileMisses;

            if (g_enableLayoutDiagnostics.load(
                    std::memory_order_relaxed)) {
                ProbeLog(
                    L"USER_TILE unavailable DisplayName=\"%s\" hr=0x%08X bitmap=%u",
                    displayName.c_str(),
                    static_cast<unsigned>(smallUserTileHr),
                    smallUserTileBitmap ? 1u : 0u);
            }
        }
    }

    // Convert/upgrade the row before writing any visible text. All three core visible
    // lines must be mod-owned; otherwise a recycled presenter can pair a stale
    // Microsoft DisplayName binding with status/qualified-name text from the
    // presenter's new UserViewModel.
    if (needStackLayout) {
        bool structureChanged = false;

        EnsureStackedUserTextLayout(
            textGrid,
            displayName,
            qualifiedName,
            &structureChanged);

        textBlocks.clear();
        bool converted = false;

        if (!CollectUserTextBlocksFromContainer(
                textGrid,
                displayName,
                qualifiedName,
                textBlocks,
                &converted) ||
            !converted ||
            !IdentifyUserTextSlots(
                textBlocks,
                displayName,
                qualifiedName,
                true,
                &displayNameSlot,
                &statusSlot,
                &qualifiedNameSlot) ||
            ElementName(displayNameSlot) !=
                kInjectedDisplayTextName) {

            ++stats.signatureMisses;
            ProbeLog(
                L"RESTORE_ROW conversion failed DisplayName=\"%s\" QualifiedName=\"%s\"",
                displayName.c_str(),
                qualifiedName.c_str());
            return false;
        }

        alreadyPatched = true;
        if (structureChanged) {
            changed = true;
        }
    }

    unsigned visibleLines = 0;

    if (alreadyPatched) {
        // DisplayName is mod-owned in alpha4c. Refresh it from the exact same
        // UserViewModel snapshot as Status and QualifiedName so one presenter
        // can never expose a cross-user combination after container recycling.
        if (TextBlockText(displayNameSlot) !=
            displayName) {

            displayNameSlot.Text(
                winrt::hstring(
                    displayName));
            changed = true;
        }

        if (displayNameSlot.Visibility() !=
            Visibility::Visible) {
            displayNameSlot.Visibility(
                Visibility::Visible);
            changed = true;
        }

        // QualifiedName is now a mod-owned line. Always refresh its text from
        // the UserViewModel so row virtualization cannot leave stale account
        // data behind.
        if (TextBlockText(qualifiedNameSlot) !=
            qualifiedName) {

            qualifiedNameSlot.Text(
                winrt::hstring(
                    qualifiedName));
            changed = true;
        }

        const auto desiredQualifiedVisibility =
            desiredQualifiedVisible
                ? Visibility::Visible
                : Visibility::Collapsed;

        if (qualifiedNameSlot.Visibility() !=
            desiredQualifiedVisibility) {

            qualifiedNameSlot.Visibility(
                desiredQualifiedVisibility);
            changed = true;
        }

        if (forceQualifiedName &&
            desiredQualifiedVisible) {
            ++stats.qualifiedNameRows;
        }

        if (!desiredStatus.empty()) {
            if (TextBlockText(statusSlot) !=
                desiredStatus) {

                statusSlot.Text(
                    winrt::hstring(
                        desiredStatus));
                changed = true;
            }

            if (statusSlot.Visibility() !=
                Visibility::Visible) {

                statusSlot.Visibility(
                    Visibility::Visible);
                changed = true;
            }

            ++stats.statusRows;
        } else {
            if (!TextBlockText(statusSlot).empty()) {
                statusSlot.Text(
                    winrt::hstring());
                changed = true;
            }

            if (statusSlot.Visibility() !=
                Visibility::Collapsed) {

                statusSlot.Visibility(
                    Visibility::Collapsed);
                changed = true;
            }
        }

        visibleLines =
            EnsureStackedUserTextLayout(
                textGrid,
                displayName,
                qualifiedName);

        const auto visualSync =
            SynchronizeOwnedUserRowVisuals(
                presenter,
                displayName,
                qualifiedName,
                desiredStatus,
                desiredQualifiedVisible,
                sessionDetails,
                smallUserTileBitmap,
                customLayout,
                rowPlan,
                unifiedExtra);

        if (visualSync.changed) {
            changed = true;
        }

        // The normal fast path updates the first validated row Grid. The full
        // owned-visual sync above additionally repairs any duplicate/recycled
        // branch that LogonUX kept alive under the same presenter. Count the
        // tile as owned if either path reached at least one valid overlay.
        if (visualSync.tileBranches != 0) {
            ownedUserTile = true;
        }

        visibleLines =
            CountVisibleInjectedUserTextLines(
                textGrid);
    } else {
        // No mod-owned presentation is needed for this row. Leave all native
        // Text/Visibility bindings completely untouched.
        for (const auto& tb :
             {displayNameSlot, statusSlot, qualifiedNameSlot}) {
            if (tb.Visibility() ==
                Visibility::Visible) {
                ++visibleLines;
            }
        }
    }

    ApplyUserRowHeightMode(
        presenter,
        effectiveLoggedIn,
        visibleLines);

    if (changed) {
        ++stats.patchedRows;

        ProbeLog(
            L"RESTORE_ROW updated DisplayName=\"%s\" "
            L"QualifiedName=\"%s\" loggedIn=%d effectiveLoggedIn=%u remote=%u "
            L"statusBehavior=%s sessionDetails=%s session=%s%lu lockState=%s "
            L"status=\"%s\" nativeTransient=\"%s\" transientWriterPid=%lu transientSerial=%llu "
            L"qualifiedVisible=%u lines=%u tile=%s",
            displayName.c_str(),
            qualifiedName.c_str(),
            loggedIn,
            effectiveLoggedIn ? 1u : 0u,
            remote ? 1u : 0u,
            StatusBehaviorName(statusBehavior),
            SessionDetailsModeName(sessionDetailsMode),
            session ? L"" : L"<none>/",
            session ? session->sessionId : 0,
            session
                ? WtsLockStateName(session->lockState)
                : L"Unknown",
            desiredStatus.empty()
                ? L"<hidden>"
                : desiredStatus.c_str(),
            hasNativeTransientStatus
                ? liveTransient.text.c_str()
                : L"",
            hasNativeTransientStatus
                ? liveTransient.writerPid
                : 0,
            static_cast<unsigned long long>(
                hasNativeTransientStatus
                    ? liveTransient.serial
                    : 0),
            desiredQualifiedVisible ? 1u : 0u,
            visibleLines,
            ownedUserTile ? L"owned" : L"native-fallback");
    } else if (
        g_enableLayoutDiagnostics.load(
            std::memory_order_relaxed)) {

        ProbeLog(
            L"RESTORE_ROW unchanged DisplayName=\"%s\" "
            L"loggedIn=%d effectiveLoggedIn=%u remote=%u sessionDetails=%s lines=%u",
            displayName.c_str(),
            loggedIn,
            effectiveLoggedIn ? 1u : 0u,
            remote ? 1u : 0u,
            SessionDetailsModeName(sessionDetailsMode),
            visibleLines);
    }

    ++stats.completedRows;
    return changed;
}

struct FastTransientApplyStats {
    unsigned knownSlots = 0;
    unsigned presenters = 0;
    unsigned matchedRows = 0;
    unsigned ownedBranches = 0;
    unsigned changedBranches = 0;
    unsigned pendingSlots = 0;
    bool pendingRowVisible = false;
    bool needsFullRefresh = false;
};

bool IsLiveTransientWriterSlot(
    const TransientBridgeSlot& slot) {

    // The console worker already owns process-handle death watches and forces a
    // full cleanup when a writer exits. Do not OpenProcess/GetProcessTimes from
    // the XAML fast path for every native string; a published writer identity is
    // enough until that event-driven death cleanup says otherwise.
    return slot.sessionId != 0xFFFFFFFF &&
           slot.writerPid != 0 &&
           slot.writerCreateTime != 0;
}

bool IsRemoteHintedTransientSlot(
    const TransientBridgeSlot& slot) {

    return slot.protocolType == 2 ||
           slot.lastClient[0] != L'\0';
}

// Transport is *not* the criterion for an unassigned sign-in session.
// Windows can run LogonUI on a local console session with no WTS username,
// including at first boot or following a console session switch. Conversely,
// a protocol=0 observation alone does not prove a remote connection.
const wchar_t* UnassignedSessionTransportName(
    const TransientBridgeSlot& slot) {
    if (IsRemoteHintedTransientSlot(slot)) {
        return slot.protocolType == 2 ? L"RDP" : L"Remote";
    }
    if (slot.protocolType == 0) {
        return L"Console";
    }
    return L"Undetermined";
}

bool IsPendingUnassignedUserTransientSlot(
    const TransientBridgeSlot& slot) {

    if (!IsLiveTransientWriterSlot(slot) ||
        slot.lastUser[0] != L'\0') {
        return false;
    }

    // Preserve the pre-auth RDP handoff exclusion, now for local and remote
    // sessions alike. Never create a row for a disconnected, idle, listener,
    // reset, or terminated session just because a stale process remains live.
    const auto state =
        static_cast<WTS_CONNECTSTATE_CLASS>(slot.wtsState);
    switch (state) {
        case WTSDisconnected:
        case WTSIdle:
        case WTSListen:
        case WTSReset:
        case WTSDown:
            return false;
        default:
            return true;
    }
}

struct ConsoleTransientBurstState {
    bool active = false;
    ULONGLONG bridgeSerial = 0;
    ULONGLONG newestTextSerial = 0;
    DWORD sessionId = 0xFFFFFFFF;
    DWORD writerPid = 0;
};

ConsoleTransientBurstState CaptureConsoleTransientBurstState() {
    ConsoleTransientBurstState state{};

    if (!g_showNativeTransientStatus.load(
            std::memory_order_acquire)) {
        return state;
    }

    const auto snapshot =
        CaptureTransientBridgeSnapshot();
    state.bridgeSerial = snapshot.serial;

    for (const auto& slot : snapshot.slots) {
        if (!IsLiveTransientWriterSlot(slot) ||
            slot.serial == 0 ||
            slot.text[0] == L'\0' ||
            !IsSameProcessInstanceAlive(
                slot.writerPid,
                slot.writerCreateTime) ||
            IsDisconnectedGenericNativeTransientStatusSlot(slot)) {
            continue;
        }

        if (!state.active ||
            slot.serial > state.newestTextSerial) {
            state.active = true;
            state.newestTextSerial = slot.serial;
            state.sessionId = slot.sessionId;
            state.writerPid = slot.writerPid;
        }
    }

    return state;
}

void CollectUsersListViewsRecursive(
    const FrameworkElement& element,
    std::vector<Controls::ListViewBase>& out,
    int depth,
    int& budget) {

    if (!element ||
        depth > 32 ||
        budget <= 0) {
        return;
    }

    --budget;

    try {
        const std::wstring className =
            SafeClassName(element);

        if (className.find(
                L"LogonUX.UsersListView") !=
            std::wstring::npos) {

            if (auto list =
                    element.try_as<Controls::ListViewBase>()) {
                out.push_back(list);
                return;
            }
        }

        const int count =
            Media::VisualTreeHelper::GetChildrenCount(
                element);

        for (int i = 0;
             i < count &&
             budget > 0;
             ++i) {

            auto child =
                Media::VisualTreeHelper::GetChild(
                    element,
                    i)
                    .try_as<FrameworkElement>();

            if (child) {
                CollectUsersListViewsRecursive(
                    child,
                    out,
                    depth + 1,
                    budget);
            }
        }
    } catch (...) {
    }
}

Controls::ListViewBase ChooseUsersListView(
    const FrameworkElement& root) {

    std::vector<Controls::ListViewBase> lists;
    int budget = 500;
    CollectUsersListViewsRecursive(
        root,
        lists,
        0,
        budget);

    for (const auto& list : lists) {
        try {
            if (list.Visibility() == Visibility::Visible &&
                list.ActualWidth() > 0.0 &&
                list.ActualHeight() > 0.0) {
                return list;
            }
        } catch (...) {
        }
    }

    return lists.empty()
        ? Controls::ListViewBase{nullptr}
        : lists.front();
}

Controls::Border FindOwnedPendingRemoteAttemptFooter(
    const Controls::ListViewBase& list) {

    if (!list) {
        return nullptr;
    }

    try {
        auto footer = list.Footer();
        if (!footer) {
            return nullptr;
        }

        auto footerElement =
            footer.try_as<FrameworkElement>();
        if (!footerElement ||
            ElementName(footerElement) !=
                kPendingRemoteAttemptFooterName) {
            return nullptr;
        }

        return footerElement.try_as<Controls::Border>();
    } catch (...) {
    }

    return nullptr;
}

Controls::Border FindOwnedPendingRemoteAttemptHeader(
    const Controls::ListViewBase& list) {

    if (!list) {
        return nullptr;
    }

    try {
        auto header = list.Header();
        if (!header) {
            return nullptr;
        }

        auto headerElement =
            header.try_as<FrameworkElement>();
        if (!headerElement ||
            ElementName(headerElement) !=
                kPendingRemoteAttemptHeaderName) {
            return nullptr;
        }

        return headerElement.try_as<Controls::Border>();
    } catch (...) {
    }

    return nullptr;
}

bool RemoveOwnedPendingRemoteAttemptHeader(
    const Controls::ListViewBase& list,
    const wchar_t* reason) {

    if (!list) {
        return false;
    }
    try {
        if (!FindOwnedPendingRemoteAttemptHeader(list)) {
            return false;
        }
        list.Header(nullptr);
        if (g_enableLayoutDiagnostics.load(
                std::memory_order_relaxed)) {
            ProbeLog(
                L"PENDING_REMOTE_HEADER removed reason=%s",
                reason ? reason : L"<unknown>");
        }
        return true;
    } catch (...) {
        return false;
    }
}

// Alpha4u uses only the supported ListView Footer/owned user-row Status
// destination. The following two helpers identify and remove alpha4t's
// obsolete *mod-owned* overlay; no new overlays are created.
Controls::Grid FindPendingRemoteOverlayHost(
    const Controls::ListViewBase& list) {

    if (!list) {
        return nullptr;
    }

    try {
        auto parent = Media::VisualTreeHelper::GetParent(list);
        for (int depth = 0; parent && depth < 8; ++depth) {
            if (auto grid = parent.try_as<Controls::Grid>()) {
                if (grid.ActualHeight() >= 40.0 &&
                    grid.ActualWidth() >= 160.0) {
                    return grid;
                }
            }
            parent = Media::VisualTreeHelper::GetParent(parent);
        }
    } catch (...) {
    }
    return nullptr;
}

bool RemoveOwnedPendingRemoteAttemptOverlay(
    const Controls::ListViewBase& list,
    const wchar_t* reason) {

    auto host = FindPendingRemoteOverlayHost(list);
    if (!host) {
        return false;
    }
    try {
        auto children = host.Children();
        for (uint32_t i = 0; i < children.Size(); ++i) {
            auto element = children.GetAt(i)
                               .try_as<FrameworkElement>();
            if (!element ||
                ElementName(element) !=
                    kPendingRemoteAttemptOverlayName) {
                continue;
            }
            children.RemoveAt(i);
            if (g_enableLayoutDiagnostics.load(
                    std::memory_order_relaxed)) {
                ProbeLog(L"PENDING_REMOTE_OVERLAY removed reason=%s",
                    reason ? reason : L"<unknown>");
            }
            return true;
        }
    } catch (...) {
    }
    return false;
}

bool FindPendingRemoteAttemptFooterTextBlocks(
    const Controls::Border& footer,
    Controls::TextBlock* titleOut,
    Controls::TextBlock* bodyOut) {

    if (titleOut) {
        *titleOut = Controls::TextBlock{nullptr};
    }
    if (bodyOut) {
        *bodyOut = Controls::TextBlock{nullptr};
    }

    if (!footer) {
        return false;
    }

    try {
        auto stack =
            footer.Child()
                .try_as<Controls::StackPanel>();
        if (!stack) {
            return false;
        }

        auto children = stack.Children();
        for (uint32_t i = 0;
             i < children.Size();
             ++i) {

            auto tb =
                children.GetAt(i)
                    .try_as<Controls::TextBlock>();
            if (!tb) {
                continue;
            }

            const auto name = ElementName(tb);
            if (name == kPendingRemoteAttemptTitleName) {
                if (titleOut) {
                    *titleOut = tb;
                }
            } else if (
                name == kPendingRemoteAttemptBodyName) {
                if (bodyOut) {
                    *bodyOut = tb;
                }
            }
        }
    } catch (...) {
        return false;
    }

    return (!titleOut || *titleOut) &&
           (!bodyOut || *bodyOut);
}

std::wstring BuildPendingRemoteAttemptBody(
    const std::vector<TransientBridgeSlot>& pending) {

    std::wstring body;

    for (size_t i = 0;
         i < pending.size();
         ++i) {

        // Newest observation first. Otherwise a persistent anonymous
        // local-console slot can push the incoming RDP status out of view.
        const auto& slot = pending[pending.size() - i - 1];
        if (i != 0) {
            body += L"\n\n";
        }

        wchar_t header[256]{};
        _snwprintf_s(
            header,
            _countof(header),
            _TRUNCATE,
            L"Session %lu | %s | %s | PID %lu",
            slot.sessionId,
            UnassignedSessionTransportName(slot),
            WtsStateName(
                slot.wtsState <= static_cast<DWORD>(WTSInit)
                    ? static_cast<WTS_CONNECTSTATE_CLASS>(slot.wtsState)
                    : WTSDown),
            slot.writerPid);
        body += header;

        if (IsRemoteHintedTransientSlot(slot)) {
            body += L"\nClient: ";
            body += slot.lastClient[0] != L'\0'
                ? slot.lastClient
                : L"<not reported>";
        } else if (slot.protocolType == 0) {
            body += L"\nConnection: local console";
        } else {
            body += L"\nConnection: not determined";
        }

        body += L"\nAccount: <not assigned by WTS yet>";

        // The Footer Status: line is the ONLY pre-identity destination for
        // a native message. No overlay, Header, synthetic message, or delay.
        body += L"\nStatus: ";
        body += slot.text[0] != L'\0'
            ? slot.text
            : L"<native status currently empty>";
    }

    return body;
}

bool RemovePendingRemoteAttemptRows(
    const FrameworkElement& root,
    const wchar_t* reason) {

    if (!root) {
        return false;
    }

    std::vector<Controls::ListViewBase> lists;
    int budget = 500;
    CollectUsersListViewsRecursive(
        root,
        lists,
        0,
        budget);

    bool removed = false;

    for (const auto& list : lists) {
        if (!list) {
            continue;
        }

        removed |= RemoveOwnedPendingRemoteAttemptOverlay(
            list, reason);
        try {
            auto header = list.Header();
            if (header) {
                auto headerElement =
                    header.try_as<FrameworkElement>();
                if (headerElement &&
                    ElementName(headerElement) ==
                        kPendingRemoteAttemptHeaderName) {
                    // Clear only the Header positively identified as ours.
                    list.Header(nullptr);
                    removed = true;
                }
            }

            auto footer = list.Footer();
            if (footer) {
                auto footerElement =
                    footer.try_as<FrameworkElement>();
                if (footerElement &&
                    ElementName(footerElement) ==
                        kPendingRemoteAttemptFooterName) {
                    // Clear only the Footer positively identified as ours.
                    list.Footer(nullptr);
                    removed = true;
                }
            }
        } catch (const winrt::hresult_error& e) {
            ProbeLog(
                L"PENDING_REMOTE_FOOTER remove failed hr=0x%08X reason=%s",
                static_cast<unsigned>(e.code().value),
                reason ? reason : L"<unknown>");
        } catch (...) {
            ProbeLog(
                L"PENDING_REMOTE_FOOTER remove failed reason=%s",
                reason ? reason : L"<unknown>");
        }
    }

    if (removed) {
        ProbeLog(
            L"PENDING_REMOTE_VISUALS removed reason=%s",
            reason ? reason : L"<unknown>");
    }

    return removed;
}

bool UpdatePendingRemoteAttemptRow(
    const FrameworkElement& root,
    const TransientBridgeSnapshot& snapshot,
    unsigned* pendingCountOut = nullptr) {

    if (pendingCountOut) {
        *pendingCountOut = 0;
    }

    if (!root ||
        !g_showPendingRemoteAttempt.load(
            std::memory_order_acquire)) {
        RemovePendingRemoteAttemptRows(
            root,
            L"disabled");
        return false;
    }

    DWORD processSession = 0xFFFFFFFF;
    if (!ProcessIdToSessionId(
            GetCurrentProcessId(),
            &processSession) ||
        processSession == 0xFFFFFFFF) {
        return false;
    }

    // An active console's user list can show other incoming unassigned
    // sessions. A LogonUI in a different session may show only its OWN
    // unassigned session: this also covers the console handoff interval when
    // WTSGetActiveConsoleSessionId() temporarily returns 0xFFFFFFFF.
    const DWORD activeConsoleSession =
        WTSGetActiveConsoleSessionId();
    const bool isConsoleReader =
        processSession == activeConsoleSession;

    std::vector<TransientBridgeSlot> pending;
    for (const auto& slot : snapshot.slots) {
        if (!IsPendingUnassignedUserTransientSlot(slot) ||
            (!isConsoleReader &&
             slot.sessionId != processSession)) {
            continue;
        }
        pending.push_back(slot);
    }

    std::sort(
        pending.begin(),
        pending.end(),
        [](const TransientBridgeSlot& a,
           const TransientBridgeSlot& b) {
            return a.serial < b.serial;
        });

    if (pendingCountOut) {
        *pendingCountOut =
            static_cast<unsigned>(pending.size());
    }

    auto list = ChooseUsersListView(root);
    if (!list) {
        if (pending.empty()) {
            RemovePendingRemoteAttemptRows(
                root, L"no pending remote writer/list unavailable");
        } else {
            ProbeLog(
                L"PENDING_REMOTE_FOOTER no visible LogonUX.UsersListView/ListViewBase; pending=%u",
                static_cast<unsigned>(pending.size()));
        }
        return false;
    }

    // Always clean up alpha4t's legacy overlay and earlier Header if still
    // present. Do not attach new floating controls or use the virtualizing
    // ItemsStackPanel/Items collection. The existing Footer/row are enough.
    RemoveOwnedPendingRemoteAttemptHeader(
        list, L"alpha4u uses Footer Status only");
    RemoveOwnedPendingRemoteAttemptOverlay(
        list, L"alpha4u uses Footer Status only");

    if (pending.empty()) {
        try {
            if (FindOwnedPendingRemoteAttemptFooter(list)) {
                list.Footer(nullptr);
                ProbeLog(
                    L"PENDING_REMOTE_FOOTER removed reason=identity assigned/session handed off");
            }
        } catch (...) {
        }
        return false;
    }

    // Only the mod-owned Footer carries pre-identity status. Once the
    // qualified WTS identity is known, the existing user's Status row wins.
    const std::wstring title =
        pending.size() != 1
            ? L"Unassigned sign-in sessions (" +
                  std::to_wstring(pending.size()) + L")"
            : IsRemoteHintedTransientSlot(pending.back())
                ? L"Remote sign-in attempt"
                : pending.back().protocolType == 0
                    ? L"Unassigned local sign-in session"
                    : L"Unassigned sign-in session";
    const std::wstring body =
        BuildPendingRemoteAttemptBody(pending);

    try {
        Controls::Border footer =
            FindOwnedPendingRemoteAttemptFooter(
                list);
        Controls::TextBlock titleBlock{nullptr};
        Controls::TextBlock bodyBlock{nullptr};

        if (!footer) {
            auto existingFooter = list.Footer();
            if (existingFooter) {
                // The ListView already owns a Footer that isn't positively
                // identified as ours. Do not replace or wrap it.
                auto existingElement =
                    existingFooter.try_as<FrameworkElement>();
                const std::wstring existingClass =
                    existingElement
                        ? SafeClassName(existingElement)
                        : L"<non-FrameworkElement>";
                const std::wstring existingName =
                    existingElement
                        ? ElementName(existingElement)
                        : L"<none>";
                ProbeLog(
                    L"PENDING_REMOTE_FOOTER existing non-owned Footer class=\"%s\" name=\"%s\"; failing closed",
                    existingClass.c_str(),
                    existingName.c_str());
                return false;
            }

            footer = Controls::Border();
            footer.Name(
                kPendingRemoteAttemptFooterName);
            footer.IsHitTestVisible(false);
            footer.HorizontalAlignment(
                HorizontalAlignment::Stretch);
            footer.Padding(
                Thickness{12.0, 7.0, 8.0, 7.0});
            footer.MinHeight(60.0);

            Controls::StackPanel stack;
            stack.Orientation(
                Controls::Orientation::Vertical);
            stack.HorizontalAlignment(
                HorizontalAlignment::Stretch);

            titleBlock = Controls::TextBlock();
            titleBlock.Name(
                kPendingRemoteAttemptTitleName);
            titleBlock.FontSize(15.0);
            titleBlock.TextWrapping(
                TextWrapping::Wrap);

            bodyBlock = Controls::TextBlock();
            bodyBlock.Name(
                kPendingRemoteAttemptBodyName);
            bodyBlock.FontSize(12.0);
            bodyBlock.Opacity(0.82);
            bodyBlock.TextWrapping(
                TextWrapping::Wrap);

            stack.Children().Append(
                titleBlock);
            stack.Children().Append(
                bodyBlock);
            footer.Child(stack);

            // Footer is a supported ListViewBase content property. Unlike the
            // alpha4k experiment, this never touches the virtualizing
            // ItemsStackPanel or the ListView's Items collection.
            list.Footer(footer);

            ProbeLog(
                L"UNASSIGNED_SESSION_FOOTER installed count=%u session=%lu transport=%s activeConsole=%lu viewerSession=%lu",
                static_cast<unsigned>(pending.size()),
                pending.back().sessionId,
                UnassignedSessionTransportName(pending.back()),
                activeConsoleSession,
                processSession);
            ProbeLog(
                L"PENDING_REMOTE_FOOTER installed pending=%u session=%lu client=\"%s\"",
                static_cast<unsigned>(pending.size()),
                pending.back().sessionId,
                pending.back().lastClient[0] != L'\0'
                    ? pending.back().lastClient
                    : L"");
        } else if (
            !FindPendingRemoteAttemptFooterTextBlocks(
                footer,
                &titleBlock,
                &bodyBlock)) {

            ProbeLog(
                L"PENDING_REMOTE_FOOTER malformed owned visual; failing closed");
            return false;
        }

        if (!titleBlock || !bodyBlock) {
            ProbeLog(
                L"PENDING_REMOTE_FOOTER missing owned TextBlocks; failing closed");
            return false;
        }

        bool changed = false;
        if (TextBlockText(titleBlock) != title) {
            titleBlock.Text(
                winrt::hstring(title));
            changed = true;
        }
        if (TextBlockText(bodyBlock) != body) {
            bodyBlock.Text(
                winrt::hstring(body));
            changed = true;
        }
        if (footer.Visibility() !=
            Visibility::Visible) {
            footer.Visibility(
                Visibility::Visible);
            changed = true;
        }

        if (changed &&
            g_enableLayoutDiagnostics.load(
                std::memory_order_relaxed)) {
            ProbeLog(
                L"PENDING_STATUS_LINE newestSession=%lu pending=%u newestText=\"%s\" listHeight=%.1f footerHeight=%.1f",
                pending.back().sessionId,
                static_cast<unsigned>(pending.size()),
                pending.back().text,
                list.ActualHeight(), footer.ActualHeight());
            ProbeLog(
                L"PENDING_REMOTE_FOOTER updated pending=%u session=%lu",
                static_cast<unsigned>(pending.size()),
                pending.back().sessionId);
        }

        return true;
    } catch (const winrt::hresult_error& e) {
        ProbeLog(
            L"PENDING_REMOTE_FOOTER install/update failed hr=0x%08X; failing closed",
            static_cast<unsigned>(e.code().value));
    } catch (...) {
        ProbeLog(
            L"PENDING_REMOTE_FOOTER install/update failed; failing closed");
    }

    return false;
}

struct FastOwnedStatusResult {
    unsigned branches = 0;
    unsigned changed = 0;
};

FastOwnedStatusResult ApplyFastTransientTextToOwnedPresenter(
    const FrameworkElement& presenter,
    const std::wstring& text) {

    FastOwnedStatusResult result{};
    if (!presenter || text.empty()) {
        return result;
    }

    std::vector<FrameworkElement> ownedTextGrids;
    CollectOwnedUserTextGridsRecursive(
        presenter,
        ownedTextGrids,
        0);

    for (const auto& gridElement :
         ownedTextGrids) {
        try {
            auto grid =
                gridElement.try_as<Controls::Grid>();
            if (!grid) {
                continue;
            }

            Controls::StackPanel stack{nullptr};
            auto gridChildren = grid.Children();
            for (uint32_t i = 0;
                 i < gridChildren.Size();
                 ++i) {
                auto candidate =
                    gridChildren.GetAt(i)
                        .try_as<Controls::StackPanel>();
                if (IsInjectedStatusStack(candidate)) {
                    stack = candidate;
                    break;
                }
            }

            if (!stack) {
                continue;
            }

            auto children = stack.Children();
            if (children.Size() < 2) {
                continue;
            }

            Controls::TextBlock statusSlot{nullptr};
            for (uint32_t j = 0; j < children.Size(); ++j) {
                auto candidate = children.GetAt(j).try_as<Controls::TextBlock>();
                if (candidate && ElementName(candidate) == kInjectedStatusTextName) {
                    statusSlot = candidate;
                    break;
                }
            }
            if (!statusSlot) continue;
            if (g_customRowLayoutEnabled.load(std::memory_order_acquire)) {
                // Priority expressions require a coherent full row snapshot.
                // Never fast-write only one source of a custom row.
                continue;
            }

            ++result.branches;
            bool changed = false;

            if (TextBlockText(statusSlot) != text) {
                statusSlot.Text(
                    winrt::hstring(text));
                changed = true;
            }
            if (statusSlot.Visibility() !=
                Visibility::Visible) {
                statusSlot.Visibility(
                    Visibility::Visible);
                changed = true;
            }

            // Deliberately do not call UpdateLayout here. Setting the native-
            // speed Text/Visibility properties is enough for the next XAML
            // render frame and avoids turning a 30-ms status into a full row
            // rebuild.
            if (changed) {
                ++result.changed;
            }
        } catch (...) {
        }
    }

    return result;
}

FastTransientApplyStats ApplyFastNativeTransientStatus(
    const FrameworkElement& root,
    const TransientBridgeSnapshot& snapshot) {

    FastTransientApplyStats stats{};

    if (!root ||
        !g_showNativeTransientStatus.load(
            std::memory_order_acquire)) {
        return stats;
    }

    struct KnownSlotRef {
        const TransientBridgeSlot* slot = nullptr;
        bool matched = false;
    };
    std::vector<KnownSlotRef> known;

    for (const auto& slot : snapshot.slots) {
        if (!IsLiveTransientWriterSlot(slot)) {
            continue;
        }

        if (slot.lastUser[0] == L'\0') {
            if (IsPendingUnassignedUserTransientSlot(slot)) {
                ++stats.pendingSlots;
            }
            continue;
        }

        if (slot.text[0] == L'\0') {
            continue;
        }

        if (IsDisconnectedGenericNativeTransientStatusSlot(slot)) {
            // The native disconnected-session LogonUI has its own generic
            // wait state, but the console row must revert to WTS state.
            stats.needsFullRefresh = true;
            continue;
        }

        known.push_back(
            KnownSlotRef{&slot, false});
    }

    stats.knownSlots =
        static_cast<unsigned>(known.size());

    // Pre-identity native statuses can be as brief as one frame. Present the
    // latest snapshot before walking every realized account presenter. This
    // avoids adding row-lookup latency to the anonymous pending-status path.
    stats.pendingRowVisible =
        UpdatePendingRemoteAttemptRow(
            root, snapshot, &stats.pendingSlots);

    // A clear/unload publication must restore the normal reconstructed row.
    // That computation needs WTS/UserViewModel state, so hand only this rare
    // transition back to the full path.
    bool latestIsClear = false;
    if (snapshot.serial != 0) {
        for (const auto& slot : snapshot.slots) {
            if (slot.serial == snapshot.serial &&
                slot.text[0] == L'\0') {
                latestIsClear = true;
                break;
            }
        }
    }

    std::vector<
        std::shared_ptr<PresenterDataContextRegistration>>
        registrations;

    AcquireSRWLockShared(
        &g_presenterDataContextLock);
    registrations =
        g_presenterDataContextRegistrations;
    ReleaseSRWLockShared(
        &g_presenterDataContextLock);

    const DWORD currentThreadId =
        GetCurrentThreadId();

    for (const auto& registration :
         registrations) {
        if (!registration ||
            !registration->active.load(
                std::memory_order_acquire) ||
            registration->xamlThreadId !=
                currentThreadId) {
            continue;
        }

        FrameworkElement presenter{nullptr};
        try {
            presenter =
                registration->presenter.get();
        } catch (...) {
            continue;
        }
        if (!presenter) {
            continue;
        }

        ++stats.presenters;

        void* userVm = nullptr;
        if (!TryGetUserViewModelFromDataContext(
                presenter,
                &userVm)) {
            continue;
        }

        HRESULT qualifiedNameHr = E_FAIL;
        const std::wstring qualifiedName =
            ReadStringProperty(
                g_getQualifiedName,
                userVm,
                &qualifiedNameHr);
        reinterpret_cast<IUnknown*>(
            userVm)->Release();

        if (FAILED(qualifiedNameHr) ||
            qualifiedName.empty()) {
            continue;
        }

        KnownSlotRef* newest = nullptr;
        for (auto& candidate : known) {
            if (!candidate.slot ||
                _wcsicmp(
                    candidate.slot->lastUser,
                    qualifiedName.c_str()) != 0) {
                continue;
            }

            if (!newest ||
                candidate.slot->serial >
                    newest->slot->serial) {
                newest = &candidate;
            }
        }

        if (!newest || !newest->slot) {
            continue;
        }

        newest->matched = true;
        ++stats.matchedRows;

        const std::wstring text =
            newest->slot->text;
        const auto applied =
            ApplyFastTransientTextToOwnedPresenter(
                presenter,
                text);
        stats.ownedBranches +=
            applied.branches;
        stats.changedBranches +=
            applied.changed;

        if (applied.branches == 0) {
            stats.needsFullRefresh = true;
        }
    }

    for (const auto& candidate : known) {
        if (candidate.slot &&
            !candidate.matched) {
            stats.needsFullRefresh = true;
        }
    }

    if (stats.pendingSlots != 0 &&
        !stats.pendingRowVisible) {
        stats.needsFullRefresh = true;
    }

    if (latestIsClear) {
        stats.needsFullRefresh = true;
    }

    return stats;
}

bool RefreshNativeTransientFastOnUiThread() {
    if (g_unloading.load(
            std::memory_order_acquire)) {
        return false;
    }

    try {
        auto window = Window::Current();
        if (!window) {
            return false;
        }

        auto root =
            window.Content()
                .try_as<FrameworkElement>();
        if (!root) {
            return false;
        }

        RememberXamlThread(
            GetCurrentThreadId(),
            false);
        EnsureTransientRenderProbeOnCurrentThread();

        const auto snapshot =
            CaptureTransientBridgeSnapshot();
        const auto stats =
            ApplyFastNativeTransientStatus(
                root,
                snapshot);

        g_transientFastNeedsFullRefresh.store(
            stats.needsFullRefresh,
            std::memory_order_release);

        g_renderConsoleTick.store(
            GetTickCount64(), std::memory_order_relaxed);
        g_renderConsoleSerial.store(
            snapshot.serial, std::memory_order_release);

        // Log only after the visible fast-path write, and only in layout
        // diagnostics mode, so the next test can distinguish bridge delivery
        // latency from XAML/full-refresh contention without putting file I/O
        // in front of the UI update.
        if (g_enableLayoutDiagnostics.load(
                std::memory_order_relaxed) &&
            (stats.changedBranches != 0 ||
             stats.pendingSlots != 0 ||
             stats.needsFullRefresh)) {
            ProbeLog(
                L"TRANSIENT_FAST_APPLY serial=%llu presenters=%u matchedRows=%u ownedBranches=%u changedBranches=%u pending=%u footer=%u needsFull=%u",
                static_cast<unsigned long long>(
                    snapshot.serial),
                stats.presenters,
                stats.matchedRows,
                stats.ownedBranches,
                stats.changedBranches,
                stats.pendingSlots,
                stats.pendingRowVisible ? 1u : 0u,
                stats.needsFullRefresh ? 1u : 0u);
        }

        return true;
    } catch (...) {
        return false;
    }
}


// alpha5b: deferred correlation samples use the already-existing XAML owner
// thread message-only HWND; no polling, new thread, private detour, or row
// mutation. WTS sources in this probe are live measurements ONLY, never the
// legacy AuthUI-style WTS snapshot used to render the actual user list.
void QueueVmCorrelationSample(unsigned sourceMask,
                              DWORD sessionId,
                              unsigned wtsEvent) {
    if (!g_vmStateCorrelationEnabled.load(std::memory_order_acquire) ||
        g_unloading.load(std::memory_order_acquire) ||
        !g_abiReady.load(std::memory_order_acquire)) return;
    g_vmCorrelationSourceMask.fetch_or(sourceMask, std::memory_order_acq_rel);
    g_vmCorrelationRequests.fetch_add(1, std::memory_order_acq_rel);
    if (sourceMask & 2) {
        g_vmCorrelationLastSession.store(sessionId, std::memory_order_release);
        g_vmCorrelationLastWtsEvent.store(wtsEvent, std::memory_order_release);
    }
    bool expected = false;
    if (!g_vmCorrelationQueued.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) return;
    HWND hwnd = g_transientFastUiBridgeWindow.load(std::memory_order_acquire);
    if (!hwnd || !IsWindow(hwnd) ||
        (g_preferredXamlThreadId.load(std::memory_order_acquire) != 0 &&
         GetWindowThreadProcessId(hwnd, nullptr) !=
            g_preferredXamlThreadId.load(std::memory_order_acquire)) ||
        !PostMessageW(hwnd, kVmCorrelationUiMessage, 0, 0)) {
        g_vmCorrelationQueued.store(false, std::memory_order_release);
        // Not yet realized. The next attach or session event can try again.
    }
}

void CaptureVmCorrelationOnUiThread() {
    g_vmCorrelationQueued.store(false, std::memory_order_release);
    if (!g_vmStateCorrelationEnabled.load(std::memory_order_acquire) ||
        g_unloading.load(std::memory_order_acquire) ||
        !g_abiReady.load(std::memory_order_acquire) ||
        !g_haveVmChainIids) return;
    const unsigned reasons = g_vmCorrelationSourceMask.exchange(
        0, std::memory_order_acq_rel);
    const unsigned requests = g_vmCorrelationRequests.exchange(
        0, std::memory_order_acq_rel);
    const DWORD wtsSession = (reasons & 2)
        ? g_vmCorrelationLastSession.load(std::memory_order_acquire)
        : 0xFFFFFFFF;
    const unsigned wtsEvent = (reasons & 2)
        ? g_vmCorrelationLastWtsEvent.load(std::memory_order_acquire)
        : 0;

    // Every native getter below executes only on the VM's owning XAML thread,
    // after the PropertyChanged callback has returned. If ownership moved,
    // skip that VM instead of crossing apartment boundaries.
    std::vector<std::shared_ptr<VmPropertyProbeRegistration>> regs;
    const DWORD tid = GetCurrentThreadId();
    AcquireSRWLockShared(&g_vmPropertyProbeLock);
    for (const auto& reg : g_vmPropertyProbeRegistrations) {
        if (reg && reg->active.load(std::memory_order_acquire) &&
            reg->xamlThreadId == tid) regs.push_back(reg);
    }
    ReleaseSRWLockShared(&g_vmPropertyProbeLock);
    if (regs.empty()) return;

    const auto liveSessions = CaptureWtsSessions(false);
    unsigned changed = 0, unchanged = 0, failed = 0;
    for (const auto& reg : regs) {
        if (!reg->active.load(std::memory_order_acquire)) continue;
        auto notify = reg->model.get();
        if (!notify) { ++failed; continue; }
        void* vm = nullptr;
        auto* raw = reinterpret_cast<IUnknown*>(winrt::get_abi(notify));
        if (!raw || FAILED(raw->QueryInterface(g_userViewModelPublicIid, &vm)) ||
            !vm) { ++failed; continue; }
        HRESULT loginHr = E_FAIL, visibleHr = E_FAIL,
                domainHr = E_FAIL, statusHr = E_FAIL;
        const int loggedIn = ReadBoolProperty(g_getIsLoggedIn, vm, &loginHr);
        const int visible = ReadBoolProperty(
            g_getIsLogonStatusVisible, vm, &visibleHr);
        const int domainVisible = ReadBoolProperty(
            g_getIsDomainNameVisible, vm, &domainHr);
        std::wstring status = ReadStringProperty(g_getLogonStatus, vm, &statusHr);
        reinterpret_cast<IUnknown*>(vm)->Release();
        vm = nullptr;
        if (status.size() > 160) status.resize(160);
        for (auto& ch : status) {
            if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
        }
        const auto* session = FindWtsSessionForQualifiedName(
            reg->qualifiedName, liveSessions);
        const DWORD sessionId = session ? session->sessionId : 0xFFFFFFFF;
        const int state = session ? static_cast<int>(session->state) : -1;
        const int lockState = session ? static_cast<int>(session->lockState) : -1;
        const USHORT protocol = session ? session->protocolType : 0xFFFF;
        const bool baseline = !reg->correlationObserved;
        const bool vmChanged = baseline ||
            reg->correlationLoggedIn != loggedIn ||
            reg->correlationVisible != visible ||
            reg->correlationDomainVisible != domainVisible ||
            reg->correlationStatus != status;
        const bool wtsChanged = baseline ||
            reg->correlationSessionId != sessionId ||
            reg->correlationWtsState != state ||
            reg->correlationLockState != lockState ||
            reg->correlationProtocol != protocol;
        if (vmChanged || wtsChanged) {
            ++changed;
            ProbeLog(
                L"VM_STATE_CORRELATION row account=\"%s\" baseline=%u vmChanged=%u wtsChanged=%u "
                L"loggedIn=%d loginHr=0x%08X visible=%d visibleHr=0x%08X "
                L"domainVisible=%d domainHr=0x%08X status=\"%s\" statusHr=0x%08X "
                L"liveWtsSession=%lu liveWtsState=%d liveWtsLock=%d liveProtocol=%u tid=%lu",
                reg->qualifiedName.c_str(), baseline ? 1u : 0u,
                vmChanged ? 1u : 0u, wtsChanged ? 1u : 0u,
                loggedIn, static_cast<unsigned>(loginHr),
                visible, static_cast<unsigned>(visibleHr),
                domainVisible, static_cast<unsigned>(domainHr),
                status.c_str(), static_cast<unsigned>(statusHr),
                sessionId, state, lockState, static_cast<unsigned>(protocol), tid);
        } else {
            ++unchanged;
        }
        reg->correlationObserved = true;
        reg->correlationLoggedIn = loggedIn;
        reg->correlationVisible = visible;
        reg->correlationDomainVisible = domainVisible;
        reg->correlationStatus = std::move(status);
        reg->correlationSessionId = sessionId;
        reg->correlationWtsState = state;
        reg->correlationLockState = lockState;
        reg->correlationProtocol = protocol;
    }
    ProbeLog(L"VM_STATE_CORRELATION batch sourceMask=0x%X requests=%u "
             L"wtsEvent=%u wtsSession=%lu samples=%u changed=%u "
             L"unchanged=%u failed=%u ownerTid=%lu",
             reasons, requests, wtsEvent, wtsSession,
             static_cast<unsigned>(regs.size()), changed, unchanged, failed, tid);
}

LRESULT CALLBACK TransientFastUiBridgeWndProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam) {

    if (message == kVmCorrelationUiMessage) {
        CaptureVmCorrelationOnUiThread();
        return 0;
    }

    if (message == kTransientFastUiMessage) {
        const bool matched =
            RefreshNativeTransientFastOnUiThread();
        const bool needsFull =
            !matched ||
            g_transientFastNeedsFullRefresh.exchange(
                false,
                std::memory_order_acq_rel);

        if (needsFull &&
            g_refreshWakeEvent &&
            !g_unloading.load(
                std::memory_order_acquire)) {
            SetEvent(g_refreshWakeEvent);
        }
        return 0;
    }

    if (message == WM_NCDESTROY) {
        HWND expected = hwnd;
        g_transientFastUiBridgeWindow.compare_exchange_strong(
            expected,
            nullptr,
            std::memory_order_acq_rel);
    }

    return DefWindowProcW(
        hwnd,
        message,
        wParam,
        lParam);
}

HWND EnsureTransientFastUiBridgeWindow() {
    if (HWND existing =
            g_transientFastUiBridgeWindow.load(
                std::memory_order_acquire)) {
        if (IsWindow(existing)) {
            return existing;
        }
        g_transientFastUiBridgeWindow.store(
            nullptr,
            std::memory_order_release);
    }

    if (!EnsurePrivateProcessWindowClass(
            kTransientFastUiBridgeClass,
            TransientFastUiBridgeWndProc)) {
        ProbeLog(
            L"TRANSIENT_FAST UI bridge class unavailable");
        return nullptr;
    }

    HWND hwnd =
        CreateWindowExW(
            0,
            kTransientFastUiBridgeClass,
            L"",
            0,
            0, 0, 0, 0,
            HWND_MESSAGE,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);

    if (!hwnd) {
        ProbeLog(
            L"TRANSIENT_FAST UI bridge CreateWindowEx failed gle=%lu",
            GetLastError());
        return nullptr;
    }

    g_transientFastUiBridgeWindow.store(
        hwnd,
        std::memory_order_release);
    ProbeLog(
        L"TRANSIENT_FAST UI bridge created hwnd=%p thread=%lu",
        hwnd,
        GetCurrentThreadId());
    return hwnd;
}

bool QueueTransientFastUiUpdate() {
    HWND hwnd =
        g_transientFastUiBridgeWindow.load(
            std::memory_order_acquire);

    if (!hwnd ||
        !IsWindow(hwnd)) {
        return false;
    }

    const DWORD bridgeThread =
        GetWindowThreadProcessId(
            hwnd,
            nullptr);
    const DWORD preferredThread =
        g_preferredXamlThreadId.load(
            std::memory_order_acquire);

    // If LogonUI moved SystemProvider/user-list ownership to another XAML
    // thread, don't post native status to an obsolete Window::Current root.
    // The worker will fall back to the preferred-thread rendezvous instead.
    if (preferredThread != 0 &&
        bridgeThread != preferredThread) {
        return false;
    }

    return PostMessageW(
               hwnd,
               kTransientFastUiMessage,
               0,
               0) != FALSE;
}

void DestroyTransientFastUiBridgeWindowOnCurrentThread() {
    HWND hwnd =
        g_transientFastUiBridgeWindow.load(
            std::memory_order_acquire);
    if (!hwnd) {
        return;
    }

    const DWORD threadId =
        GetWindowThreadProcessId(
            hwnd,
            nullptr);

    if (threadId !=
        GetCurrentThreadId()) {
        return;
    }

    HWND expected = hwnd;
    if (g_transientFastUiBridgeWindow.compare_exchange_strong(
            expected,
            nullptr,
            std::memory_order_acq_rel)) {
        DestroyWindow(hwnd);
        ProbeLog(
            L"TRANSIENT_FAST UI bridge destroyed thread=%lu",
            GetCurrentThreadId());
    }
}

// Compare exact COM object identities, not account names or row counts.
// Never infer deduplication safety from this probe alone: separate roots can
// still have root-specific non-row effects or separate ownership lifetimes.
void ReportRowIdentityCapture(const FrameworkElement& root,
                              const RestoreStats& stats,
                              RowIdentityCapture&& capture) {
    const bool logIdentity = g_rowIdentityProbeEnabled.load(
        std::memory_order_relaxed);
    if ((!logIdentity && !g_identityGuardedRowReuse.load(
            std::memory_order_relaxed)) ||
        !root || g_unloading.load(std::memory_order_relaxed)) return;
    try {
        std::sort(capture.presenters.begin(), capture.presenters.end());
        capture.presenters.erase(std::unique(capture.presenters.begin(),
            capture.presenters.end()), capture.presenters.end());

        RowIdentitySnapshot snapshot;
        snapshot.threadId = GetCurrentThreadId();
        snapshot.generation = g_layoutRepairGeneration.load(
            std::memory_order_acquire);
        snapshot.rootIdentity = reinterpret_cast<void*>(winrt::get_abi(root));
        snapshot.presenterCount = stats.presentersSeen;
        snapshot.userRowCount = stats.userRows;
        snapshot.queryFailures = capture.queryFailures;
        snapshot.truncated = capture.truncated;
        snapshot.presenters = std::move(capture.presenters);

        std::vector<RowIdentitySnapshot> peers;
        bool changed = false;
        // RAII protects the lock if a vector allocation throws. These are
        // diagnostics only, so exceptions must never deadlock LogonUI.
        {
        struct SnapshotLock {
            SnapshotLock() { AcquireSRWLockExclusive(&g_rowIdentitySnapshotsLock); }
            ~SnapshotLock() { ReleaseSRWLockExclusive(&g_rowIdentitySnapshotsLock); }
        } snapshotLock;
        // No stale-generation comparisons, including during RDP WTS churn.
        g_rowIdentitySnapshots.erase(std::remove_if(
            g_rowIdentitySnapshots.begin(), g_rowIdentitySnapshots.end(),
            [&](const RowIdentitySnapshot& old) {
                return old.generation != snapshot.generation;
            }), g_rowIdentitySnapshots.end());
        auto own = std::find_if(g_rowIdentitySnapshots.begin(),
            g_rowIdentitySnapshots.end(), [&](const RowIdentitySnapshot& old) {
                return old.threadId == snapshot.threadId &&
                       old.rootIdentity == snapshot.rootIdentity;
            });
        if (own == g_rowIdentitySnapshots.end()) {
            changed = true;
        } else if (own->presenters != snapshot.presenters ||
                   own->queryFailures != snapshot.queryFailures ||
                   own->truncated != snapshot.truncated ||
                   own->presenterCount != snapshot.presenterCount ||
                   own->userRowCount != snapshot.userRowCount) {
            changed = true;
        }
        if (changed) {
            for (const auto& old : g_rowIdentitySnapshots) {
                if (old.threadId == snapshot.threadId &&
                    old.rootIdentity != snapshot.rootIdentity) peers.push_back(old);
            }
            if (own == g_rowIdentitySnapshots.end()) {
                if (g_rowIdentitySnapshots.size() >= 12)
                    g_rowIdentitySnapshots.erase(g_rowIdentitySnapshots.begin());
                g_rowIdentitySnapshots.push_back(snapshot);
            } else {
                *own = snapshot;
            }
        }
        }
        if (!changed || !logIdentity) return;
        // Identity cache updates still happen without diagnostic logging;
        // no strings are formatted when the structural probe is off.
        std::wstring identities;
        for (const auto id : snapshot.presenters) {
            wchar_t buffer[40]{};
            swprintf_s(buffer, L"%p", reinterpret_cast<void*>(id));
            if (!identities.empty()) identities += L",";
            identities += buffer;
        }
        const bool complete = snapshot.queryFailures == 0 &&
            snapshot.truncated == 0 && snapshot.presenterCount != 0 &&
            snapshot.presenterCount == snapshot.presenters.size();
        ProbeLog(L"ROW_IDENTITY root=%p generation=%u tid=%lu presenters=%u "
                 L"userRows=%u unique=%u complete=%u qiFailures=%u capped=%u ids=[%s]",
            snapshot.rootIdentity, snapshot.generation, snapshot.threadId,
            snapshot.presenterCount, snapshot.userRowCount,
            static_cast<unsigned>(snapshot.presenters.size()),
            complete ? 1u : 0u, snapshot.queryFailures, snapshot.truncated,
            identities.c_str());

        for (const auto& peer : peers) {
            unsigned common = 0;
            size_t a = 0, b = 0;
            while (a < snapshot.presenters.size() &&
                   b < peer.presenters.size()) {
                if (snapshot.presenters[a] == peer.presenters[b]) {
                    ++common; ++a; ++b;
                } else if (snapshot.presenters[a] < peer.presenters[b]) {
                    ++a;
                } else {
                    ++b;
                }
            }
            const bool peerComplete = peer.queryFailures == 0 &&
                peer.truncated == 0 && peer.presenterCount != 0 &&
                peer.presenterCount == peer.presenters.size();
            const wchar_t* relation = !complete || !peerComplete
                ? L"incomplete" : snapshot.presenters == peer.presenters
                    ? L"exact" : common ? L"partial" : L"disjoint";
            ProbeLog(L"ROW_IDENTITY_COMPARE generation=%u tid=%lu rootA=%p "
                     L"rootB=%p common=%u onlyA=%u onlyB=%u relation=%s",
                snapshot.generation, snapshot.threadId,
                snapshot.rootIdentity, peer.rootIdentity, common,
                static_cast<unsigned>(snapshot.presenters.size()) - common,
                static_cast<unsigned>(peer.presenters.size()) - common,
                relation);
        }
    } catch (...) {
        // Purely diagnostic work must never prevent native LogonUI repair.
        ProbeLog(L"ROW_IDENTITY unavailable: diagnostic capture exception");
    }
}

// Fail-closed: a complete, previously observed exact presenter set is a
// *prerequisite*, never a substitute for the next real tree traversal.
bool HasExactIdentityPeer(DWORD tid, unsigned generation,
                          void* rootIdentity, void* peerRoot) {
    if (!rootIdentity || !peerRoot || rootIdentity == peerRoot) return false;
    AcquireSRWLockShared(&g_rowIdentitySnapshotsLock);
    const RowIdentitySnapshot* mine = nullptr;
    const RowIdentitySnapshot* other = nullptr;
    for (const auto& row : g_rowIdentitySnapshots) {
        if (row.threadId != tid || row.generation != generation) continue;
        if (row.rootIdentity == rootIdentity) mine = &row;
        if (row.rootIdentity == peerRoot) other = &row;
    }
    const auto complete = [](const RowIdentitySnapshot* row) {
        return row && row->presenterCount > 0 &&
            row->queryFailures == 0 && row->truncated == 0 &&
            row->presenterCount == row->userRowCount &&
            row->presenterCount == row->presenters.size();
    };
    const bool exact = complete(mine) && complete(other) &&
        mine->presenters == other->presenters;
    ReleaseSRWLockShared(&g_rowIdentitySnapshotsLock);
    return exact;
}

struct PresenterReuseWalk {
    bool enabled = false;
    bool canReuse = false;
    DWORD tid = 0;
    unsigned generation = 0;
    unsigned settingsGeneration = 0;
    void* rootIdentity = nullptr;
    unsigned skipped = 0;
    unsigned checked = 0;
    std::shared_ptr<const RecentCleanPresenterPass> previous;
    std::vector<PresenterCleanResult> fullResults;
};

// WTS notifications may advance this generation on the worker thread while
// the owning XAML thread is walking a row. A snapshot taken at walk entry is
// NOT sufficient: fail closed immediately before consuming each clean entry.
// Only acquire atomics here; this hot path must not wait or create extra work.
bool IsPresenterReuseGenerationCurrent(const PresenterReuseWalk& reuse) {
    return reuse.enabled &&
        !g_unloading.load(std::memory_order_acquire) &&
        g_enabled.load(std::memory_order_acquire) &&
        g_identityGuardedRowReuse.load(std::memory_order_acquire) &&
        reuse.generation == g_layoutRepairGeneration.load(
            std::memory_order_acquire) &&
        reuse.settingsGeneration == g_settingsGeneration.load(
            std::memory_order_acquire);
}

void AddCleanPresenterCounts(RestoreStats& stats,
                             const PresenterCleanResult& entry) {
    stats.presentersSeen += entry.presentersSeen;
    stats.userRows += entry.userRows;
    stats.completedRows += entry.completedRows;
    stats.remoteRows += entry.remoteRows;
    stats.statusRows += entry.statusRows;
    stats.qualifiedNameRows += entry.qualifiedNameRows;
    stats.userTileRows += entry.userTileRows;
    stats.userTileMisses += entry.userTileMisses;
}

void ProcessPresenterWithOptionalReuse(
    const FrameworkElement& presenter,
    const std::vector<WtsSessionSnapshot>& sessions,
    RestoreStats& stats, ULONG_PTR presenterId,
    PresenterReuseWalk* reuse) {
    if (!reuse || !reuse->enabled) {
        TryRestoreRemoteStatusInPresenter(presenter, sessions, stats);
        return;
    }
    // Always re-read the current model association before any reuse. The
    // DataContextChanged handler also increments the global repair generation.
    ULONG_PTR vm = 0;
    if (presenterId && presenter &&
        presenter.Visibility() == Visibility::Visible &&
        presenter.ActualWidth() > 0 && presenter.ActualHeight() > 0) {
        void* raw = nullptr;
        if (TryGetUserViewModelFromDataContext(presenter, &raw) && raw) {
            vm = reinterpret_cast<ULONG_PTR>(raw);
            reinterpret_cast<IUnknown*>(raw)->Release();
        }
    }
    const double width = presenter.ActualWidth();
    const double height = presenter.ActualHeight();
    if (reuse->canReuse && vm && reuse->previous &&
        IsPresenterReuseGenerationCurrent(*reuse)) {
        auto it = std::find_if(reuse->previous->entries.begin(),
            reuse->previous->entries.end(),
            [&](const PresenterCleanResult& item) {
                return item.presenterIdentity == presenterId;
            });
        if (it != reuse->previous->entries.end() &&
            it->userVmInterface == vm &&
            it->width == width && it->height == height &&
            IsPresenterReuseGenerationCurrent(*reuse)) {
            AddCleanPresenterCounts(stats, *it);
            ++reuse->skipped;
            return;
        }
    }
    RestoreStats one{};
    TryRestoreRemoteStatusInPresenter(presenter, sessions, one);
    stats.presentersSeen += one.presentersSeen;
    stats.userRows += one.userRows;
    stats.completedRows += one.completedRows;
    stats.remoteRows += one.remoteRows;
    stats.statusRows += one.statusRows;
    stats.qualifiedNameRows += one.qualifiedNameRows;
    stats.userTileRows += one.userTileRows;
    stats.userTileMisses += one.userTileMisses;
    stats.patchedRows += one.patchedRows;
    stats.signatureMisses += one.signatureMisses;
    ++reuse->checked;
    if (presenterId && vm && one.userRows == 1 &&
        one.completedRows == 1 && one.presentersSeen == 1 &&
        one.patchedRows == 0 &&
        one.signatureMisses == 0 && reuse->fullResults.size() < 64) {
        PresenterCleanResult item{};
        item.presenterIdentity = presenterId;
        item.userVmInterface = vm;
        item.width = width;
        item.height = height;
        item.presentersSeen = one.presentersSeen;
        item.userRows = one.userRows;
        item.completedRows = one.completedRows;
        item.remoteRows = one.remoteRows;
        item.statusRows = one.statusRows;
        item.qualifiedNameRows = one.qualifiedNameRows;
        item.userTileRows = one.userTileRows;
        item.userTileMisses = one.userTileMisses;
        try {
            reuse->fullResults.push_back(item);
        } catch (...) {
            // Fail closed: the actual row repair has already completed.
        }
    }
}

void RestoreRemoteStatusRowsRecursive(
    const FrameworkElement& element,
    const std::vector<WtsSessionSnapshot>& sessions,
    RestoreStats& stats,
    int depth,
    int& budget,
    RowIdentityCapture* identityCapture,
    PresenterReuseWalk* reuse) {

    if (!element ||
        depth > 64 ||
        budget <= 0) {
        return;
    }

    --budget;

    try {
        const std::wstring className =
            SafeClassName(element);

        if (className.find(
                L"Windows.UI.Xaml.Controls.Primitives.ListViewItemPresenter") !=
            std::wstring::npos) {

            ULONG_PTR presenterId = 0;
            if (identityCapture) {
                // A diagnostic allocation/QI failure cannot suppress the
                // actual native row-repair path or truncate recursion.
                try {
                    if (identityCapture->presenters.size() < 64) {
                        IUnknown* identity = nullptr;
                        auto raw = reinterpret_cast<IUnknown*>(winrt::get_abi(element));
                        // Use the canonical COM IUnknown IID value by-value.
                        // Windhawk's Clang/lld does not automatically link the
                        // external SDK symbol IID_IUnknown (typically uuid.lib).
                        // An inline literal avoids any new linker dependency.
                        constexpr GUID kComIUnknownIid = {
                            0x00000000, 0x0000, 0x0000,
                            {0xC0, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0x00, 0x46}};
                        if (raw && SUCCEEDED(raw->QueryInterface(kComIUnknownIid,
                            reinterpret_cast<void**>(&identity))) && identity) {
                            presenterId = reinterpret_cast<ULONG_PTR>(identity);
                            identity->Release();
                            identityCapture->presenters.push_back(presenterId);
                        } else {
                            if (identity) identity->Release();
                            ++identityCapture->queryFailures;
                        }
                    } else {
                        ++identityCapture->truncated;
                    }
                } catch (...) {
                    ++identityCapture->queryFailures;
                }
            }
            ProcessPresenterWithOptionalReuse(
                element, sessions, stats, presenterId, reuse);
        }

        const int count =
            Media::VisualTreeHelper::GetChildrenCount(
                element);

        for (int i = 0;
             i < count &&
             budget > 0;
             ++i) {

            auto child =
                Media::VisualTreeHelper::GetChild(
                    element,
                    i)
                    .try_as<FrameworkElement>();

            if (child) {
                RestoreRemoteStatusRowsRecursive(
                    child,
                    sessions,
                    stats,
                    depth + 1,
                    budget,
                    identityCapture,
                    reuse);
            }
        }
    } catch (const winrt::hresult_error& e) {
        ProbeLog(
            L"RESTORE tree exception depth=%d hr=0x%08X",
            depth,
            static_cast<unsigned>(
                e.code().value));
    } catch (...) {
        ProbeLog(
            L"RESTORE tree exception depth=%d",
            depth);
    }
}

RestoreStats RefreshRemoteStatusRows(
    const FrameworkElement& root) {

    RestoreStats stats{};
    const bool sampleCost = g_vmStateCorrelationEnabled.load(
        std::memory_order_acquire);
    LARGE_INTEGER correlationStart{};
    if (sampleCost) QueryPerformanceCounter(&correlationStart);

    if (!root ||
        g_unloading.load(
            std::memory_order_acquire)) {
        return stats;
    }

    // Structural discovery always executes for each XAML root. This cache
    // only avoids repeating a clean row's expensive getter/text work.
    const bool reuseEnabled =
        g_identityGuardedRowReuse.load(std::memory_order_acquire) &&
        !g_liveSessionRowUpdates.load(std::memory_order_acquire) &&
        !g_showNativeTransientStatus.load(std::memory_order_acquire) &&
        !g_showPendingRemoteAttempt.load(std::memory_order_acquire) &&
        !g_customRowLayoutEnabled.load(std::memory_order_acquire) &&
        !g_vmPropertyProbeEnabled.load(std::memory_order_acquire) &&
        !g_vmStateCorrelationEnabled.load(std::memory_order_acquire) &&
        !g_compactUnsignedRows.load(std::memory_order_acquire);
    PresenterReuseWalk reuse;
    reuse.enabled = reuseEnabled;
    if (reuseEnabled) {
        reuse.tid = GetCurrentThreadId();
        reuse.generation = g_layoutRepairGeneration.load(
            std::memory_order_acquire);
        reuse.settingsGeneration = g_settingsGeneration.load(
            std::memory_order_acquire);
        reuse.rootIdentity = reinterpret_cast<void*>(winrt::get_abi(root));
        reuse.previous = std::atomic_load_explicit(
            &g_recentCleanPresenterPass, std::memory_order_acquire);
        const ULONGLONG now = GetTickCount64();
        reuse.canReuse = reuse.previous &&
            reuse.previous->threadId == reuse.tid &&
            reuse.previous->generation == reuse.generation &&
            reuse.previous->settingsGeneration == reuse.settingsGeneration &&
            reuse.previous->rootIdentity != reuse.rootIdentity &&
            now >= reuse.previous->capturedTick &&
            now - reuse.previous->capturedTick <= 50 &&
            HasExactIdentityPeer(reuse.tid, reuse.generation,
                reuse.rootIdentity, reuse.previous->rootIdentity);
    }
    RowIdentityCapture identityCapture;
    RowIdentityCapture* identityCapturePtr =
        (g_rowIdentityProbeEnabled.load(std::memory_order_relaxed) ||
         reuseEnabled) ? &identityCapture : nullptr;
    auto sessions =
        CaptureWtsSessionsForRows();
    DWORD initialConsoleSession =
        WTSGetActiveConsoleSessionId();
    int budget = 2500;

    RestoreRemoteStatusRowsRecursive(
        root,
        sessions,
        stats,
        0,
        budget,
        identityCapturePtr,
        reuseEnabled ? &reuse : nullptr);

    // A WTS event can race *after* one or more rows were reused. Even though
    // every row has a pre-reuse generation check, replay the complete pass
    // once with freshly captured WTS data when the walk was superseded. This
    // prevents partially reused stats and keeps the session snapshot coherent.
    // The replay uses the original repair code, not the reuse cache. During
    // sustained event churn, the retained lifecycle watcher handles later
    // generations; there is deliberately no retry loop or polling timer.
    if (reuseEnabled && reuse.skipped &&
        !IsPresenterReuseGenerationCurrent(reuse)) {
        if (g_unloading.load(std::memory_order_acquire)) {
            return RestoreStats{}; // Never start new XAML work during unload.
        }
        const unsigned staleGeneration = reuse.generation;
        sessions = CaptureWtsSessionsForRows();
        initialConsoleSession = WTSGetActiveConsoleSessionId();
        stats = RestoreStats{};
        budget = 2500;
        RestoreRemoteStatusRowsRecursive(root, sessions, stats, 0, budget,
                                         nullptr, nullptr);
        if (g_unloading.load(std::memory_order_acquire)) {
            return RestoreStats{};
        }
        // The earlier identity observation belongs to the superseded pass.
        // Do not publish it as if it represented the replay's row set.
        identityCapturePtr = nullptr;
        reuse.skipped = 0;
        reuse.canReuse = false;
        ProbeLog(L"ROW_REUSE fallback staleGeneration=%u currentGeneration=%u "
                 L"settingsGeneration=%u currentSettingsGeneration=%u "
                 L"recheckedRows=%u",
            staleGeneration,
            g_layoutRepairGeneration.load(std::memory_order_acquire),
            reuse.settingsGeneration,
            g_settingsGeneration.load(std::memory_order_acquire),
            stats.userRows);
    }
    if (reuseEnabled) {
        // Do not perpetuate skipped results; only a wholly fresh, clean pass
        // can serve as the next source. Incomplete identity sets fail closed.
        if (!reuse.skipped && stats.userRows > 0 &&
            stats.patchedRows == 0 && stats.signatureMisses == 0 &&
            stats.userRows == stats.presentersSeen &&
            stats.completedRows == stats.userRows &&
            identityCapture.queryFailures == 0 &&
            identityCapture.truncated == 0 &&
            identityCapture.presenters.size() == stats.presentersSeen &&
            reuse.fullResults.size() == stats.userRows &&
            reuse.generation == g_layoutRepairGeneration.load(
                std::memory_order_acquire) &&
            reuse.settingsGeneration == g_settingsGeneration.load(
                std::memory_order_acquire)) {
            try {
                auto fresh = std::make_shared<RecentCleanPresenterPass>();
                fresh->threadId = reuse.tid;
                fresh->generation = reuse.generation;
                fresh->settingsGeneration = reuse.settingsGeneration;
                fresh->rootIdentity = reuse.rootIdentity;
                fresh->capturedTick = GetTickCount64();
                fresh->entries = std::move(reuse.fullResults);
                std::shared_ptr<const RecentCleanPresenterPass> immutable = fresh;
                std::atomic_store_explicit(&g_recentCleanPresenterPass,
                    std::move(immutable), std::memory_order_release);
            } catch (...) {
                // A cache allocation failure never interrupts normal repair.
            }
        }
        if (reuse.skipped) {
            ProbeLog(L"ROW_REUSE generation=%u root=%p reused=%u checked=%u userRows=%u",
                reuse.generation, reuse.rootIdentity,
                reuse.skipped, reuse.checked, stats.userRows);
        }
    }
    if (identityCapturePtr) ReportRowIdentityCapture(
        root, stats, std::move(identityCapture));

    if (g_showNativeTransientStatus.load(
            std::memory_order_acquire)) {
        const auto transientSnapshot =
            CaptureTransientBridgeSnapshot();
        unsigned pendingCount = 0;
        UpdatePendingRemoteAttemptRow(
            root,
            transientSnapshot,
            &pendingCount);
    }

    if (stats.patchedRows != 0 ||
        stats.signatureMisses != 0 ||
        g_enableLayoutDiagnostics.load(
            std::memory_order_relaxed)) {

        ProbeLog(
            L"REFRESH_END presenters=%u userRows=%u remoteRows=%u "
            L"statusRows=%u qualifiedNameRows=%u userTileRows=%u "
            L"userTileMisses=%u changedRows=%u signatureMisses=%u "
            L"remainingBudget=%d",
            stats.presentersSeen,
            stats.userRows,
            stats.remoteRows,
            stats.statusRows,
            stats.qualifiedNameRows,
            stats.userTileRows,
            stats.userTileMisses,
            stats.patchedRows,
            stats.signatureMisses,
            budget);
    }

    if (stats.userRows != 0) {
        CommitFirstRowSessionSnapshot(
            sessions,
            stats.userRows,
            initialConsoleSession);
        g_userListReady.store(
            true,
            std::memory_order_release);
    }

    if (sampleCost && stats.userRows > 0) {
        LARGE_INTEGER end{}, frequency{};
        if (QueryPerformanceCounter(&end) &&
            QueryPerformanceFrequency(&frequency) &&
            frequency.QuadPart > 0) {
            const auto elapsedUs = static_cast<unsigned long long>(
                (end.QuadPart - correlationStart.QuadPart) * 1000000LL /
                frequency.QuadPart);
            ProbeLog(L"VM_STATE_CORRELATION repairWalk us=%llu rows=%u "
                     L"changed=%u signatureMisses=%u",
                elapsedUs, stats.userRows, stats.patchedRows,
                stats.signatureMisses);
        }
    }
    return stats;
}

void TryDumpLayoutOnce(
    const FrameworkElement& anchor) {

    if (!anchor) {
        return;
    }

    auto root =
        FindVisualRoot(anchor);

    if (!root) {
        return;
    }

    if (g_enableLayoutDiagnostics.load(
            std::memory_order_relaxed) &&
        g_userListReady.load(
            std::memory_order_acquire)) {

        bool expected = false;

        if (g_layoutDumped.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel)) {

            ProbeLog(
                L"LAYOUT_DUMP_BEGIN");

            ProbeLog(
                L"LAYOUT_DUMP rootClass=\"%s\" rootName=\"%s\"",
                SafeClassName(root).c_str(),
                ElementName(root).c_str());

            int scanBudget = 2500;
            FindAndDumpLayoutTargets(
                root,
                0,
                scanBudget);

            ProbeLog(
                L"LAYOUT_DUMP_END remainingScanBudget=%d",
                scanBudget);
        }
    }

    // Operational restoration is intentionally not one-shot. Re-running this
    // is how v0.18 follows sign-in/sign-out and RDP reconnect transitions.
    RefreshRemoteStatusRows(
        root);
}



bool IsNativeTransientStatusOwnerClass(
    const std::wstring& className) {
    return className == L"LogonUX.DisplayMessageUserControl" ||
           className == L"LogonUX.CredentialDisplayMessageUserControl";
}

bool IsNativeTransientStatusTextBlock(
    const Controls::TextBlock& textBlock,
    std::wstring* ownerClass) {

    if (!textBlock ||
        ElementName(textBlock) != L"StatusMessage") {
        return false;
    }

    try {
        auto dataContext = textBlock.DataContext();
        if (!dataContext ||
            SafeClassName(dataContext) !=
                L"LogonUX.DisplayMessageViewModel") {
            return false;
        }

        DependencyObject current = textBlock;

        for (int depth = 0;
             current && depth < 16;
             ++depth) {

            if (auto element =
                    current.try_as<FrameworkElement>()) {
                const std::wstring className =
                    SafeClassName(element);
                if (IsNativeTransientStatusOwnerClass(
                        className)) {
                    if (ownerClass) {
                        *ownerClass = className;
                    }
                    return true;
                }
            }

            current =
                Media::VisualTreeHelper::GetParent(
                    current);
        }
    } catch (...) {
    }

    return false;
}

void RemoveTransientStatusTextObserverRegistration(
    const std::shared_ptr<TransientStatusTextObserverRegistration>& registration) {

    AcquireSRWLockExclusive(
        &g_transientStatusObserverLock);

    auto it = std::remove(
        g_transientStatusTextObservers.begin(),
        g_transientStatusTextObservers.end(),
        registration);

    g_transientStatusTextObservers.erase(
        it,
        g_transientStatusTextObservers.end());

    ReleaseSRWLockExclusive(
        &g_transientStatusObserverLock);
}

void DetachTransientStatusTextObserver(
    const std::shared_ptr<TransientStatusTextObserverRegistration>& registration,
    bool removeHandlers) {

    if (!registration) {
        return;
    }

    const bool wasActive =
        registration->active.exchange(
            false,
            std::memory_order_acq_rel);

    if (removeHandlers) {
        try {
            if (auto textBlock =
                    registration->textBlock.get()) {
                if (registration->propertyRegistered) {
                    textBlock.UnregisterPropertyChangedCallback(
                        Controls::TextBlock::TextProperty(),
                        registration->propertyToken);
                }
                if (registration->unloadedRegistered) {
                    textBlock.Unloaded(
                        registration->unloadedToken);
                }
            }
        } catch (...) {
        }
    }

    if (wasActive || removeHandlers) {
        RemoveTransientStatusTextObserverRegistration(
            registration);
    }
}

void PublishBestNativeTransientStatusOnCurrentThread(
    const wchar_t* reason) {

    if (!g_showNativeTransientStatus.load(
            std::memory_order_acquire) ||
        !g_transientBridge ||
        g_unloading.load(
            std::memory_order_acquire)) {
        return;
    }

    const DWORD currentThreadId =
        GetCurrentThreadId();

    std::vector<std::shared_ptr<TransientStatusTextObserverRegistration>>
        registrations;

    AcquireSRWLockShared(
        &g_transientStatusObserverLock);
    registrations =
        g_transientStatusTextObservers;
    ReleaseSRWLockShared(
        &g_transientStatusObserverLock);

    std::wstring bestVisibleText;
    std::wstring bestRetainedText;
    double bestVisibleArea = -1.0;
    double bestRetainedArea = -1.0;

    for (const auto& registration : registrations) {
        if (!registration ||
            !registration->active.load(
                std::memory_order_acquire) ||
            registration->xamlThreadId != currentThreadId) {
            continue;
        }

        try {
            auto textBlock = registration->textBlock.get();
            if (!textBlock) {
                continue;
            }

            // alpha4r: AttachTransientStatusTextObserver already verified
            // the native StatusMessage owner and registered the Text DP.
            // Re-checking its *current* ancestry/visibility on every
            // LayoutUpdated can briefly fail while native XAML reparents or
            // transitions the control. That used to publish an empty status
            // even though the remote UI continued rendering the original
            // nonempty TextBlock for many seconds.
            const auto text = textBlock.Text();
            if (text.empty()) {
                continue;
            }
            std::wstring value(text.c_str(), text.size());

            bool visible = false;
            try {
                visible = textBlock.Visibility() == Visibility::Visible;
            } catch (...) {
                // A transient visibility-query failure must not be treated
                // as an explicit native Text change or Unloaded event.
            }

            double area = 0.0;
            try {
                area =
                    std::max(0.0, textBlock.ActualWidth()) *
                    std::max(0.0, textBlock.ActualHeight());
            } catch (...) {
            }

            // Prefer genuinely visible status controls when there are two.
            // Retain a temporarily hidden, still-live nonempty TextBlock only
            // as a fallback. A real Text clear, native Unloaded callback,
            // writer death, or WTS terminal cleanup still releases it.
            if (visible) {
                if (bestVisibleText.empty() || area >= bestVisibleArea) {
                    bestVisibleText = std::move(value);
                    bestVisibleArea = area;
                }
            } else if (bestRetainedText.empty() ||
                       area >= bestRetainedArea) {
                bestRetainedText = std::move(value);
                bestRetainedArea = area;
            }
        } catch (...) {
        }
    }

    PublishNativeTransientStatus(
        !bestVisibleText.empty() ? bestVisibleText : bestRetainedText,
        reason);
}

// Read-only view of Windows' current native TextBlock state at a *render
// pass*. The result is not a pixel capture and does not prove that the RDP
// encoder delivered this frame. Limit work to at most two frames per status
// publication/console write, and enable only with explicit diagnostics.
[[maybe_unused]] void DescribeNativeStatusAtRenderPass(
    std::wstring* textOut,
    unsigned* visibleOut,
    unsigned* totalOut) {

    if (textOut) textOut->clear();
    if (visibleOut) *visibleOut = 0;
    if (totalOut) *totalOut = 0;
    const DWORD threadId = GetCurrentThreadId();

    std::vector<std::shared_ptr<TransientStatusTextObserverRegistration>>
        observers;
    AcquireSRWLockShared(&g_transientStatusObserverLock);
    observers = g_transientStatusTextObservers;
    ReleaseSRWLockShared(&g_transientStatusObserverLock);

    double largest = -1.0;
    for (const auto& reg : observers) {
        if (!reg ||
            !reg->active.load(std::memory_order_acquire) ||
            reg->xamlThreadId != threadId) {
            continue;
        }
        try {
            auto textBlock = reg->textBlock.get();
            if (!textBlock) continue;
            if (totalOut) ++*totalOut;
            if (textBlock.Visibility() != Visibility::Visible) continue;
            if (visibleOut) ++*visibleOut;
            const double area =
                std::max(0.0, textBlock.ActualWidth()) *
                std::max(0.0, textBlock.ActualHeight());
            if (area >= largest && textOut) {
                auto text = textBlock.Text();
                *textOut = std::wstring(text.c_str(), text.size());
                largest = area;
            }
        } catch (...) {
        }
    }
}

void EnsureTransientRenderProbeOnCurrentThread() {
    if (!g_enableLayoutDiagnostics.load(std::memory_order_relaxed) ||
        !g_showNativeTransientStatus.load(std::memory_order_acquire) ||
        g_unloading.load(std::memory_order_acquire)) {
        return;
    }

    const DWORD threadId = GetCurrentThreadId();
    AcquireSRWLockShared(&g_transientRenderProbeLock);
    for (const auto& reg : g_transientRenderProbes) {
        if (reg && reg->active.load(std::memory_order_acquire) &&
            reg->xamlThreadId == threadId) {
            ReleaseSRWLockShared(&g_transientRenderProbeLock);
            return;
        }
    }
    ReleaseSRWLockShared(&g_transientRenderProbeLock);

    auto reg = std::make_shared<TransientRenderProbeRegistration>();
    reg->xamlThreadId = threadId;
    std::weak_ptr<TransientRenderProbeRegistration> weakReg = reg;
    try {
        reg->renderingToken = Media::CompositionTarget::Rendering(
            [weakReg](auto const&, auto const&) {
                auto reg = weakReg.lock();
                if (!reg ||
                    !reg->active.load(std::memory_order_acquire) ||
                    g_unloading.load(std::memory_order_acquire)) {
                    return;
                }
                const auto sourceSerial = g_renderWriterSerial.load(
                    std::memory_order_acquire);
                const auto appliedSerial = g_renderConsoleSerial.load(
                    std::memory_order_acquire);
                if (sourceSerial != reg->lastWriterSerial ||
                    appliedSerial != reg->lastConsoleSerial) {
                    reg->lastWriterSerial = sourceSerial;
                    reg->lastConsoleSerial = appliedSerial;
                    reg->pendingFrames = 2;
                    reg->sampledFrames = 0;
                }
                if (!reg->pendingFrames ||
                    (sourceSerial == 0 && appliedSerial == 0)) {
                    return;
                }
                --reg->pendingFrames;
                ++reg->sampledFrames;
                const auto now = GetTickCount64();
                const auto sourceTick = g_renderWriterTick.load(
                    std::memory_order_relaxed);
                const auto appliedTick = g_renderConsoleTick.load(
                    std::memory_order_relaxed);
                std::wstring nativeText;
                unsigned visible = 0;
                unsigned total = 0;
                DescribeNativeStatusAtRenderPass(
                    &nativeText, &visible, &total);
                ProbeLog(
                    L"TRANSIENT_RENDER_PASS frame=%u nativeSerial=%llu consoleSerial=%llu nativeAgeMs=%llu consoleAgeMs=%llu observers=%u visible=%u nativeText=\"%s\"",
                    reg->sampledFrames,
                    static_cast<unsigned long long>(sourceSerial),
                    static_cast<unsigned long long>(appliedSerial),
                    static_cast<unsigned long long>(
                        sourceTick && now >= sourceTick ? now - sourceTick : 0),
                    static_cast<unsigned long long>(
                        appliedTick && now >= appliedTick ? now - appliedTick : 0),
                    total, visible, nativeText.c_str());
            });
        reg->subscribed = true;
    } catch (...) {
        ProbeLog(L"TRANSIENT_RENDER_PASS attach failed thread=%lu", threadId);
        return;
    }
    AcquireSRWLockExclusive(&g_transientRenderProbeLock);
    g_transientRenderProbes.push_back(reg);
    ReleaseSRWLockExclusive(&g_transientRenderProbeLock);
    ProbeLog(L"TRANSIENT_RENDER_PASS observer attached thread=%lu", threadId);
}

void DetachTransientRenderProbesOnCurrentThread() {
    const DWORD threadId = GetCurrentThreadId();
    std::vector<std::shared_ptr<TransientRenderProbeRegistration>> removed;
    AcquireSRWLockExclusive(&g_transientRenderProbeLock);
    auto it = g_transientRenderProbes.begin();
    while (it != g_transientRenderProbes.end()) {
        if (*it && (*it)->xamlThreadId == threadId) {
            removed.push_back(*it);
            it = g_transientRenderProbes.erase(it);
        } else {
            ++it;
        }
    }
    ReleaseSRWLockExclusive(&g_transientRenderProbeLock);
    for (const auto& reg : removed) {
        reg->active.store(false, std::memory_order_release);
        if (reg->subscribed) {
            try {
                Media::CompositionTarget::Rendering(reg->renderingToken);
            } catch (...) {
            }
        }
    }
}

bool HasTransientStatusTextObserver(
    DWORD threadId,
    void* textIdentity) {

    bool found = false;

    AcquireSRWLockShared(
        &g_transientStatusObserverLock);

    for (const auto& registration :
         g_transientStatusTextObservers) {
        if (registration &&
            registration->active.load(
                std::memory_order_acquire) &&
            registration->xamlThreadId == threadId &&
            registration->textIdentity == textIdentity) {
            found = true;
            break;
        }
    }

    ReleaseSRWLockShared(
        &g_transientStatusObserverLock);
    return found;
}

bool AttachTransientStatusTextObserver(
    const Controls::TextBlock& textBlock,
    const std::wstring& ownerClass) {

    if (!textBlock ||
        g_unloading.load(
            std::memory_order_acquire)) {
        return false;
    }

    const DWORD threadId =
        GetCurrentThreadId();
    void* identity =
        reinterpret_cast<void*>(
            winrt::get_abi(textBlock));

    if (HasTransientStatusTextObserver(
            threadId,
            identity)) {
        return false;
    }

    auto registration =
        std::make_shared<TransientStatusTextObserverRegistration>();
    registration->textBlock =
        winrt::make_weak(textBlock);
    registration->xamlThreadId =
        threadId;
    registration->textIdentity =
        identity;
    registration->ownerClass =
        ownerClass;

    std::weak_ptr<TransientStatusTextObserverRegistration>
        weakRegistration = registration;

    try {
        DependencyPropertyChangedCallback propertyCallback =
            [weakRegistration](DependencyObject const&,
                               DependencyProperty const&) {
                auto registration =
                    weakRegistration.lock();
                if (!registration ||
                    !registration->active.load(
                        std::memory_order_acquire) ||
                    g_unloading.load(
                        std::memory_order_acquire)) {
                    return;
                }

                PublishBestNativeTransientStatusOnCurrentThread(
                    L"StatusMessage.Text changed");
            };

        registration->propertyToken =
            textBlock.RegisterPropertyChangedCallback(
                Controls::TextBlock::TextProperty(),
                propertyCallback);
        registration->propertyRegistered = true;

        registration->unloadedToken =
            textBlock.Unloaded(
                [weakRegistration](auto const&, auto const&) {
                    auto registration =
                        weakRegistration.lock();
                    if (!registration) {
                        return;
                    }

                    registration->active.store(
                        false,
                        std::memory_order_release);

                    // If another native status control is already live on this
                    // XAML thread, keep its exact text; otherwise this publishes
                    // an explicit empty string and releases the bridge override.
                    PublishBestNativeTransientStatusOnCurrentThread(
                        L"StatusMessage.Unloaded");
                });
        registration->unloadedRegistered = true;
    } catch (...) {
        try {
            if (registration->propertyRegistered) {
                textBlock.UnregisterPropertyChangedCallback(
                    Controls::TextBlock::TextProperty(),
                    registration->propertyToken);
            }
        } catch (...) {
        }
        return false;
    }

    AcquireSRWLockExclusive(
        &g_transientStatusObserverLock);
    g_transientStatusTextObservers.push_back(
        registration);
    ReleaseSRWLockExclusive(
        &g_transientStatusObserverLock);

    ProbeLog(
        L"TRANSIENT_STATUS observer attached owner=\"%s\" textBlock=%p thread=%lu",
        ownerClass.c_str(),
        identity,
        threadId);

    return true;
}

void PruneInactiveTransientStatusTextObserversOnCurrentThread() {
    const DWORD currentThreadId =
        GetCurrentThreadId();

    std::vector<std::shared_ptr<TransientStatusTextObserverRegistration>>
        registrations;

    AcquireSRWLockShared(
        &g_transientStatusObserverLock);
    registrations =
        g_transientStatusTextObservers;
    ReleaseSRWLockShared(
        &g_transientStatusObserverLock);

    for (const auto& registration : registrations) {
        if (!registration ||
            registration->xamlThreadId !=
                currentThreadId) {
            continue;
        }

        bool remove =
            !registration->active.load(
                std::memory_order_acquire);

        if (!remove) {
            try {
                remove =
                    !registration->textBlock.get();
            } catch (...) {
                remove = true;
            }
        }

        if (remove) {
            DetachTransientStatusTextObserver(
                registration,
                true);
        }
    }
}

void DetachTransientStatusTextObserversOnCurrentThread() {
    const DWORD currentThreadId =
        GetCurrentThreadId();

    std::vector<std::shared_ptr<TransientStatusTextObserverRegistration>>
        registrations;

    AcquireSRWLockShared(
        &g_transientStatusObserverLock);
    registrations =
        g_transientStatusTextObservers;
    ReleaseSRWLockShared(
        &g_transientStatusObserverLock);

    for (const auto& registration : registrations) {
        if (registration &&
            registration->xamlThreadId ==
                currentThreadId) {
            DetachTransientStatusTextObserver(
                registration,
                true);
        }
    }
}

void ScanNativeTransientStatusObserversRecursive(
    const FrameworkElement& element,
    int depth,
    int& budget,
    unsigned* attached) {

    if (!element ||
        depth > 64 ||
        budget <= 0) {
        return;
    }

    --budget;

    try {
        if (auto textBlock =
                element.try_as<Controls::TextBlock>()) {
            std::wstring ownerClass;
            if (IsNativeTransientStatusTextBlock(
                    textBlock,
                    &ownerClass) &&
                AttachTransientStatusTextObserver(
                    textBlock,
                    ownerClass)) {
                if (attached) {
                    ++*attached;
                }
            }
        }

        const int count =
            Media::VisualTreeHelper::GetChildrenCount(
                element);

        for (int i = 0;
             i < count && budget > 0;
             ++i) {
            auto child =
                Media::VisualTreeHelper::GetChild(
                    element,
                    i).try_as<FrameworkElement>();
            if (child) {
                ScanNativeTransientStatusObserversRecursive(
                    child,
                    depth + 1,
                    budget,
                    attached);
            }
        }
    } catch (...) {
    }
}

void ScanNativeTransientStatusObservers(
    const FrameworkElement& root,
    const wchar_t* reason) {

    if (!root ||
        !g_showNativeTransientStatus.load(
            std::memory_order_acquire) ||
        !g_transientBridge ||
        g_unloading.load(
            std::memory_order_acquire)) {
        return;
    }

    PruneInactiveTransientStatusTextObserversOnCurrentThread();

    int budget = 3000;
    unsigned attached = 0;
    ScanNativeTransientStatusObserversRecursive(
        root,
        0,
        budget,
        &attached);

    PublishBestNativeTransientStatusOnCurrentThread(
        reason ? reason : L"transient scan");

    if (attached != 0) {
        ProbeLog(
            L"TRANSIENT_STATUS scan reason=%s attached=%u remainingBudget=%d thread=%lu",
            reason ? reason : L"<unknown>",
            attached,
            budget,
            GetCurrentThreadId());
    }
}

void RemoveTransientStatusRootRegistration(
    const std::shared_ptr<TransientStatusRootRegistration>& registration) {

    AcquireSRWLockExclusive(
        &g_transientStatusRootLock);

    auto it = std::remove(
        g_transientStatusRootRegistrations.begin(),
        g_transientStatusRootRegistrations.end(),
        registration);
    g_transientStatusRootRegistrations.erase(
        it,
        g_transientStatusRootRegistrations.end());

    ReleaseSRWLockExclusive(
        &g_transientStatusRootLock);
}

void DetachTransientStatusRootObserver(
    const std::shared_ptr<TransientStatusRootRegistration>& registration,
    bool removeHandler) {

    if (!registration ||
        !registration->active.exchange(
            false,
            std::memory_order_acq_rel)) {
        return;
    }

    if (removeHandler) {
        try {
            if (auto root =
                    registration->root.get()) {
                root.LayoutUpdated(
                    registration->layoutToken);
            }
        } catch (...) {
        }
    }

    RemoveTransientStatusRootRegistration(
        registration);
}

void DetachTransientStatusRootObserversOnCurrentThread() {
    const DWORD currentThreadId =
        GetCurrentThreadId();

    std::vector<std::shared_ptr<TransientStatusRootRegistration>>
        registrations;

    AcquireSRWLockShared(
        &g_transientStatusRootLock);
    registrations =
        g_transientStatusRootRegistrations;
    ReleaseSRWLockShared(
        &g_transientStatusRootLock);

    for (const auto& registration : registrations) {
        if (registration &&
            registration->xamlThreadId ==
                currentThreadId) {
            DetachTransientStatusRootObserver(
                registration,
                true);
        }
    }
}

bool HasTransientStatusRootObserver(
    DWORD threadId,
    void* rootIdentity) {

    bool found = false;

    AcquireSRWLockShared(
        &g_transientStatusRootLock);
    for (const auto& registration :
         g_transientStatusRootRegistrations) {
        if (registration &&
            registration->active.load(
                std::memory_order_acquire) &&
            registration->xamlThreadId == threadId &&
            registration->rootIdentity == rootIdentity) {
            found = true;
            break;
        }
    }
    ReleaseSRWLockShared(
        &g_transientStatusRootLock);

    return found;
}

void AttachTransientStatusRootObserver(
    const FrameworkElement& root) {

    if (!root ||
        !g_showNativeTransientStatus.load(
            std::memory_order_acquire) ||
        !g_transientBridge ||
        g_unloading.load(
            std::memory_order_acquire)) {
        return;
    }

    const DWORD threadId =
        GetCurrentThreadId();
    void* rootIdentity =
        reinterpret_cast<void*>(
            winrt::get_abi(root));

    if (HasTransientStatusRootObserver(
            threadId,
            rootIdentity)) {
        EnsureTransientRenderProbeOnCurrentThread();
        ScanNativeTransientStatusObservers(
            root,
            L"SystemProvider/root revalidated");
        return;
    }

    EnsureTransientRenderProbeOnCurrentThread();

    auto registration =
        std::make_shared<TransientStatusRootRegistration>();
    registration->root =
        winrt::make_weak(root);
    registration->xamlThreadId =
        threadId;
    registration->rootIdentity =
        rootIdentity;

    std::weak_ptr<TransientStatusRootRegistration>
        weakRegistration = registration;

    try {
        registration->layoutToken =
            root.LayoutUpdated(
                [weakRegistration](auto const&, auto const&) {
                    auto registration =
                        weakRegistration.lock();
                    if (!registration ||
                        !registration->active.load(
                            std::memory_order_acquire) ||
                        g_unloading.load(
                            std::memory_order_acquire)) {
                        return;
                    }

                    const ULONGLONG now =
                        GetTickCount64();
                    const ULONGLONG next =
                        registration->nextScanTick.load(
                            std::memory_order_acquire);
                    if (next != 0 && now < next) {
                        return;
                    }

                    registration->nextScanTick.store(
                        now + kTransientObserverScanThrottleMs,
                        std::memory_order_release);

                    auto root =
                        registration->root.get();
                    if (!root) {
                        registration->active.store(
                            false,
                            std::memory_order_release);
                        return;
                    }

                    ScanNativeTransientStatusObservers(
                        root,
                        L"LayoutUpdated");
                });
    } catch (...) {
        return;
    }

    AcquireSRWLockExclusive(
        &g_transientStatusRootLock);
    g_transientStatusRootRegistrations.push_back(
        registration);
    ReleaseSRWLockExclusive(
        &g_transientStatusRootLock);

    ProbeLog(
        L"TRANSIENT_STATUS retained root observer attached thread=%lu root=%p throttleMs=%llu",
        threadId,
        rootIdentity,
        static_cast<unsigned long long>(
            kTransientObserverScanThrottleMs));

    ScanNativeTransientStatusObservers(
        root,
        L"root attach");
}

void AttachTransientStatusFromSystemProviderSender(
    void* sender) {

    if (!sender ||
        !g_showNativeTransientStatus.load(
            std::memory_order_acquire) ||
        !g_transientBridge ||
        g_unloading.load(
            std::memory_order_acquire)) {
        return;
    }

    try {
        winrt::Windows::Foundation::IUnknown unknown;
        winrt::copy_from_abi(
            unknown,
            sender);

        auto senderElement =
            unknown.try_as<FrameworkElement>();
        if (!senderElement) {
            return;
        }

        auto root =
            FindVisualRoot(
                senderElement);
        if (root) {
            AttachTransientStatusRootObserver(
                root);
        }
    } catch (...) {
    }
}

void RemoveInitialLayoutRetryRegistration(
    const std::shared_ptr<InitialLayoutRetryRegistration>& registration) {

    AcquireSRWLockExclusive(
        &g_initialLayoutRetriesLock);

    auto it = std::remove(
        g_initialLayoutRetries.begin(),
        g_initialLayoutRetries.end(),
        registration);

    g_initialLayoutRetries.erase(
        it,
        g_initialLayoutRetries.end());

    ReleaseSRWLockExclusive(
        &g_initialLayoutRetriesLock);
}

void DetachInitialLayoutRetry(
    const std::shared_ptr<InitialLayoutRetryRegistration>& registration,
    bool removeHandler) {

    if (!registration ||
        !registration->active.exchange(
            false,
            std::memory_order_acq_rel)) {
        return;
    }

    if (removeHandler) {
        try {
            if (auto root =
                    registration->root.get()) {
                root.LayoutUpdated(
                    registration->token);
            }
        } catch (...) {
        }
    }

    RemoveInitialLayoutRetryRegistration(
        registration);
}

void DetachInitialLayoutRetriesOnCurrentThread() {
    const DWORD currentThreadId =
        GetCurrentThreadId();

    std::vector<
        std::shared_ptr<InitialLayoutRetryRegistration>>
        registrations;

    AcquireSRWLockShared(
        &g_initialLayoutRetriesLock);
    registrations =
        g_initialLayoutRetries;
    ReleaseSRWLockShared(
        &g_initialLayoutRetriesLock);

    for (const auto& registration :
         registrations) {
        if (registration &&
            registration->xamlThreadId ==
                currentThreadId) {
            DetachInitialLayoutRetry(
                registration,
                true);
        }
    }
}

bool HasInitialLayoutRetryForRoot(
    DWORD threadId,
    void* rootIdentity) {

    bool found = false;

    AcquireSRWLockShared(
        &g_initialLayoutRetriesLock);

    for (const auto& registration :
         g_initialLayoutRetries) {
        if (registration &&
            registration->active.load(
                std::memory_order_acquire) &&
            registration->xamlThreadId ==
                threadId &&
            registration->rootIdentity ==
                rootIdentity) {
            found = true;
            break;
        }
    }

    ReleaseSRWLockShared(
        &g_initialLayoutRetriesLock);

    return found;
}

void AttachInitialLayoutRetry(
    const FrameworkElement& root) {

    if (!root ||
        g_unloading.load(
            std::memory_order_acquire)) {
        return;
    }

    const DWORD threadId =
        GetCurrentThreadId();
    void* rootIdentity =
        reinterpret_cast<void*>(
            winrt::get_abi(root));

    if (HasInitialLayoutRetryForRoot(
            threadId,
            rootIdentity)) {
        return;
    }

    auto registration =
        std::make_shared<
            InitialLayoutRetryRegistration>();

    registration->root =
        winrt::make_weak(root);
    registration->xamlThreadId =
        threadId;
    registration->rootIdentity =
        rootIdentity;
    registration->observedRepairGeneration.store(
        g_layoutRepairGeneration.load(
            std::memory_order_acquire),
        std::memory_order_release);

    std::weak_ptr<InitialLayoutRetryRegistration>
        weakRegistration =
            registration;

    try {
        registration->token =
            root.LayoutUpdated(
                [weakRegistration](
                    auto const&,
                    auto const&) {

                    auto registration =
                        weakRegistration.lock();

                    if (!registration ||
                        !registration->active.load(
                            std::memory_order_acquire)) {
                        return;
                    }

                    if (g_unloading.load(
                            std::memory_order_acquire)) {
                        return;
                    }

                    const unsigned callback =
                        registration->callbacks.fetch_add(
                            1,
                            std::memory_order_acq_rel) + 1;

                    auto root =
                        registration->root.get();

                    if (!root) {
                        DetachInitialLayoutRetry(
                            registration,
                            false);
                        return;
                    }

                    const bool bootstrapComplete =
                        registration->bootstrapComplete.load(
                            std::memory_order_acquire);

                    const unsigned currentGeneration =
                        g_layoutRepairGeneration.load(
                            std::memory_order_acquire);

                    if (bootstrapComplete) {
                        const unsigned observedGeneration =
                            registration->observedRepairGeneration.load(
                                std::memory_order_acquire);

                        if (observedGeneration !=
                            currentGeneration) {
                            if (g_adaptiveRepairCoalescing.load(
                                    std::memory_order_acquire) &&
                                registration->repairWalkCount.load(
                                    std::memory_order_acquire) > 0 &&
                                registration->repairDeadlineTick.load(
                                    std::memory_order_acquire) != 0) {
                                ProbeLog(L"REPAIR_COALESCE superseded generation=%u nextGeneration=%u root=%p walks=%u clean=%u dirty=%u deferredCallbacks=%u",
                                    observedGeneration, currentGeneration,
                                    registration->rootIdentity,
                                    registration->repairWalkCount.load(),
                                    registration->repairCleanWalks.load(),
                                    registration->repairDirtyWalks.load(),
                                    registration->throttledLayoutCallbacks.load());
                            }
                            registration->repairWalkCount.store(0);
                            registration->repairCleanWalks.store(0);
                            registration->repairDirtyWalks.store(0);
                            registration->throttledLayoutCallbacks.store(0);
                            registration->cleanBeforeGraceStreak.store(0);
                            registration->largestChosenThrottleMs.store(0);

                            const ULONGLONG now =
                                GetTickCount64();

                            registration->observedRepairGeneration.store(
                                currentGeneration,
                                std::memory_order_release);
                            registration->repairDeadlineTick.store(
                                now + kLifecycleRepairWindowMs,
                                std::memory_order_release);
                            registration->nextRepairEligibleTick.store(
                                0,
                                std::memory_order_release);
                            registration->firstRepairRowsTick.store(
                                0,
                                std::memory_order_release);
                            registration->lastRepairUserRows.store(
                                0,
                                std::memory_order_release);
                            registration->stableRepairPasses.store(
                                0,
                                std::memory_order_release);

                            ProbeLog(
                                L"LIFECYCLE_LAYOUT repair armed generation=%u "
                                L"windowMs=%llu thread=%lu root=%p",
                                currentGeneration,
                                static_cast<unsigned long long>(
                                    kLifecycleRepairWindowMs),
                                GetCurrentThreadId(),
                                registration->rootIdentity);
                        }

                        const ULONGLONG deadline =
                            registration->repairDeadlineTick.load(
                                std::memory_order_acquire);

                        if (deadline == 0) {
                            // Steady state: keep the callback attached, but
                            // don't walk the tree on ordinary LayoutUpdated.
                            return;
                        }

                        const ULONGLONG now =
                            GetTickCount64();

                        if (now >= deadline) {
                            registration->repairDeadlineTick.store(
                                0,
                                std::memory_order_release);
                            registration->nextRepairEligibleTick.store(
                                0,
                                std::memory_order_release);
                            registration->firstRepairRowsTick.store(
                                0,
                                std::memory_order_release);
                            registration->lastRepairUserRows.store(
                                0,
                                std::memory_order_release);
                            registration->stableRepairPasses.store(
                                0,
                                std::memory_order_release);

                            if (g_adaptiveRepairCoalescing.load(
                                    std::memory_order_acquire)) {
                                ProbeLog(L"REPAIR_COALESCE deadline generation=%u root=%p walks=%u clean=%u dirty=%u deferredCallbacks=%u maxCooldownMs=%llu",
                                    currentGeneration, registration->rootIdentity,
                                    registration->repairWalkCount.load(),
                                    registration->repairCleanWalks.load(),
                                    registration->repairDirtyWalks.load(),
                                    registration->throttledLayoutCallbacks.load(),
                                    static_cast<unsigned long long>(registration->largestChosenThrottleMs.load()));
                            }
                            ProbeLog(
                                L"LIFECYCLE_LAYOUT repair deadline expired generation=%u "
                                L"thread=%lu root=%p",
                                currentGeneration,
                                GetCurrentThreadId(),
                                registration->rootIdentity);
                            return;
                        }

                        const ULONGLONG nextEligible =
                            registration->nextRepairEligibleTick.load(
                                std::memory_order_acquire);

                        if (nextEligible != 0 &&
                            now < nextEligible) {
                            if (g_adaptiveRepairCoalescing.load(
                                    std::memory_order_relaxed)) {
                                registration->throttledLayoutCallbacks.fetch_add(
                                    1, std::memory_order_relaxed);
                            }
                            // During periods where Windows has temporarily
                            // removed the user list, coalesce high-frequency
                            // LayoutUpdated callbacks instead of walking the
                            // same empty tree repeatedly.
                            return;
                        }
                    }

                    // Exact native StatusMessage bursts are latency-sensitive.
                    // Once bootstrap has realized the user list, do not let a
                    // bounded WTS/lifecycle repair monopolize this XAML thread
                    // while the console worker is actively mirroring native
                    // transient text. Keep the repair window armed and resume
                    // it after the transient clears.
                    if (bootstrapComplete &&
                        g_transientBurstActive.load(
                            std::memory_order_acquire)) {

                        registration->repairDeadlineTick.store(
                            GetTickCount64() +
                                kLifecycleRepairWindowMs,
                            std::memory_order_release);

                        if (!registration->repairDeferredByTransient.exchange(
                                true,
                                std::memory_order_acq_rel)) {
                            ProbeLog(
                                L"LIFECYCLE_LAYOUT repair deferred for native transient generation=%u serial=%llu thread=%lu",
                                currentGeneration,
                                static_cast<unsigned long long>(
                                    g_transientBurstSerial.load(
                                        std::memory_order_acquire)),
                                GetCurrentThreadId());
                        }
                        return;
                    }

                    if (bootstrapComplete &&
                        registration->repairDeferredByTransient.exchange(
                            false,
                            std::memory_order_acq_rel)) {
                        ProbeLog(
                            L"LIFECYCLE_LAYOUT repair resumed after native transient generation=%u thread=%lu",
                            currentGeneration,
                            GetCurrentThreadId());
                    }

                    const RestoreStats stats =
                        RefreshRemoteStatusRows(
                            root);
                    if (bootstrapComplete) {
                        registration->repairWalkCount.fetch_add(
                            1, std::memory_order_relaxed);
                        if (stats.userRows != 0 &&
                            stats.patchedRows == 0 &&
                            stats.signatureMisses == 0) {
                            registration->repairCleanWalks.fetch_add(
                                1, std::memory_order_relaxed);
                        } else {
                            registration->repairDirtyWalks.fetch_add(
                                1, std::memory_order_relaxed);
                        }
                    }

                    if (!bootstrapComplete) {
                        if (stats.userRows != 0) {
                            registration->bootstrapComplete.store(
                                true,
                                std::memory_order_release);
                            registration->observedRepairGeneration.store(
                                currentGeneration,
                                std::memory_order_release);
                            registration->repairDeadlineTick.store(
                                0,
                                std::memory_order_release);
                            registration->nextRepairEligibleTick.store(
                                0,
                                std::memory_order_release);
                            registration->firstRepairRowsTick.store(
                                0,
                                std::memory_order_release);
                            registration->lastRepairUserRows.store(
                                0,
                                std::memory_order_release);
                            registration->stableRepairPasses.store(
                                0,
                                std::memory_order_release);

                            ProbeLog(
                                L"BOOTSTRAP_LAYOUT ready callbacks=%u userRows=%u "
                                L"changedRows=%u thread=%lu; lifecycle watcher retained idle",
                                callback,
                                stats.userRows,
                                stats.patchedRows,
                                GetCurrentThreadId());
                            return;
                        }

                        if (callback >= 64) {
                            ProbeLog(
                                L"BOOTSTRAP_LAYOUT giving up after %u LayoutUpdated "
                                L"callbacks thread=%lu",
                                callback,
                                GetCurrentThreadId());

                            DetachInitialLayoutRetry(
                                registration,
                                true);
                        }

                        return;
                    }

                    const ULONGLONG now =
                        GetTickCount64();
                    const ULONGLONG deadline =
                        registration->repairDeadlineTick.load(
                            std::memory_order_acquire);

                    if (stats.userRows == 0) {
                        registration->cleanBeforeGraceStreak.store(
                            0, std::memory_order_relaxed);
                        // The lifecycle repair window is realization-aware:
                        // an empty tree never consumes a success/stability
                        // pass. Stay armed until rows appear or the hard
                        // deadline is reached.
                        registration->nextRepairEligibleTick.store(
                            now + kLifecycleEmptyTreeThrottleMs,
                            std::memory_order_release);
                        registration->firstRepairRowsTick.store(
                            0,
                            std::memory_order_release);
                        registration->lastRepairUserRows.store(
                            0,
                            std::memory_order_release);
                        registration->stableRepairPasses.store(
                            0,
                            std::memory_order_release);

                        if (g_enableLayoutDiagnostics.load(
                                std::memory_order_relaxed)) {
                            ProbeLog(
                                L"LIFECYCLE_LAYOUT waiting for user rows generation=%u "
                                L"retryAfterMs=%llu deadlineInMs=%llu",
                                currentGeneration,
                                static_cast<unsigned long long>(
                                    kLifecycleEmptyTreeThrottleMs),
                                static_cast<unsigned long long>(
                                    deadline > now
                                        ? deadline - now
                                        : 0));
                        }
                        return;
                    }

                    // Keep the first structural wakeup unthrottled. Only
                    // successive no-change walks can receive a longer
                    // cooldown; the original two-pass post-grace settle
                    // remains untouched. No timers or queued refreshes.
                    ULONGLONG firstRowsTick =
                        registration->firstRepairRowsTick.load(
                            std::memory_order_acquire);

                    if (firstRowsTick == 0) {
                        firstRowsTick = now;
                        registration->firstRepairRowsTick.store(
                            firstRowsTick,
                            std::memory_order_release);
                    }

                    const unsigned previousUserRows =
                        registration->lastRepairUserRows.exchange(
                            stats.userRows,
                            std::memory_order_acq_rel);

                    const bool rowCountStable =
                        previousUserRows != 0 &&
                        previousUserRows == stats.userRows;

                    const bool cleanPass =
                        stats.patchedRows == 0 &&
                        stats.signatureMisses == 0;

                    const bool realizationGraceElapsed =
                        now >= firstRowsTick &&
                        (now - firstRowsTick) >=
                            kLifecycleRealizationGraceMs;

                    ULONGLONG throttleMs =
                        kLifecycleNormalRepairThrottleMs;
                    if (g_adaptiveRepairCoalescing.load(
                            std::memory_order_acquire) &&
                        cleanPass && rowCountStable &&
                        !realizationGraceElapsed) {
                        const unsigned streak =
                            registration->cleanBeforeGraceStreak.fetch_add(
                                1, std::memory_order_relaxed) + 1;
                        throttleMs = (streak >= 2)
                            ? kLifecycleCleanBackoffMaximumMs
                            : kLifecycleCleanBackoffFirstMs;
                    } else {
                        registration->cleanBeforeGraceStreak.store(
                            0, std::memory_order_relaxed);
                    }
                    registration->nextRepairEligibleTick.store(
                        now + throttleMs,
                        std::memory_order_release);
                    if (throttleMs > registration->largestChosenThrottleMs.load(
                            std::memory_order_relaxed)) {
                        registration->largestChosenThrottleMs.store(
                            throttleMs, std::memory_order_relaxed);
                    }
                    unsigned stablePasses = 0;

                    if (cleanPass &&
                        rowCountStable &&
                        realizationGraceElapsed) {

                        stablePasses =
                            registration->stableRepairPasses.fetch_add(
                                1,
                                std::memory_order_acq_rel) + 1;
                    } else {
                        registration->stableRepairPasses.store(
                            0,
                            std::memory_order_release);
                    }

                    ProbeLog(
                        L"LIFECYCLE_LAYOUT repair pass userRows=%u changedRows=%u "
                        L"signatureMisses=%u rowCountStable=%u grace=%u stable=%u/%u "
                        L"generation=%u deadlineInMs=%llu cooldownMs=%llu",
                        stats.userRows,
                        stats.patchedRows,
                        stats.signatureMisses,
                        rowCountStable ? 1u : 0u,
                        realizationGraceElapsed ? 1u : 0u,
                        stablePasses,
                        kLifecycleStablePassesRequired,
                        currentGeneration,
                        static_cast<unsigned long long>(
                            deadline > now
                                ? deadline - now
                                : 0),
                        static_cast<unsigned long long>(throttleMs));

                    if (stablePasses >=
                        kLifecycleStablePassesRequired) {

                        registration->repairDeadlineTick.store(
                            0,
                            std::memory_order_release);
                        registration->nextRepairEligibleTick.store(
                            0,
                            std::memory_order_release);
                        registration->firstRepairRowsTick.store(
                            0,
                            std::memory_order_release);
                        registration->lastRepairUserRows.store(
                            0,
                            std::memory_order_release);
                        registration->stableRepairPasses.store(
                            0,
                            std::memory_order_release);

                        if (g_adaptiveRepairCoalescing.load(
                                std::memory_order_acquire)) {
                            ProbeLog(L"REPAIR_COALESCE settled generation=%u root=%p walks=%u clean=%u dirty=%u deferredCallbacks=%u maxCooldownMs=%llu",
                                currentGeneration, registration->rootIdentity,
                                registration->repairWalkCount.load(),
                                registration->repairCleanWalks.load(),
                                registration->repairDirtyWalks.load(),
                                registration->throttledLayoutCallbacks.load(),
                                static_cast<unsigned long long>(registration->largestChosenThrottleMs.load()));
                        }
                        ProbeLog(
                            L"LIFECYCLE_LAYOUT repair settled generation=%u "
                            L"userRows=%u thread=%lu",
                            currentGeneration,
                            stats.userRows,
                            GetCurrentThreadId());
                        if (g_vmStateCorrelationEnabled.load(
                                std::memory_order_acquire)) {
                            QueueVmCorrelationSample(8);
                        }
                    }
                });
    } catch (...) {
        return;
    }

    AcquireSRWLockExclusive(
        &g_initialLayoutRetriesLock);
    g_initialLayoutRetries.push_back(
        registration);
    ReleaseSRWLockExclusive(
        &g_initialLayoutRetriesLock);

    ProbeLog(
        L"BOOTSTRAP_LAYOUT armed retained LayoutUpdated bridge thread=%lu root=%p",
        threadId,
        rootIdentity);
}


unsigned CleanupPatchedRowsRecursive(
    const FrameworkElement& element,
    int depth,
    int& budget) {

    if (!element ||
        depth > 64 ||
        budget <= 0) {
        return 0;
    }

    --budget;
    unsigned restored = 0;

    try {
        if (auto grid =
                element.try_as<
                    winrt::Windows::UI::Xaml::Controls::Grid>()) {

            auto children =
                grid.Children();

            // alpha4d profile-picture ownership uses a pure overlay: Microsoft's
            // native Ellipse and binding are never modified. Hot-disable only
            // needs to remove our named sibling to reveal stock behavior again.
            for (uint32_t i = 0;
                 i < children.Size();
                 ++i) {

                auto ellipse =
                    children.GetAt(i)
                        .try_as<Shapes::Ellipse>();

                if (ellipse &&
                    ElementName(ellipse) ==
                        kInjectedUserTileEllipseName) {
                    children.RemoveAt(i);
                    break;
                }
            }

            winrt::Windows::UI::Xaml::Controls::StackPanel
                visibleStack{nullptr};
            winrt::Windows::UI::Xaml::Controls::StackPanel
                nativeStash{nullptr};

            for (uint32_t i = 0;
                 i < children.Size();
                 ++i) {

                auto candidate =
                    children.GetAt(i)
                        .try_as<
                            winrt::Windows::UI::Xaml::Controls::StackPanel>();

                if (IsInjectedStatusStack(candidate)) {
                    visibleStack = candidate;
                } else if (IsNativeUserTextStash(candidate)) {
                    nativeStash = candidate;
                }
            }

            if (visibleStack &&
                nativeStash) {

                auto visibleChildren =
                    visibleStack.Children();
                auto stashChildren =
                    nativeStash.Children();

                winrt::Windows::UI::Xaml::Controls::TextBlock
                    nativeDisplaySlot{nullptr};
                winrt::Windows::UI::Xaml::Controls::TextBlock
                    nativeStatusSlot{nullptr};
                winrt::Windows::UI::Xaml::Controls::TextBlock
                    nativeQualifiedSlot{nullptr};

                if (visibleChildren.Size() >= 3 &&
                    stashChildren.Size() == 3) {

                    // alpha4c: every visible line is mod-owned. Restore all
                    // three original Microsoft controls from the hidden stash.
                    nativeDisplaySlot =
                        stashChildren.GetAt(0)
                            .try_as<
                                winrt::Windows::UI::Xaml::Controls::TextBlock>();
                    nativeStatusSlot =
                        stashChildren.GetAt(1)
                            .try_as<
                                winrt::Windows::UI::Xaml::Controls::TextBlock>();
                    nativeQualifiedSlot =
                        stashChildren.GetAt(2)
                            .try_as<
                                winrt::Windows::UI::Xaml::Controls::TextBlock>();
                } else if (
                    visibleChildren.Size() == 3 &&
                    stashChildren.Size() == 2) {

                    // alpha4b compatibility: DisplayName was still Microsoft's
                    // native visible TextBlock, while Status/QualifiedName were
                    // the two stashed originals.
                    nativeDisplaySlot =
                        visibleChildren.GetAt(0)
                            .try_as<
                                winrt::Windows::UI::Xaml::Controls::TextBlock>();
                    nativeStatusSlot =
                        stashChildren.GetAt(0)
                            .try_as<
                                winrt::Windows::UI::Xaml::Controls::TextBlock>();
                    nativeQualifiedSlot =
                        stashChildren.GetAt(1)
                            .try_as<
                                winrt::Windows::UI::Xaml::Controls::TextBlock>();
                }

                if (nativeDisplaySlot &&
                    nativeStatusSlot &&
                    nativeQualifiedSlot) {

                    // None of these native controls are locally overwritten in
                    // alpha4c. Their original bindings keep running while
                    // stashed, so hot-disable restores the exact stock objects.
                    visibleChildren.Clear();
                    stashChildren.Clear();
                    children.Clear();

                    children.Append(
                        nativeDisplaySlot);
                    children.Append(
                        nativeStatusSlot);
                    children.Append(
                        nativeQualifiedSlot);

                    DependencyObject ancestor =
                        grid;

                    while (ancestor) {
                        if (auto item =
                                ancestor.try_as<
                                    winrt::Windows::UI::Xaml::Controls::ListViewItem>()) {

                            item.ClearValue(
                                FrameworkElement::HeightProperty());
                            item.ClearValue(
                                FrameworkElement::MinHeightProperty());
                            break;
                        }

                        ancestor =
                            Media::VisualTreeHelper::GetParent(
                                ancestor);
                    }

                    ++restored;
                }

            } else if (visibleStack &&
                       children.Size() == 1) {

                // Compatibility cleanup for a row left in the pre-alpha4b
                // three-native-TextBlock StackPanel layout.
                auto stackChildren =
                    visibleStack.Children();

                std::vector<
                    winrt::Windows::UI::Xaml::Controls::TextBlock>
                    slots;

                for (uint32_t i = 0;
                     i < stackChildren.Size();
                     ++i) {

                    if (auto tb =
                            stackChildren.GetAt(i)
                                .try_as<
                                    winrt::Windows::UI::Xaml::Controls::TextBlock>()) {
                        slots.push_back(tb);
                    }
                }

                if (slots.size() == 3) {
                    auto displaySlot = slots[0];
                    auto statusSlot = slots[1];
                    auto qualifiedSlot = slots[2];

                    int nativeDomainVisible = 0;
                    HRESULT nativeDomainVisibleHr = E_FAIL;

                    void* userVm = nullptr;
                    if (TryGetUserViewModelFromDataContext(
                            grid,
                            &userVm)) {

                        nativeDomainVisible =
                            ReadBoolProperty(
                                g_getIsDomainNameVisible,
                                userVm,
                                &nativeDomainVisibleHr);

                        reinterpret_cast<IUnknown*>(
                            userVm)->Release();
                    }

                    displaySlot.Visibility(
                        Visibility::Visible);
                    statusSlot.Text(
                        winrt::hstring());
                    statusSlot.Visibility(
                        Visibility::Collapsed);
                    qualifiedSlot.Visibility(
                        SUCCEEDED(nativeDomainVisibleHr) &&
                        nativeDomainVisible == 1
                            ? Visibility::Visible
                            : Visibility::Collapsed);

                    stackChildren.Clear();
                    children.Clear();

                    for (const auto& tb : slots) {
                        children.Append(tb);
                    }

                    ++restored;
                }
            }
        }

        const int count =
            Media::VisualTreeHelper::GetChildrenCount(
                element);

        for (int i = 0;
             i < count &&
             budget > 0;
             ++i) {

            auto child =
                Media::VisualTreeHelper::GetChild(
                    element,
                    i)
                    .try_as<FrameworkElement>();

            if (child) {
                restored +=
                    CleanupPatchedRowsRecursive(
                        child,
                        depth + 1,
                        budget);
            }
        }
    } catch (...) {
    }

    return restored;
}

bool CleanupCurrentXamlWindowOnUiThread() {
    // Retained LayoutUpdated handlers, per-presenter DataContextChanged
    // handlers, and alpha4j native StatusMessage callbacks all call back into
    // this DLL. Detach every one on its owning XAML thread before unload.
    DetachTransientRenderProbesOnCurrentThread();
    DetachTransientStatusTextObserversOnCurrentThread();
    DetachTransientStatusRootObserversOnCurrentThread();
    DetachVmPropertyProbesOnCurrentThread();
    DetachPresenterDataContextWatchersOnCurrentThread();
    DetachInitialLayoutRetriesOnCurrentThread();
    DestroyTransientFastUiBridgeWindowOnCurrentThread();

    try {
        auto window =
            Window::Current();

        if (!window) {
            return false;
        }

        auto root =
            window.Content()
                .try_as<FrameworkElement>();

        if (!root) {
            return false;
        }

        RemovePendingRemoteAttemptRows(
            root,
            L"module cleanup");

        int budget = 3000;
        const unsigned restored =
            CleanupPatchedRowsRecursive(
                root,
                0,
                budget);

        if (restored != 0) {
            ProbeLog(
                L"CLEANUP restored %u patched user row(s) on XAML thread=%lu",
                restored,
                GetCurrentThreadId());
        }

        return true;
    } catch (...) {
        return false;
    }
}


bool ProbeRootViewModelObject(
    IUnknown* rootUnknown,
    const wchar_t* source) {

    if (!rootUnknown ||
        !g_haveVmChainIids ||
        !g_rootGetRequestCredentialsVm ||
        !g_requestGetSelectedUserOrV1) {
        return false;
    }

    void* rootVm = nullptr;
    HRESULT rootQiHr =
        rootUnknown->QueryInterface(
            g_rootViewModelPublicIid,
            &rootVm);

    if (FAILED(rootQiHr) || !rootVm) {
        return false;
    }

    ProbeLog(
        L"CHAIN source=%s RootViewModel QI ok iface=%p",
        source ? source : L"<unknown>",
        rootVm);

    void* requestVmObject = nullptr;
    HRESULT requestGetHr =
        g_rootGetRequestCredentialsVm(
            rootVm,
            &requestVmObject);

    reinterpret_cast<IUnknown*>(rootVm)->Release();
    rootVm = nullptr;

    ProbeLog(
        L"CHAIN Root.RequestCredentialsViewModel hr=0x%08X object=%p class=\"%s\"",
        static_cast<unsigned>(requestGetHr),
        requestVmObject,
        RuntimeClassNameFromUnknown(
            reinterpret_cast<IUnknown*>(requestVmObject)).c_str());

    if (FAILED(requestGetHr) || !requestVmObject) {
        if (requestVmObject) {
            reinterpret_cast<IUnknown*>(requestVmObject)->Release();
        }
        return true;
    }

    void* requestVm = nullptr;
    HRESULT requestQiHr =
        reinterpret_cast<IUnknown*>(requestVmObject)->QueryInterface(
            g_requestCredentialVmPublicIid,
            &requestVm);

    reinterpret_cast<IUnknown*>(requestVmObject)->Release();
    requestVmObject = nullptr;

    ProbeLog(
        L"CHAIN RequestCredentialEntryViewModel QI hr=0x%08X iface=%p",
        static_cast<unsigned>(requestQiHr),
        requestVm);

    if (FAILED(requestQiHr) || !requestVm) {
        return true;
    }

    ProbeUserList(requestVm);

    void* selectedObject = nullptr;
    HRESULT selectedHr =
        g_requestGetSelectedUserOrV1(
            requestVm,
            &selectedObject);

    reinterpret_cast<IUnknown*>(requestVm)->Release();
    requestVm = nullptr;

    ProbeLog(
        L"CHAIN Request.SelectedUserOrV1OrPLAPCredential hr=0x%08X object=%p class=\"%s\"",
        static_cast<unsigned>(selectedHr),
        selectedObject,
        RuntimeClassNameFromUnknown(
            reinterpret_cast<IUnknown*>(selectedObject)).c_str());

    if (FAILED(selectedHr) || !selectedObject) {
        if (selectedObject) {
            reinterpret_cast<IUnknown*>(selectedObject)->Release();
        }
        return true;
    }

    void* userVm = nullptr;
    HRESULT userQiHr =
        reinterpret_cast<IUnknown*>(selectedObject)->QueryInterface(
            g_userViewModelPublicIid,
            &userVm);

    ProbeLog(
        L"CHAIN selected object -> IUserViewModelPublicNonVirtuals QI hr=0x%08X iface=%p",
        static_cast<unsigned>(userQiHr),
        userVm);

    if (SUCCEEDED(userQiHr) && userVm) {
        LogUserViewModel(
            userVm,
            L"Root.RequestCredentials.SelectedUserOrV1OrPLAPCredential");

        reinterpret_cast<IUnknown*>(userVm)->Release();
        userVm = nullptr;
    }

    reinterpret_cast<IUnknown*>(selectedObject)->Release();
    selectedObject = nullptr;

    return true;
}

bool ProbeRootViewModelFromTree(
    FrameworkElement const& element,
    int depth) {

    if (!element || depth > 64) {
        return false;
    }

    try {
        auto dataContext =
            element.DataContext();

        if (dataContext) {
            auto raw =
                reinterpret_cast<IUnknown*>(
                    winrt::get_abi(dataContext));

            if (raw &&
                ProbeRootViewModelObject(
                    raw,
                    L"XAML DataContext")) {

                std::wstring elementName;
                auto name = element.Name();
                elementName.assign(
                    name.c_str(),
                    name.size());

                ProbeLog(
                    L"CHAIN root DataContext located depth=%d elementClass=\"%s\" elementName=\"%s\"",
                    depth,
                    SafeClassName(element).c_str(),
                    elementName.c_str());

                TryDumpLayoutOnce(
                    element);

                return true;
            }
        }

        const int count =
            Media::VisualTreeHelper::GetChildrenCount(
                element);

        for (int i = 0; i < count; ++i) {
            auto child =
                Media::VisualTreeHelper::GetChild(
                    element,
                    i).try_as<FrameworkElement>();

            if (child &&
                ProbeRootViewModelFromTree(
                    child,
                    depth + 1)) {
                return true;
            }
        }
    } catch (const winrt::hresult_error& e) {
        ProbeLog(
            L"CHAIN tree exception depth=%d hr=0x%08X",
            depth,
            static_cast<unsigned>(
                e.code().value));
    } catch (...) {
        ProbeLog(
            L"CHAIN tree non-WinRT exception depth=%d",
            depth);
    }

    return false;
}


void RememberXamlThread(
    DWORD threadId,
    bool preferred) {

    if (!threadId) {
        return;
    }

    AcquireSRWLockExclusive(
        &g_knownXamlThreadsLock);

    if (std::find(
            g_knownXamlThreads.begin(),
            g_knownXamlThreads.end(),
            threadId) ==
        g_knownXamlThreads.end()) {

        g_knownXamlThreads.push_back(
            threadId);
    }

    ReleaseSRWLockExclusive(
        &g_knownXamlThreadsLock);

    if (preferred) {
        g_preferredXamlThreadId.store(
            threadId,
            std::memory_order_release);
    }
}

std::vector<DWORD> SnapshotKnownXamlThreads() {
    std::vector<DWORD> result;

    AcquireSRWLockShared(
        &g_knownXamlThreadsLock);
    result =
        g_knownXamlThreads;
    ReleaseSRWLockShared(
        &g_knownXamlThreadsLock);

    return result;
}

BOOL CALLBACK EnumUiThreadWindow(
    HWND hwnd,
    LPARAM lParam) {

    auto* candidates =
        reinterpret_cast<
            std::vector<UiThreadCandidate>*>(
                lParam);

    if (!candidates) {
        return TRUE;
    }

    DWORD processId = 0;
    const DWORD threadId =
        GetWindowThreadProcessId(
            hwnd,
            &processId);

    if (!threadId ||
        processId !=
            GetCurrentProcessId()) {
        return TRUE;
    }

    RECT rect{};
    unsigned long long area = 0;

    if (GetWindowRect(
            hwnd,
            &rect)) {

        const LONG width =
            rect.right -
            rect.left;
        const LONG height =
            rect.bottom -
            rect.top;

        if (width > 0 &&
            height > 0) {

            area =
                static_cast<
                    unsigned long long>(
                        width) *
                static_cast<
                    unsigned long long>(
                        height);
        }
    }

    auto it =
        std::find_if(
            candidates->begin(),
            candidates->end(),
            [threadId](
                const UiThreadCandidate& c) {
                return c.threadId ==
                       threadId;
            });

    if (it ==
        candidates->end()) {

        candidates->push_back(
            {hwnd, threadId, area});
    } else if (
        !it->hwnd ||
        area > it->area) {

        it->hwnd = hwnd;
        it->area = area;
    }

    return TRUE;
}

std::vector<UiThreadCandidate>
CollectUiThreadCandidates() {

    std::vector<UiThreadCandidate>
        candidates;

    EnumWindows(
        EnumUiThreadWindow,
        reinterpret_cast<LPARAM>(
            &candidates));

    for (DWORD threadId :
         SnapshotKnownXamlThreads()) {

        auto it =
            std::find_if(
                candidates.begin(),
                candidates.end(),
                [threadId](
                    const UiThreadCandidate& c) {
                    return c.threadId ==
                           threadId;
                });

        if (it ==
            candidates.end()) {

            candidates.push_back(
                {nullptr, threadId, 0});
        }
    }

    const DWORD preferred =
        g_preferredXamlThreadId.load(
            std::memory_order_acquire);

    std::stable_sort(
        candidates.begin(),
        candidates.end(),
        [preferred](
            const UiThreadCandidate& a,
            const UiThreadCandidate& b) {

            const bool aPreferred =
                preferred &&
                a.threadId == preferred;
            const bool bPreferred =
                preferred &&
                b.threadId == preferred;

            if (aPreferred !=
                bPreferred) {
                return aPreferred;
            }

            return a.area >
                   b.area;
        });

    return candidates;
}

bool RefreshCurrentXamlWindowOnUiThread() {
    if (g_unloading.load(
            std::memory_order_acquire)) {
        return false;
    }

    try {
        auto window =
            Window::Current();

        if (!window) {
            return false;
        }

        auto root =
            window.Content()
                .try_as<FrameworkElement>();

        if (!root) {
            return false;
        }

        RememberXamlThread(
            GetCurrentThreadId(),
            false);

        TryDumpLayoutOnce(
            root);

        // A WTS/bridge event can rendezvous with a newly-created XAML root
        // before SystemProvider::_OnLoaded fires (or without it firing again at
        // all). Ensure that root owns both retained repair/observer bridges.
        AttachInitialLayoutRetry(
            root);
        AttachTransientStatusRootObserver(
            root);
        EnsureTransientFastUiBridgeWindow();

        return true;
    } catch (...) {
        return false;
    }
}

void DetachUiRendezvousHooks() {
    if (HHOOK hook =
            g_uiRendezvousHook.exchange(
                nullptr,
                std::memory_order_acq_rel)) {

        UnhookWindowsHookEx(
            hook);
    }

    if (HHOOK hook =
            g_uiRendezvousCallWndHook.exchange(
                nullptr,
                std::memory_order_acq_rel)) {

        UnhookWindowsHookEx(
            hook);
    }
}

bool TryHandleUiRendezvous(
    UINT message,
    WPARAM wParam) {

    if (!g_uiRendezvousPending.load(
            std::memory_order_acquire) ||
        message !=
            g_uiRendezvousMessage ||
        GetCurrentThreadId() !=
            g_uiRendezvousThreadId.load(
                std::memory_order_acquire) ||
        static_cast<ULONG_PTR>(wParam) !=
            g_uiRendezvousCookie.load(
                std::memory_order_acquire)) {

        return false;
    }

    bool expected = true;

    if (!g_uiRendezvousPending.compare_exchange_strong(
            expected,
            false,
            std::memory_order_acq_rel)) {

        return false;
    }

    bool matched = false;

    const auto action =
        static_cast<UiRendezvousAction>(
            g_uiRendezvousAction.load(
                std::memory_order_acquire));

    if (action ==
        UiRendezvousAction::Refresh) {

        matched =
            RefreshCurrentXamlWindowOnUiThread();
    } else if (
        action ==
        UiRendezvousAction::TransientFast) {

        matched =
            RefreshNativeTransientFastOnUiThread();
    } else if (
        action ==
        UiRendezvousAction::Cleanup) {

        matched =
            CleanupCurrentXamlWindowOnUiThread();
    }

    g_uiRendezvousMatched.store(
        matched,
        std::memory_order_release);

    if (g_uiRendezvousCompletedEvent) {
        SetEvent(
            g_uiRendezvousCompletedEvent);
    }

    return true;
}

LRESULT CALLBACK UiRendezvousGetMsgProc(
    int code,
    WPARAM wParam,
    LPARAM lParam) {

    bool handled = false;

    if (code >= 0 &&
        lParam) {

        const auto* message =
            reinterpret_cast<const MSG*>(
                lParam);

        handled =
            TryHandleUiRendezvous(
                message->message,
                message->wParam);
    }

    const LRESULT next =
        CallNextHookEx(
            nullptr,
            code,
            wParam,
            lParam);

    if (handled) {
        DetachUiRendezvousHooks();
    }

    return next;
}

LRESULT CALLBACK UiRendezvousCallWndProc(
    int code,
    WPARAM wParam,
    LPARAM lParam) {

    bool handled = false;

    if (code >= 0 &&
        lParam) {

        const auto* message =
            reinterpret_cast<const CWPSTRUCT*>(
                lParam);

        handled =
            TryHandleUiRendezvous(
                message->message,
                message->wParam);
    }

    const LRESULT next =
        CallNextHookEx(
            nullptr,
            code,
            wParam,
            lParam);

    if (handled) {
        DetachUiRendezvousHooks();
    }

    return next;
}

bool EnsureUiRendezvousObjects() {
    if (!g_uiRendezvousMessage) {
        g_uiRendezvousMessage =
            RegisterWindowMessageW(
                L"Windhawk.LogonUI.Tweaks.UserStatus.UiRendezvous.v1");

        if (!g_uiRendezvousMessage) {
            ProbeLog(
                L"UI rendezvous RegisterWindowMessage failed: %lu",
                GetLastError());
            return false;
        }
    }

    if (!g_uiRendezvousCompletedEvent) {
        g_uiRendezvousCompletedEvent =
            CreateEventW(
                nullptr,
                TRUE,
                FALSE,
                nullptr);

        if (!g_uiRendezvousCompletedEvent) {
            ProbeLog(
                L"UI rendezvous CreateEvent failed: %lu",
                GetLastError());
            return false;
        }
    }

    return true;
}

unsigned RequestUiRendezvousAction(
    UiRendezvousAction action,
    DWORD timeoutMs,
    bool stopAfterFirstMatch = false) {

    if (!EnsureUiRendezvousObjects()) {
        return 0;
    }

    auto candidates =
        CollectUiThreadCandidates();

    unsigned matchedCount = 0;

    for (const auto& candidate :
         candidates) {

        if (!candidate.threadId) {
            continue;
        }

        if ((action ==
                 UiRendezvousAction::Refresh ||
             action ==
                 UiRendezvousAction::TransientFast) &&
            g_unloading.load(
                std::memory_order_acquire)) {
            break;
        }

        if (GetCurrentThreadId() ==
            candidate.threadId) {

            bool matched = false;

            if (action ==
                UiRendezvousAction::Refresh) {
                matched =
                    RefreshCurrentXamlWindowOnUiThread();
            } else if (
                action ==
                UiRendezvousAction::TransientFast) {
                matched =
                    RefreshNativeTransientFastOnUiThread();
            } else if (
                action ==
                UiRendezvousAction::Cleanup) {
                matched =
                    CleanupCurrentXamlWindowOnUiThread();
            }

            if (matched) {
                ++matchedCount;
                if (stopAfterFirstMatch) {
                    break;
                }
            }

            continue;
        }

        ResetEvent(
            g_uiRendezvousCompletedEvent);

        g_uiRendezvousMatched.store(
            false,
            std::memory_order_release);

        g_uiRendezvousThreadId.store(
            candidate.threadId,
            std::memory_order_release);

        g_uiRendezvousAction.store(
            static_cast<int>(
                action),
            std::memory_order_release);

        ULONG_PTR cookie =
            g_uiRendezvousCookieCounter.fetch_add(
                1,
                std::memory_order_acq_rel);

        if (cookie == 0) {
            cookie =
                g_uiRendezvousCookieCounter.fetch_add(
                    1,
                    std::memory_order_acq_rel);
        }

        g_uiRendezvousCookie.store(
            cookie,
            std::memory_order_release);

        g_uiRendezvousPending.store(
            true,
            std::memory_order_release);

        HHOOK getMsgHook =
            SetWindowsHookExW(
                WH_GETMESSAGE,
                UiRendezvousGetMsgProc,
                nullptr,
                candidate.threadId);

        HHOOK callWndHook =
            SetWindowsHookExW(
                WH_CALLWNDPROC,
                UiRendezvousCallWndProc,
                nullptr,
                candidate.threadId);

        if (!getMsgHook &&
            !callWndHook) {

            g_uiRendezvousPending.store(
                false,
                std::memory_order_release);

            ProbeLog(
                L"UI rendezvous couldn't install GETMESSAGE/CALLWNDPROC hooks "
                L"thread=%lu error=%lu",
                candidate.threadId,
                GetLastError());

            continue;
        }

        if (HHOOK oldHook =
                g_uiRendezvousHook.exchange(
                    getMsgHook,
                    std::memory_order_acq_rel)) {

            UnhookWindowsHookEx(
                oldHook);
        }

        if (HHOOK oldHook =
                g_uiRendezvousCallWndHook.exchange(
                    callWndHook,
                    std::memory_order_acq_rel)) {

            UnhookWindowsHookEx(
                oldHook);
        }

        bool posted = false;

        if (candidate.hwnd) {
            if (PostMessageW(
                    candidate.hwnd,
                    g_uiRendezvousMessage,
                    static_cast<WPARAM>(
                        cookie),
                    0)) {

                posted = true;
            }

            // Important for nested LogonUI/IME message pumps: this path is
            // observable by WH_CALLWNDPROC even when ordinary queued messages
            // are delayed or filtered.
            if (SendNotifyMessageW(
                    candidate.hwnd,
                    g_uiRendezvousMessage,
                    static_cast<WPARAM>(
                        cookie),
                    0)) {

                posted = true;
            }
        }

        if (PostThreadMessageW(
                candidate.threadId,
                g_uiRendezvousMessage,
                static_cast<WPARAM>(
                    cookie),
                0)) {

            posted = true;
        }

        if (!posted) {
            g_uiRendezvousPending.store(
                false,
                std::memory_order_release);
            DetachUiRendezvousHooks();
            continue;
        }

        const DWORD wait =
            WaitForSingleObject(
                g_uiRendezvousCompletedEvent,
                timeoutMs);

        const bool rendezvousMatched =
            wait == WAIT_OBJECT_0 &&
            g_uiRendezvousMatched.load(
                std::memory_order_acquire);

        if (rendezvousMatched) {
            ++matchedCount;
        }

        g_uiRendezvousPending.store(
            false,
            std::memory_order_release);

        DetachUiRendezvousHooks();

        if (rendezvousMatched &&
            stopAfterFirstMatch) {
            break;
        }
    }

    g_uiRendezvousThreadId.store(
        0,
        std::memory_order_release);

    g_uiRendezvousCookie.store(
        0,
        std::memory_order_release);

    g_uiRendezvousAction.store(
        static_cast<int>(
            UiRendezvousAction::None),
        std::memory_order_release);

    return matchedCount;
}

void ProbeCurrentVisualTree(void* sender) {
    if (!sender ||
        g_unloading.load(std::memory_order_acquire)) {
        return;
    }

    try {
        winrt::Windows::Foundation::IUnknown unknown;
        winrt::copy_from_abi(unknown, sender);

        auto senderElement =
            unknown.try_as<FrameworkElement>();

        if (!senderElement) {
            ProbeLog(
                L"CHAIN couldn't project SystemProvider sender as FrameworkElement");
            return;
        }

        auto root =
            FindVisualRoot(senderElement);

        if (!root) {
            ProbeLog(
                L"CHAIN visual root unavailable");
            return;
        }

        ProbeLog(
            L"CHAIN scan begin senderClass=\"%s\" rootClass=\"%s\"",
            SafeClassName(senderElement).c_str(),
            SafeClassName(root).c_str());

        if (!ProbeRootViewModelFromTree(
                root,
                0)) {
            ProbeLog(
                L"CHAIN no RootViewModel DataContext found");

            // Even if the VM chain isn't ready yet, attempt the strictly
            // fail-closed visual refresh.
            TryDumpLayoutOnce(
                root);
        }

        // _OnLoaded commonly precedes UsersListView item realization. The
        // WTS_SESSION_LOCK event can also arrive before these rows exist.
        // LayoutUpdated gives us the native XAML realization event we need for
        // a bounded initial retry without enabling steady-state polling.
        AttachInitialLayoutRetry(
            root);

        // alpha4j observes only Microsoft's own StatusMessage TextBlock. This
        // is independent of the private UserViewModel getter chain.
        AttachTransientStatusRootObserver(
            root);

        ProbeLog(
            L"CHAIN scan complete");

    } catch (const winrt::hresult_error& e) {
        ProbeLog(
            L"CHAIN top-level WinRT exception hr=0x%08X",
            static_cast<unsigned>(
                e.code().value));
    } catch (...) {
        ProbeLog(
            L"CHAIN top-level non-WinRT exception");
    }
}







bool PrepareInternalAbiTargets(HMODULE module) {
    if (!module) {
        return false;
    }

    DWORD timestamp = 0;
    DWORD sizeOfImage = 0;

    if (!GetPeIdentity(
            module,
            &timestamp,
            &sizeOfImage)) {
        return false;
    }

    if (timestamp != kFastTimeDateStamp ||
        sizeOfImage != kFastSizeOfImage) {

        ProbeLog(
            L"User-list module currently knows the private UserViewModel IID/getter table for "
            L"TimeDateStamp=0x%08X SizeOfImage=0x%08X; got 0x%08X/0x%08X",
            kFastTimeDateStamp,
            kFastSizeOfImage,
            timestamp,
            sizeOfImage);
        return false;
    }

    auto base =
        reinterpret_cast<BYTE*>(module);

    g_getDisplayName =
        reinterpret_cast<GetHStringAbi>(
            base + kRvaGetDisplayName);
    g_getEmailAddress =
        reinterpret_cast<GetHStringAbi>(
            base + kRvaGetEmailAddress);
    g_getIsDomainNameVisible =
        reinterpret_cast<GetBoolAbi>(
            base + kRvaGetIsDomainNameVisible);
    g_getIsEmailAddressVisible =
        reinterpret_cast<GetBoolAbi>(
            base + kRvaGetIsEmailAddressVisible);
    g_getIsLoggedIn =
        reinterpret_cast<GetBoolAbi>(
            base + kRvaGetIsLoggedIn);
    g_getIsLogonStatusVisible =
        reinterpret_cast<GetBoolAbi>(
            base + kRvaGetIsLogonStatusVisible);
    g_getLogonStatus =
        reinterpret_cast<GetHStringAbi>(
            base + kRvaGetLogonStatus);
    g_getQualifiedName =
        reinterpret_cast<GetHStringAbi>(
            base + kRvaGetQualifiedName);
    g_getSmallUserTileImage =
        reinterpret_cast<GetObjectAbi>(
            base + kRvaGetSmallUserTileImage);
    g_userTileImageGetBitmapImage =
        reinterpret_cast<GetObjectAbi>(
            base + kRvaUserTileImageGetBitmapImage);
    g_rootGetRequestCredentialsVm =
        reinterpret_cast<GetObjectAbi>(
            base + kRvaRootGetRequestCredentialsVm);
    g_requestGetSelectedUserOrV1 =
        reinterpret_cast<GetObjectAbi>(
            base + kRvaRequestGetSelectedUserOrV1);
    g_requestGetUserAndV1List =
        reinterpret_cast<GetObjectAbi>(
            base + kRvaRequestGetUserAndV1List);

    if (!IsExecutableAddress(
            reinterpret_cast<void*>(g_getDisplayName)) ||
        !IsExecutableAddress(
            reinterpret_cast<void*>(g_getEmailAddress)) ||
        !IsExecutableAddress(
            reinterpret_cast<void*>(g_getIsDomainNameVisible)) ||
        !IsExecutableAddress(
            reinterpret_cast<void*>(g_getIsEmailAddressVisible)) ||
        !IsExecutableAddress(
            reinterpret_cast<void*>(g_getIsLoggedIn)) ||
        !IsExecutableAddress(
            reinterpret_cast<void*>(g_getIsLogonStatusVisible)) ||
        !IsExecutableAddress(
            reinterpret_cast<void*>(g_getLogonStatus)) ||
        !IsExecutableAddress(
            reinterpret_cast<void*>(g_getQualifiedName)) ||
        !IsExecutableAddress(
            reinterpret_cast<void*>(g_getSmallUserTileImage)) ||
        !IsExecutableAddress(
            reinterpret_cast<void*>(g_userTileImageGetBitmapImage)) ||
        !IsExecutableAddress(
            reinterpret_cast<void*>(g_rootGetRequestCredentialsVm)) ||
        !IsExecutableAddress(
            reinterpret_cast<void*>(g_requestGetSelectedUserOrV1)) ||
        !IsExecutableAddress(
            reinterpret_cast<void*>(g_requestGetUserAndV1List))) {

        ProbeLog(
            L"ABI getter executable-page validation failed");
        return false;
    }

    auto ReadGuidAt = [&](uintptr_t rva, GUID* outGuid) -> bool {
        if (!outGuid) {
            return false;
        }

        const GUID* iid =
            reinterpret_cast<const GUID*>(
                base + rva);

        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(
                iid,
                &mbi,
                sizeof(mbi)) ||
            mbi.State != MEM_COMMIT ||
            (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {

            return false;
        }

        *outGuid = *iid;

        const GUID zeroGuid{};
        return !IsEqualGUID(
            *outGuid,
            zeroGuid);
    };

    if (!ReadGuidAt(
            kRvaRootViewModelPublicIid,
            &g_rootViewModelPublicIid) ||
        !ReadGuidAt(
            kRvaRequestCredentialVmPublicIid,
            &g_requestCredentialVmPublicIid) ||
        !ReadGuidAt(
            kRvaUserViewModelPublicIid,
            &g_userViewModelPublicIid) ||
        !ReadGuidAt(
            kRvaUserTileImageVmPublicIid,
            &g_userTileImageVmPublicIid)) {

        ProbeLog(
            L"ABI one or more VM-chain IID reads failed");
        return false;
    }

    g_haveVmChainIids = true;

    wchar_t rootIidText[64]{};
    wchar_t requestIidText[64]{};
    wchar_t userIidText[64]{};
    wchar_t userTileIidText[64]{};

    StringFromGUID2(
        g_rootViewModelPublicIid,
        rootIidText,
        _countof(rootIidText));
    StringFromGUID2(
        g_requestCredentialVmPublicIid,
        requestIidText,
        _countof(requestIidText));
    StringFromGUID2(
        g_userViewModelPublicIid,
        userIidText,
        _countof(userIidText));
    StringFromGUID2(
        g_userTileImageVmPublicIid,
        userTileIidText,
        _countof(userTileIidText));

    ProbeLog(
        L"ABI VM chain ready RootIID=%s RequestIID=%s UserIID=%s UserTileIID=%s",
        rootIidText,
        requestIidText,
        userIidText,
        userTileIidText);

    ProbeLog(
        L"ABI getters Root.RequestCredentials=0x%llX "
        L"Request.SelectedUserOrV1=0x%llX Request.UserAndV1List=0x%llX "
        L"DisplayName=0x%llX EmailAddress=0x%llX "
        L"IsDomainNameVisible=0x%llX IsEmailAddressVisible=0x%llX "
        L"QualifiedName=0x%llX IsLoggedIn=0x%llX "
        L"IsLogonStatusVisible=0x%llX LogonStatus=0x%llX "
        L"SmallUserTileImage=0x%llX Tile.BitmapImage=0x%llX",
        static_cast<unsigned long long>(
            kRvaRootGetRequestCredentialsVm),
        static_cast<unsigned long long>(
            kRvaRequestGetSelectedUserOrV1),
        static_cast<unsigned long long>(
            kRvaRequestGetUserAndV1List),
        static_cast<unsigned long long>(
            kRvaGetDisplayName),
        static_cast<unsigned long long>(
            kRvaGetEmailAddress),
        static_cast<unsigned long long>(
            kRvaGetIsDomainNameVisible),
        static_cast<unsigned long long>(
            kRvaGetIsEmailAddressVisible),
        static_cast<unsigned long long>(
            kRvaGetQualifiedName),
        static_cast<unsigned long long>(
            kRvaGetIsLoggedIn),
        static_cast<unsigned long long>(
            kRvaGetIsLogonStatusVisible),
        static_cast<unsigned long long>(
            kRvaGetLogonStatus),
        static_cast<unsigned long long>(
            kRvaGetSmallUserTileImage),
        static_cast<unsigned long long>(
            kRvaUserTileImageGetBitmapImage));

    return true;
}




bool EnsurePrivateProcessWindowClass(
    const wchar_t* className,
    WNDPROC wndProc) {

    if (!className ||
        !*className ||
        !wndProc) {
        return false;
    }

    HINSTANCE owner =
        GetModuleHandleW(
            nullptr);

    WNDCLASSEXW existing{};
    existing.cbSize =
        sizeof(existing);

    if (GetClassInfoExW(
            owner,
            className,
            &existing)) {

        if (existing.lpfnWndProc ==
            wndProc) {
            return true;
        }

        ProbeLog(
            L"SESSION_EVENT class %s exists with stale/different WNDPROC; "
            L"attempting safe unregister",
            className);

        if (!UnregisterClassW(
                className,
                owner)) {

            ProbeLog(
                L"SESSION_EVENT refusing stale window class %s error=%lu",
                className,
                GetLastError());
            return false;
        }
    }

    WNDCLASSEXW wc{};
    wc.cbSize =
        sizeof(wc);
    wc.lpfnWndProc =
        wndProc;
    wc.hInstance =
        owner;
    wc.lpszClassName =
        className;

    if (!RegisterClassExW(
            &wc)) {

        ProbeLog(
            L"SESSION_EVENT RegisterClassEx failed class=%s error=%lu",
            className,
            GetLastError());
        return false;
    }

    return true;
}

void UnregisterPrivateProcessWindowClass(
    const wchar_t* className,
    WNDPROC wndProc) {

    if (!className ||
        !*className ||
        !wndProc) {
        return;
    }

    HINSTANCE owner =
        GetModuleHandleW(
            nullptr);

    WNDCLASSEXW existing{};
    existing.cbSize =
        sizeof(existing);

    if (!GetClassInfoExW(
            owner,
            className,
            &existing)) {
        return;
    }

    if (existing.lpfnWndProc !=
        wndProc) {

        ProbeLog(
            L"SESSION_EVENT refusing to unregister class=%s because WNDPROC is not ours",
            className);
        return;
    }

    if (!UnregisterClassW(
            className,
            owner)) {

        const DWORD error =
            GetLastError();

        if (error !=
            ERROR_CLASS_DOES_NOT_EXIST) {

            ProbeLog(
                L"SESSION_EVENT UnregisterClass failed class=%s error=%lu",
                className,
                error);
        }
    }
}

LRESULT CALLBACK SessionNotifyWndProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam) {

    if (message ==
        WM_WTSSESSION_CHANGE) {

        const DWORD eventSessionId =
            static_cast<DWORD>(lParam);

        if (wParam == WTS_SESSION_LOCK) {
            SetWtsLockEventState(
                eventSessionId,
                WtsLockState::Locked);
        } else if (wParam == WTS_SESSION_UNLOCK) {
            SetWtsLockEventState(
                eventSessionId,
                WtsLockState::Unlocked);
        } else if (wParam == WTS_SESSION_LOGON ||
                   wParam == WTS_SESSION_CREATE ||
                   wParam == WTS_SESSION_LOGOFF ||
                   wParam == WTS_SESSION_TERMINATE) {
            // Session IDs can be reused. Do not carry a previous incarnation's
            // lock state into a new WTS session.
            ForgetWtsLockEventState(
                eventSessionId);

            // Do NOT retire the transient bridge at LOGOFF/TERMINATE. Runtime
            // traces proved that Windows can publish its exact native
            // "Signing out" text *after* WTS_SESSION_LOGOFF, from a replacement
            // LogonUI process whose WTS username/protocol fields are already
            // blank/downgraded. The last proven identity therefore has to live
            // through that terminal visual. A CREATE event is the safe hard
            // boundary for a new incarnation of the numeric session ID.
            if (wParam == WTS_SESSION_CREATE) {
                ClearTransientBridgeSession(
                    eventSessionId,
                    L"WTS_SESSION_CREATE/new incarnation");
            }
        }

        ProbeLog(
            L"SESSION_EVENT WM_WTSSESSION_CHANGE event=%llu session=%llu",
            static_cast<unsigned long long>(
                wParam),
            static_cast<unsigned long long>(
                lParam));
        // Separate live diagnostic sample. Never overwrites alpha4v's
        // process-local WTS row snapshot or renders a changed status.
        QueueVmCorrelationSample(2, eventSessionId,
            static_cast<unsigned>(wParam));

        const unsigned repairGeneration =
            g_layoutRepairGeneration.fetch_add(
                1,
                std::memory_order_acq_rel) + 1;
        InvalidateRecentCleanPresenterPass();

        ProbeLog(
            L"LIFECYCLE WTS event marked user-row repair generation=%u",
            repairGeneration);

        if (g_refreshWakeEvent) {
            SetEvent(
                g_refreshWakeEvent);
        }

        return 0;
    }

    return DefWindowProcW(
        hwnd,
        message,
        wParam,
        lParam);
}

bool CreateSessionNotificationWindowOnWorkerThread() {
    if (!EnsurePrivateProcessWindowClass(
            kSessionNotifyWindowClass,
            SessionNotifyWndProc)) {

        return false;
    }

    HINSTANCE owner =
        GetModuleHandleW(
            nullptr);

    g_sessionNotifyWindow =
        CreateWindowExW(
            0,
            kSessionNotifyWindowClass,
            L"",
            0,
            0, 0, 0, 0,
            HWND_MESSAGE,
            nullptr,
            owner,
            nullptr);

    if (!g_sessionNotifyWindow) {
        ProbeLog(
            L"SESSION_EVENT CreateWindowEx failed error=%lu",
            GetLastError());

        UnregisterPrivateProcessWindowClass(
            kSessionNotifyWindowClass,
            SessionNotifyWndProc);

        return false;
    }

    if (!WTSRegisterSessionNotification(
            g_sessionNotifyWindow,
            NOTIFY_FOR_ALL_SESSIONS)) {

        ProbeLog(
            L"SESSION_EVENT WTSRegisterSessionNotification failed error=%lu; "
            L"falling back to periodic safety refresh",
            GetLastError());

        DestroyWindow(
            g_sessionNotifyWindow);
        g_sessionNotifyWindow =
            nullptr;

        UnregisterPrivateProcessWindowClass(
            kSessionNotifyWindowClass,
            SessionNotifyWndProc);

        return false;
    }

    g_sessionNotificationsActive.store(
        true,
        std::memory_order_release);

    ProbeLog(
        L"SESSION_EVENT registered WTS session notifications on worker thread=%lu",
        GetCurrentThreadId());

    return true;
}

void DestroySessionNotificationWindowOnWorkerThread() {
    if (g_sessionNotifyWindow) {
        if (g_sessionNotificationsActive.exchange(
                false,
                std::memory_order_acq_rel)) {

            WTSUnRegisterSessionNotification(
                g_sessionNotifyWindow);
        }

        DestroyWindow(
            g_sessionNotifyWindow);
        g_sessionNotifyWindow =
            nullptr;
    }

    UnregisterPrivateProcessWindowClass(
        kSessionNotifyWindowClass,
        SessionNotifyWndProc);
}

DWORD WINAPI WorkerThreadProc(void*) {
    ProbeLog(
        L"WORKER waiting for Windows.UI.Logon.dll");

    HMODULE module = nullptr;

    while (!g_unloading.load(
               std::memory_order_acquire)) {

        if (g_stopEvent &&
            WaitForSingleObject(
                g_stopEvent,
                10) != WAIT_TIMEOUT) {
            return 0;
        }

        module =
            GetModuleHandleW(
                kTargetModule);

        if (module) {
            break;
        }
    }

    if (!module ||
        g_unloading.load(
            std::memory_order_acquire)) {
        return 0;
    }

    DWORD timestamp = 0;
    DWORD sizeOfImage = 0;
    const bool havePeIdentity =
        GetPeIdentity(module, &timestamp, &sizeOfImage);

    if (havePeIdentity) {
        ProbeLog(
            L"MODULE Windows.UI.Logon.dll base=%p TimeDateStamp=0x%08X SizeOfImage=0x%08X",
            module, timestamp, sizeOfImage);
    }

    const bool transientValidated =
        havePeIdentity &&
        timestamp == kFastTimeDateStamp &&
        sizeOfImage == kFastSizeOfImage;
    g_transientFeatureValidated.store(
        transientValidated,
        std::memory_order_release);

    if (transientValidated &&
        g_showNativeTransientStatus.load(
            std::memory_order_acquire)) {
        if (!InitializeTransientBridge()) {
            ProbeLog(
                L"TRANSIENT_BRIDGE unavailable; exact native transient override will fail closed while steady-state status behavior remains active");
        }
    } else if (g_showNativeTransientStatus.load(
                   std::memory_order_acquire)) {
        ProbeLog(
            L"TRANSIENT_BRIDGE exact-native observer disabled: Windows.UI.Logon.dll identity is not the validated 0x%08X/0x%08X image",
            kFastTimeDateStamp,
            kFastSizeOfImage);
    }

    if (!PrepareInternalAbiTargets(module)) {
        ProbeLog(
            L"WORKER private UserViewModel ABI setup failed; user-list tweaks disabled while other LogonUI Tweaks features remain active");
        return 0;
    }

    g_abiReady.store(true, std::memory_order_release);

    const bool haveSessionNotifications =
        CreateSessionNotificationWindowOnWorkerThread();

    ProbeLog(
        L"WORKER private ABI ready via shared SystemProvider hook; update mode=%s",
        haveSessionNotifications
            ? L"event-driven WTS session notifications"
            : L"periodic safety refresh fallback");

    // Initial hot-attach. _OnLoaded remains armed for future XAML surfaces.
    RequestUiRendezvousAction(
        UiRendezvousAction::Refresh,
        900);

    std::vector<HANDLE> transientWriterWatches =
        OpenTransientWriterWatchesForConsoleReader();

    if (!transientWriterWatches.empty()) {
        ProbeLog(
            L"TRANSIENT_BRIDGE watching %u remote writer process(es) for event-driven death cleanup",
            static_cast<unsigned>(transientWriterWatches.size()));
    }

    bool stop = false;

    bool deferredStructuralRefresh = false;

    while (!stop &&
           !g_unloading.load(
               std::memory_order_acquire)) {

        int fallbackInterval =
            g_fallbackRefreshIntervalMs.load(
                std::memory_order_relaxed);

        if (fallbackInterval != 0) {
            fallbackInterval =
                ClampInt(
                    fallbackInterval,
                    1000,
                    60000);
        }

        DWORD timeout =
            fallbackInterval > 0
                ? static_cast<DWORD>(
                      fallbackInterval)
                : INFINITE;

        // If WTS event registration failed, don't leave the feature stale
        // forever even when the configured fallback is disabled.
        if (!haveSessionNotifications &&
            timeout == INFINITE) {
            timeout = 1000;
        }

        std::vector<HANDLE> waits;
        waits.reserve(
            3 + transientWriterWatches.size());
        waits.push_back(g_stopEvent);
        waits.push_back(g_refreshWakeEvent);

        const DWORD bridgeIndex =
            g_transientBridgeReaderEvent
                ? static_cast<DWORD>(waits.size())
                : MAXDWORD;
        if (g_transientBridgeReaderEvent) {
            waits.push_back(
                g_transientBridgeReaderEvent);
        }

        const DWORD writerWatchBegin =
            static_cast<DWORD>(waits.size());
        waits.insert(
            waits.end(),
            transientWriterWatches.begin(),
            transientWriterWatches.end());

        const DWORD waitCount =
            static_cast<DWORD>(waits.size());

        const DWORD wait =
            MsgWaitForMultipleObjects(
                waitCount,
                waits.data(),
                FALSE,
                timeout,
                QS_ALLINPUT);

        bool regularRefresh = false;
        bool transientFastRefresh = false;
        ULONGLONG transientWakeSerial = 0;
        ULONGLONG transientWakePrevious = 0;
        bool transientWakeCoalesced = false;

        if (wait ==
            WAIT_OBJECT_0) {
            stop = true;
            break;
        }

        if (wait ==
            WAIT_OBJECT_0 + 1) {
            regularRefresh = true;
        } else if (
            bridgeIndex != MAXDWORD &&
            wait == WAIT_OBJECT_0 + bridgeIndex) {

            const ULONGLONG serial =
                ReadTransientBridgeSerial();
            const ULONGLONG previous =
                g_lastTransientBridgeReadSerial.exchange(
                    serial,
                    std::memory_order_acq_rel);

            transientWakeSerial = serial;
            transientWakePrevious = previous;
            transientFastRefresh = true;
        } else if (
            wait >= WAIT_OBJECT_0 + writerWatchBegin &&
            wait < WAIT_OBJECT_0 + waitCount) {

            const DWORD writerIndex =
                wait - WAIT_OBJECT_0 - writerWatchBegin;
            ProbeLog(
                L"TRANSIENT_BRIDGE writer process signaled index=%lu; scheduling immediate stale-status cleanup",
                writerIndex);
            regularRefresh = true;
        } else if (
            wait ==
            WAIT_OBJECT_0 + waitCount) {

            MSG message{};

            while (PeekMessageW(
                    &message,
                    nullptr,
                    0,
                    0,
                    PM_REMOVE)) {

                if (message.message ==
                    WM_QUIT) {
                    stop = true;
                    break;
                }

                TranslateMessage(
                    &message);
                DispatchMessageW(
                    &message);
            }

            // SessionNotifyWndProc wakes g_refreshWakeEvent. Consume it here
            // so one session event maps to one structural/WTS refresh pass.
            if (g_refreshWakeEvent &&
                WaitForSingleObject(
                    g_refreshWakeEvent,
                    0) ==
                    WAIT_OBJECT_0) {

                regularRefresh = true;
            }
        } else if (
            wait ==
            WAIT_TIMEOUT) {

            regularRefresh = true;
        } else if (
            wait ==
            WAIT_FAILED) {

            ProbeLog(
                L"WORKER MsgWaitForMultipleObjects failed error=%lu",
                GetLastError());
            break;
        }

        if (stop ||
            g_unloading.load(
                std::memory_order_acquire)) {
            break;
        }

        // If a WTS/lifecycle wake raced a native bridge publication, queue the
        // current native text first. The full pass can then repair membership,
        // diagnostics, and steady-state semantics without making the transient
        // wait behind it.
        if (regularRefresh &&
            g_transientBridgeReaderEvent &&
            WaitForSingleObject(
                g_transientBridgeReaderEvent,
                0) == WAIT_OBJECT_0) {

            const ULONGLONG serial =
                ReadTransientBridgeSerial();
            const ULONGLONG previous =
                g_lastTransientBridgeReadSerial.exchange(
                    serial,
                    std::memory_order_acq_rel);

            transientWakeSerial = serial;
            transientWakePrevious = previous;
            transientWakeCoalesced = true;
            transientFastRefresh = true;
        }

        // Refresh the scheduling fence from the current shared bridge before
        // deciding whether a structural repair may run. The bridge serial is
        // global and monotonic; the newest live non-empty slot is the current
        // latency-sensitive native-status burst.
        const auto transientBurst =
            CaptureConsoleTransientBurstState();
        const bool wasBurstActive =
            g_transientBurstActive.exchange(
                transientBurst.active,
                std::memory_order_acq_rel);
        g_transientBurstSerial.store(
            transientBurst.bridgeSerial,
            std::memory_order_release);

        if (wasBurstActive != transientBurst.active &&
            g_enableLayoutDiagnostics.load(
                std::memory_order_relaxed)) {
            ProbeLog(
                L"TRANSIENT_BURST active=%u bridgeSerial=%llu newestTextSerial=%llu session=%lu writerPid=%lu",
                transientBurst.active ? 1u : 0u,
                static_cast<unsigned long long>(
                    transientBurst.bridgeSerial),
                static_cast<unsigned long long>(
                    transientBurst.newestTextSerial),
                transientBurst.sessionId,
                transientBurst.writerPid);
        }

        if (transientFastRefresh) {
            // Normal steady state uses a persistent message-only HWND that was
            // created on the validated LogonUX XAML thread. Posting to it is
            // deliberately fire-and-forget: the worker is immediately free to
            // receive the next native text change instead of blocking for a
            // full six-row/WTS traversal.
            bool queued =
                QueueTransientFastUiUpdate();

            if (!queued) {
                // Early startup fallback before the persistent UI bridge exists.
                // Still use only the status fast path and the preferred XAML
                // thread; never replay historical strings.
                const unsigned matched =
                    RequestUiRendezvousAction(
                        UiRendezvousAction::TransientFast,
                        75,
                        true);
                queued = matched != 0;

                if (!queued ||
                    g_transientFastNeedsFullRefresh.exchange(
                        false,
                        std::memory_order_acq_rel)) {
                    regularRefresh = true;
                }
            }

            // Diagnostic logging comes *after* PostMessage/rendezvous so shared
            // file I/O cannot delay the XAML update itself.
            ProbeLog(
                L"TRANSIENT_FAST dispatch serial=%llu previous=%llu coalesced=%u queued=%u persistentHwnd=%p",
                static_cast<unsigned long long>(transientWakeSerial),
                static_cast<unsigned long long>(transientWakePrevious),
                transientWakeCoalesced ? 1u : 0u,
                queued ? 1u : 0u,
                g_transientFastUiBridgeWindow.load(
                    std::memory_order_acquire));

            // A bridge publish can introduce a new remote writer. Rebuild the
            // small process-handle watch set immediately; no polling is used.
            CloseTransientWriterWatches(
                &transientWriterWatches);
            transientWriterWatches =
                OpenTransientWriterWatchesForConsoleReader();
        }

        // Do not run a six-row/WTS tree repair while Windows is actively
        // changing StatusMessage text. Those repairs were the reason alpha4n
        // could dispatch Group Policy promptly yet miss Local Session Manager
        // / System Event / Preparing Windows bursts. Remember the structural
        // request and perform one repair as soon as the native text clears.
        if (regularRefresh &&
            transientBurst.active) {
            if (!deferredStructuralRefresh) {
                ProbeLog(
                    L"STRUCTURAL_REFRESH deferred nativeTransient=1 bridgeSerial=%llu session=%lu writerPid=%lu",
                    static_cast<unsigned long long>(
                        transientBurst.bridgeSerial),
                    transientBurst.sessionId,
                    transientBurst.writerPid);
            }
            deferredStructuralRefresh = true;
            regularRefresh = false;
        } else if (
            !transientBurst.active &&
            deferredStructuralRefresh) {

            deferredStructuralRefresh = false;
            regularRefresh = true;
            ProbeLog(
                L"STRUCTURAL_REFRESH resumed after native transient clear bridgeSerial=%llu",
                static_cast<unsigned long long>(
                    transientBurst.bridgeSerial));
        }

        if (regularRefresh) {
            RequestUiRendezvousAction(
                UiRendezvousAction::Refresh,
                300,
                true);

            CloseTransientWriterWatches(
                &transientWriterWatches);
            transientWriterWatches =
                OpenTransientWriterWatchesForConsoleReader();
        }
    }

    CloseTransientWriterWatches(
        &transientWriterWatches);

    g_transientBurstActive.store(
        false,
        std::memory_order_release);
    g_transientBurstSerial.store(
        0,
        std::memory_order_release);

    DestroySessionNotificationWindowOnWorkerThread();

    ProbeLog(
        L"WORKER update loop stopped");

    return 0;
}


void LoadSettings() {
    const bool enabled =
        Wh_GetIntSetting(L"userList.enabled") != 0;

    const bool showStatus =
        Wh_GetIntSetting(
            L"userList.showStatus") != 0;

    const bool showNativeTransientStatus =
        Wh_GetIntSetting(
            L"userList.showNativeTransientStatus") != 0;

    const bool showPendingRemoteAttempt =
        Wh_GetIntSetting(
            L"userList.showPendingRemoteAttempt") != 0;

    const bool liveSessionRowUpdates =
        Wh_GetIntSetting(
            L"nonNativeUserList.liveSessionRowUpdates") != 0;
    const bool adaptiveRepairCoalescing =
        Wh_GetIntSetting(
            L"nonNativeUserList.adaptiveRepairCoalescing") != 0;
    const bool identityGuardedRowReuse =
        Wh_GetIntSetting(L"nonNativeUserList.identityGuardedRowReuse") != 0;

    const bool nativeVmPropertyProbe =
        Wh_GetIntSetting(L"diagnostics.vmPropertyProbe") != 0;
    const bool nativeVmStateCorrelation =
        Wh_GetIntSetting(L"diagnostics.vmStateCorrelation") != 0;
    const bool rowIdentityProbe =
        Wh_GetIntSetting(L"diagnostics.rowIdentityProbe") != 0;

    const bool customRowLayoutEnabled =
        Wh_GetIntSetting(L"rowLayout.enabled") != 0;
    PCWSTR ruleLines = Wh_GetStringSetting(L"rowLayout.lines");
    PCWSTR ruleOrder = Wh_GetStringSetting(L"rowLayout.order");
    PCWSTR customText = Wh_GetStringSetting(L"rowLayout.customText");
    PCWSTR customAccountTexts = Wh_GetStringSetting(L"rowLayout.customAccountTexts");
    auto rowLayoutPlan = ParseRowLayoutPlan(ruleLines, ruleOrder);
    auto customRowTextSettings = ParseCustomRowTexts(customText, customAccountTexts);
    Wh_FreeStringSetting(ruleLines);
    Wh_FreeStringSetting(ruleOrder);
    Wh_FreeStringSetting(customText);
    Wh_FreeStringSetting(customAccountTexts);

    PCWSTR statusBehaviorSetting =
        Wh_GetStringSetting(
            L"userList.statusBehavior");
    const StatusBehavior statusBehavior =
        ParseStatusBehavior(statusBehaviorSetting);
    Wh_FreeStringSetting(statusBehaviorSetting);

    const bool alwaysQualified =
        Wh_GetIntSetting(
            L"userList.alwaysShowQualifiedName") != 0;

    PCWSTR sessionDetailsSetting =
        Wh_GetStringSetting(
            L"userList.sessionDetails");
    const SessionDetailsMode sessionDetailsMode =
        ParseSessionDetailsMode(
            sessionDetailsSetting);
    Wh_FreeStringSetting(
        sessionDetailsSetting);

    // Keep all detail switches presentation-only. A single atomic mask keeps
    // a single row's Custom line selection consistent during each refresh.
    uint32_t customDetailFlags = 0;
    struct DetailSettingSpec { const wchar_t* key; uint32_t flag; };
    const DetailSettingSpec sourceSettings[] = {
        {L"customRowDetails.sessionId", kDetailSessionId},
        {L"customRowDetails.connection", kDetailConnection},
        {L"customRowDetails.wtsState", kDetailWtsState},
        {L"customRowDetails.station", kDetailStation},
        {L"customRowDetails.protocol", kDetailProtocol},
        {L"customRowDetails.lockState", kDetailLockState},
        {L"customRowDetails.vmLoggedIn", kDetailVmLoggedIn},
        {L"customRowDetails.vmStatusVisible", kDetailVmVisible},
        {L"customRowDetails.vmLogonStatus", kDetailVmLogonStatus},
    };
    for (const auto& sourceSetting : sourceSettings) {
        if (Wh_GetIntSetting(sourceSetting.key) != 0) {
            customDetailFlags |= sourceSetting.flag;
        }
    }

    int fallbackRefreshInterval =
        Wh_GetIntSetting(
            L"userList.fallbackRefreshIntervalMs");

    if (fallbackRefreshInterval != 0) {
        fallbackRefreshInterval =
            ClampInt(
                fallbackRefreshInterval,
                1000,
                60000);
    }

    const bool compactUnsigned =
        Wh_GetIntSetting(
            L"userList.compactUnsignedRows") != 0;

    int compactHeight =
        Wh_GetIntSetting(
            L"userList.compactUnsignedRowHeight");

    compactHeight =
        ClampInt(
            compactHeight,
            48,
            60);

    const bool layoutDiagnostics =
        g_enableVerboseDiagnostics.load(std::memory_order_relaxed);

    g_enabled.store(enabled, std::memory_order_release);
    g_showLogonStatusForAllLoggedIn.store(
        showStatus,
        std::memory_order_release);
    g_showNativeTransientStatus.store(
        showNativeTransientStatus,
        std::memory_order_release);
    g_showPendingRemoteAttempt.store(
        showPendingRemoteAttempt,
        std::memory_order_release);
    g_liveSessionRowUpdates.store(
        liveSessionRowUpdates,
        std::memory_order_release);
    g_adaptiveRepairCoalescing.store(
        adaptiveRepairCoalescing,
        std::memory_order_release);
    g_identityGuardedRowReuse.store(
        identityGuardedRowReuse, std::memory_order_release);
    std::atomic_store_explicit(&g_recentCleanPresenterPass,
        std::shared_ptr<const RecentCleanPresenterPass>{},
        std::memory_order_release);
    if (identityGuardedRowReuse) {
        ProbeLog(L"ROW_REUSE enabled: identity/VM guarded clean result reuse; both root traversals retained");
    }
    if (adaptiveRepairCoalescing) {
        ProbeLog(L"REPAIR_COALESCE enabled: adaptive clean-pass cooldown; generation guards and native transient priority unchanged");
    }
    g_vmPropertyProbeEnabled.store(
        nativeVmPropertyProbe || nativeVmStateCorrelation,
        std::memory_order_release);
    g_vmStateCorrelationEnabled.store(
        nativeVmStateCorrelation, std::memory_order_release);
    g_rowIdentityProbeEnabled.store(
        rowIdentityProbe, std::memory_order_release);
    {
        struct SnapshotSettingsLock {
            SnapshotSettingsLock() { AcquireSRWLockExclusive(&g_rowIdentitySnapshotsLock); }
            ~SnapshotSettingsLock() { ReleaseSRWLockExclusive(&g_rowIdentitySnapshotsLock); }
        } guard;
        g_rowIdentitySnapshots.clear();
    }
    ClearRowSessionSnapshot();
    if (rowIdentityProbe) {
        ProbeLog(L"ROW_IDENTITY enabled: bounded read-only presenter comparison; "
                 L"piggybacks on existing repairs; no new tree walks or timers");
    }
    if (nativeVmStateCorrelation) {
        ProbeLog(L"VM_STATE_CORRELATION enabled: deferred getter/WTS correlation (read-only)");
    }
    g_statusBehavior.store(
        statusBehavior,
        std::memory_order_release);
    g_customRowLayoutEnabled.store(customRowLayoutEnabled, std::memory_order_release);
    std::atomic_store_explicit(&g_rowLayoutPlan,
        std::move(rowLayoutPlan), std::memory_order_release);
    {
        std::lock_guard<std::mutex> guard(g_rowObservationMutex);
        g_rowFirstSeen.clear();
    }
    std::atomic_store_explicit(&g_customRowTextSettings,
        std::move(customRowTextSettings), std::memory_order_release);

    g_alwaysShowQualifiedName.store(
        alwaysQualified,
        std::memory_order_release);
    g_sessionDetailsMode.store(
        sessionDetailsMode,
        std::memory_order_release);
    g_customRowDetailFlags.store(
        customDetailFlags,
        std::memory_order_release);
    if (sessionDetailsMode != SessionDetailsMode::Off) {
        g_sessionDetailsEverEnabled.store(
            true,
            std::memory_order_release);
    }
    g_fallbackRefreshIntervalMs.store(
        fallbackRefreshInterval,
        std::memory_order_release);
    g_compactUnsignedRows.store(
        compactUnsigned,
        std::memory_order_release);
    if (compactUnsigned) {
        g_compactRowsEverEnabled.store(
            true,
            std::memory_order_release);
    }
    g_compactUnsignedRowHeight.store(
        compactHeight,
        std::memory_order_release);
    g_enableLayoutDiagnostics.store(
        layoutDiagnostics,
        std::memory_order_release);

    const unsigned generation =
        g_settingsGeneration.fetch_add(
            1,
            std::memory_order_acq_rel) +
        1;

    ProbeLog(
        L"SETTINGS generation=%u enabled=%u showStatus=%u nativeTransient=%u pendingRemoteAttempt=%u liveSessionRows=%u statusBehavior=%s "
        L"alwaysQualified=%u customRowLayout=%u vmPropertyProbe=%u rowLayoutRules=%u sessionDetails=%s customDetailMask=0x%03X fallbackRefreshIntervalMs=%d compactUnsignedRows=%u "
        L"compactUnsignedRowHeight=%d layoutDiagnostics=%u adaptiveRepair=%u rowIdentityProbe=%u identityReuse=%u",
        generation,
        enabled ? 1u : 0u,
        showStatus ? 1u : 0u,
        showNativeTransientStatus ? 1u : 0u,
        showPendingRemoteAttempt ? 1u : 0u,
        liveSessionRowUpdates ? 1u : 0u,
        StatusBehaviorName(statusBehavior),
        alwaysQualified ? 1u : 0u,
        customRowLayoutEnabled ? 1u : 0u,
        (nativeVmPropertyProbe || nativeVmStateCorrelation) ? 1u : 0u,
        static_cast<unsigned>(std::atomic_load_explicit(
            &g_rowLayoutPlan, std::memory_order_acquire)->rules.size()),
        SessionDetailsModeName(sessionDetailsMode),
        customDetailFlags,
        fallbackRefreshInterval,
        compactUnsigned ? 1u : 0u,
        compactHeight,
        layoutDiagnostics ? 1u : 0u,
        adaptiveRepairCoalescing ? 1u : 0u,
        rowIdentityProbe ? 1u : 0u,
        identityGuardedRowReuse ? 1u : 0u);

}


[[maybe_unused]] void OnSystemProviderUnloaded() {
    if (!g_enabled.load(std::memory_order_acquire) ||
        g_unloading.load(std::memory_order_acquire)) {
        return;
    }

    const unsigned generation =
        g_layoutRepairGeneration.fetch_add(
            1,
            std::memory_order_acq_rel) + 1;

    InvalidateRecentCleanPresenterPass();
    ProbeLog(
        L"LIFECYCLE SystemProvider Unloaded marked user-row repair generation=%u",
        generation);
}

void OnSystemProviderLoaded(void* sender) {
    if (!sender ||
        !g_enabled.load(std::memory_order_acquire) ||
        g_unloading.load(std::memory_order_acquire)) {
        return;
    }

    RememberXamlThread(GetCurrentThreadId(), true);
    // A native SystemProvider reload must discard old repair results.
    InvalidateRecentCleanPresenterPass();

    // Create the persistent status-only dispatch target on the same XAML thread
    // that Windows just proved owns SystemProvider. This removes per-message
    // hook/rendezvous setup from the normal transient path.
    EnsureTransientFastUiBridgeWindow();

    // The native transient observer uses only safe XAML projection. Attach it
    // even if the build-specific UserViewModel ABI worker hasn't completed yet.
    AttachTransientStatusFromSystemProviderSender(
        sender);

    if (!g_abiReady.load(std::memory_order_acquire)) {
        return;
    }

    if (g_userListReady.load(
            std::memory_order_acquire)) {

        const unsigned generation =
            g_layoutRepairGeneration.fetch_add(
                1,
                std::memory_order_acq_rel) + 1;

        ProbeLog(
            L"LIFECYCLE SystemProvider Loaded marked user-row repair generation=%u",
            generation);
    }

    ProbeLog(L"shared SystemProvider::_OnLoaded dispatch thread=%lu sender=%p",
             GetCurrentThreadId(), sender);
    ProbeCurrentVisualTree(sender);
}


BOOL Initialize() {
    g_unloading.store(
        false,
        std::memory_order_release);
    g_transientFeatureValidated.store(
        false,
        std::memory_order_release);

    LoadSettings();

    if (!g_enabled.load(std::memory_order_acquire)) {
        ProbeLog(L"disabled by setting; private UserViewModel ABI will not be touched");
        return TRUE;
    }

    ProbeLog(L"initializing merged user-list status module");

    DWORD build = 0;
    DWORD ubr = 0;

    if (GetOsBuild(&build, &ubr)) {
        ProbeLog(
            L"OS Windows build=%lu.%lu",
            build,
            ubr);
    }

    DWORD currentSession =
        0xFFFFFFFF;

    ProcessIdToSessionId(
        GetCurrentProcessId(),
        &currentSession);

    ProbeLog(
        L"PROCESS pid=%lu session=%lu SM_REMOTESESSION=%d activeConsoleSession=%lu",
        GetCurrentProcessId(),
        currentSession,
        GetSystemMetrics(
            SM_REMOTESESSION),
        WTSGetActiveConsoleSessionId());

    ResolveWinStringApis();

    g_stopEvent =
        CreateEventW(
            nullptr,
            TRUE,
            FALSE,
            nullptr);

    g_refreshWakeEvent =
        CreateEventW(
            nullptr,
            FALSE,
            FALSE,
            nullptr);

    if (!g_stopEvent ||
        !g_refreshWakeEvent ||
        !EnsureUiRendezvousObjects()) {

        ProbeLog(
            L"INIT event/rendezvous setup failed: %lu",
            GetLastError());

        if (g_refreshWakeEvent) {
            CloseHandle(
                g_refreshWakeEvent);
            g_refreshWakeEvent =
                nullptr;
        }

        if (g_stopEvent) {
            CloseHandle(
                g_stopEvent);
            g_stopEvent =
                nullptr;
        }

        if (g_uiRendezvousCompletedEvent) {
            CloseHandle(
                g_uiRendezvousCompletedEvent);
            g_uiRendezvousCompletedEvent =
                nullptr;
        }

        return FALSE;
    }

    g_workerThread =
        CreateThread(
            nullptr,
            0,
            WorkerThreadProc,
            nullptr,
            0,
            nullptr);

    if (!g_workerThread) {
        ProbeLog(
            L"INIT CreateThread failed: %lu",
            GetLastError());

        CloseHandle(
            g_uiRendezvousCompletedEvent);
        g_uiRendezvousCompletedEvent =
            nullptr;

        CloseHandle(
            g_refreshWakeEvent);
        g_refreshWakeEvent =
            nullptr;

        CloseHandle(
            g_stopEvent);
        g_stopEvent =
            nullptr;

        ShutdownTransientBridge();
        return FALSE;
    }

    return TRUE;
}

void BeforeUninit() {
    g_unloading.store(
        true,
        std::memory_order_release);
    g_abiReady.store(false, std::memory_order_release);

    if (!g_enabled.load(std::memory_order_acquire) && !g_workerThread) {
        return;
    }

    ProbeLog(
        L"BeforeUninit: stopping worker and restoring patched XAML where possible");

    if (g_stopEvent) {
        SetEvent(
            g_stopEvent);
    }

    if (g_refreshWakeEvent) {
        SetEvent(
            g_refreshWakeEvent);
    }

    // Stop the producer first so it can't race the cleanup rendezvous.
    if (g_workerThread) {
        WaitForSingleObject(
            g_workerThread,
            INFINITE);
    }

    const unsigned cleanedThreads =
        RequestUiRendezvousAction(
            UiRendezvousAction::Cleanup,
            1200);

    ProbeLog(
        L"BeforeUninit: cleanup rendezvous matched XAML windows=%u",
        cleanedThreads);

    // Any callback already executing on another thread must finish before
    // Windhawk can unload this DLL. The observer is opt-in and log-only.
    while (g_vmProbeCallbacksInFlight.load(std::memory_order_acquire) != 0) {
        Sleep(1);
    }


    g_uiRendezvousPending.store(
        false,
        std::memory_order_release);

    DetachUiRendezvousHooks();

    // Normal teardown explicitly releases this process's native transient
    // ownership. Unexpected termination is covered by reader-side writer
    // liveness + the short terminal grace window.
    ClearOwnedTransientBridgeSlots(
        L"BeforeUninit");
}

void Uninitialize() {
    if (g_workerThread) {
        WaitForSingleObject(
            g_workerThread,
            INFINITE);
        CloseHandle(
            g_workerThread);
        g_workerThread =
            nullptr;
    }

    DetachUiRendezvousHooks();

    if (g_uiRendezvousCompletedEvent) {
        CloseHandle(
            g_uiRendezvousCompletedEvent);
        g_uiRendezvousCompletedEvent =
            nullptr;
    }

    if (g_refreshWakeEvent) {
        CloseHandle(
            g_refreshWakeEvent);
        g_refreshWakeEvent =
            nullptr;
    }

    if (g_stopEvent) {
        CloseHandle(
            g_stopEvent);
        g_stopEvent =
            nullptr;
    }

    AcquireSRWLockExclusive(
        &g_knownXamlThreadsLock);
    g_knownXamlThreads.clear();
    ReleaseSRWLockExclusive(
        &g_knownXamlThreadsLock);

    g_preferredXamlThreadId.store(
        0,
        std::memory_order_release);

    g_transientFeatureValidated.store(
        false,
        std::memory_order_release);
    ShutdownTransientBridge();

    UnregisterPrivateProcessWindowClass(
        kTransientFastUiBridgeClass,
        TransientFastUiBridgeWndProc);

    std::atomic_store_explicit(&g_recentCleanPresenterPass,
        std::shared_ptr<const RecentCleanPresenterPass>{},
        std::memory_order_release);
    ProbeLog(L"user-list status module stopped");
}

} // namespace UserStatus



void OnLoaded_Hook(void* pThis, void* sender, void* args) {
    RememberXamlThread(GetCurrentThreadId(), true);
    ModLog(L"SystemProviderUserControl::_OnLoaded hook fired on XAML thread=%lu",
           GetCurrentThreadId());

    g_OnLoaded_Original(pThis, sender, args);

    if (g_unloading.load(std::memory_order_acquire)) {
        return;
    }

    // The RoutedEvent sender is an ABI interface pointer for the actual
    // SystemProviderUserControl, which is safer to project than the native
    // C++/CX implementation `this` pointer.
    InjectVisual(sender);

    // User-list status uses the same validated SystemProvider lifecycle hook.
    // Its private UserViewModel ABI is independently gated, so failure here
    // never disables Battery/Volume or native system-button tweaks.
    UserStatus::OnSystemProviderLoaded(sender);
}

bool InstallHook(HMODULE module, DWORD build, DWORD ubr) {
    if (g_hookInstalled.load()) {
        return true;
    }

    // OS build/UBR is diagnostic only. A function boundary at an old RVA
    // does not establish the identity or ABI of the loaded implementation.
    // Always validate this image, including when logging is disabled.
    ModLog(L"Validating loaded Windows.UI.Logon.dll structure (OS %lu.%lu is diagnostic only)",
           build, ubr);
    void* target = ResolveOnLoadedByStructure(module);
    const bool usedStructuralResolver = target != nullptr;

    if (!target) {
        ModLog(L"Symbol-free _OnLoaded resolution unavailable; using symbol fallback");
        target = ResolveOnLoadedBySymbols(module);
        if (!target) {
            return false;
        }
    }

    bool expected = false;
    if (!g_hookInstalled.compare_exchange_strong(expected, true)) {
        return true;
    }

    if (!Wh_SetFunctionHook(
            target,
            reinterpret_cast<void*>(OnLoaded_Hook),
            reinterpret_cast<void**>(&g_OnLoaded_Original))) {
        ModLog(L"Wh_SetFunctionHook failed");
        g_hookInstalled = false;
        return false;
    }

    Wh_ApplyHookOperations();

    if (usedStructuralResolver) {
        ModLog(L"Hook installed via cross-build symbol-free structural resolver");
    } else {
        ModLog(L"Hook installed via cross-build symbol fallback");
    }

    // Hot-attach/reload recovery: the provider may already have completed
    // _OnLoaded while first-time symbol acquisition was in progress, or the
    // user may have recompiled the mod while already on the sign-in screen.
    RequestLateAttachExistingSystemProvider();
    return true;
}

DWORD WINAPI HookThreadProc(void*) {
    DWORD build = 0;
    DWORD ubr = 0;
    if (GetOsBuild(&build, &ubr)) {
        g_osBuild.store(build);
        ModLog(L"Windows build: %lu.%lu (diagnostic only; no build allowlist)",
               build,
               ubr);
    } else {
        ModLog(L"Couldn't read Windows build; continuing with symbol resolution");
    }

    while (WaitForSingleObject(g_stopEvent, 50) == WAIT_TIMEOUT) {
        HMODULE module = GetModuleHandleW(kTargetModule);
        if (!module) {
            continue;
        }

        if (!InstallHook(module, build, ubr)) {
            ModLog(L"Couldn't resolve/hook _OnLoaded on this system; leaving LogonUI untouched");
        }
        return 0;
    }

    return 0;
}

} // namespace

BOOL Wh_ModInit() {
    g_unloading.store(false, std::memory_order_release);
    ClearKnownXamlThreads();
    g_lateAttachProbeThreadId.store(0, std::memory_order_release);
    g_hotCleanupProbeThreadId.store(0, std::memory_order_release);
    LoadSettings();
    ModLog(L"Init");

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent) {
        ModLog(L"CreateEvent failed: %lu", GetLastError());
        return FALSE;
    }

    if (!UserStatus::Initialize()) {
        ModLog(L"WARNING: user-list status module initialization failed; continuing with system-control tweaks only");
    }

    // Start stobject symbol acquisition immediately on a worker. On an exact
    // known build this resolves essentially instantly; on an unknown build
    // Windhawk can download/enumerate symbols without ever blocking LogonUI's
    // XAML UI thread.
    StartBatteryFlyoutSymbolPrefetch();
    StartClassicClockAbiPrefetch();
    EnsureBatteryNotificationWatcherStarted();

    g_hookThread = CreateThread(nullptr, 0, HookThreadProc, nullptr, 0, nullptr);
    if (!g_hookThread) {
        ModLog(L"CreateThread failed: %lu", GetLastError());

        // Producers may already own windows/callbacks in this DLL. Keep the
        // shared stop handle valid until every started producer has exited.
        g_unloading.store(true, std::memory_order_release);
        SetEvent(g_stopEvent);

        if (g_batteryNotificationThread) {
            WaitForSingleObject(g_batteryNotificationThread, INFINITE);
            CloseHandle(g_batteryNotificationThread);
            g_batteryNotificationThread = nullptr;
        }
        UnregisterPrivateProcessWindowClass(
            kBatteryNotifyClass, BatteryNotifyWndProc);

        if (g_batteryResolverThread) {
            WaitForSingleObject(g_batteryResolverThread, INFINITE);
            CloseHandle(g_batteryResolverThread);
            g_batteryResolverThread = nullptr;
        }
        if (g_classicClockResolverThread) {
            WaitForSingleObject(g_classicClockResolverThread, INFINITE);
            CloseHandle(g_classicClockResolverThread);
            g_classicClockResolverThread = nullptr;
        }

        UserStatus::BeforeUninit();
        UserStatus::Uninitialize();

        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
        return FALSE;
    }

    return TRUE;
}

BOOL Wh_ModSettingsChanged(BOOL* bReload) {
    ModLog(L"SettingsChanged: requesting a controlled mod reload so all visual, layout, interaction, and diagnostic settings apply consistently");
    if (bReload) {
        *bReload = TRUE;
    }
    return TRUE;
}

void Wh_ModBeforeUninit() {
    g_unloading.store(true, std::memory_order_release);
    ModLog(L"BeforeUninit: beginning hot-unload-safe teardown");

    // Stop producers first so the UI fence only has to drain work which was
    // already queued before teardown began.
    if (g_stopEvent) {
        SetEvent(g_stopEvent);
    }
    StopBrightnessWorker();

    UserStatus::BeforeUninit();

    g_lateAttachPending.store(false, std::memory_order_release);
    DetachLateAttachRendezvousHooks();
    if (g_lateAttachCompletedEvent) {
        SetEvent(g_lateAttachCompletedEvent);
    }

    const bool cleaned = RequestHotCleanupOnUiThread();
    if (!cleaned) {
        ModLog(L"WARNING: hot-unload UI cleanup couldn't be fully confirmed before hook removal");
    }

    ShutdownClassicClockBackend();

    // Diagnostic RenderTargetBitmap capture is a coroutine whose continuation
    // also lives in this DLL. It is off by default, but if enabled, don't let
    // Windhawk unload the module until an already-started capture has resumed
    // and completed.
    ULONGLONG nextCaptureWaitLog = GetTickCount64() + 5000;
    while (g_captureAsyncInFlight.load(std::memory_order_acquire) != 0) {
        Sleep(10);
        if (GetTickCount64() >= nextCaptureWaitLog) {
            ModLog(L"BeforeUninit: waiting for diagnostic XAML capture coroutine to finish before DLL unload");
            nextCaptureWaitLog = GetTickCount64() + 5000;
        }
    }

    ULONGLONG nextAudioWaitLog = GetTickCount64() + 5000;
    while (g_audioCallbackObjectsAlive.load(std::memory_order_acquire) != 0 ||
           g_audioCallbacksInFlight.load(std::memory_order_acquire) != 0) {
        Sleep(10);
        if (GetTickCount64() >= nextAudioWaitLog) {
            ModLog(L"BeforeUninit: waiting for Core Audio callback objects to be released before DLL unload (objects=%u inFlight=%u)",
                   g_audioCallbackObjectsAlive.load(std::memory_order_relaxed),
                   g_audioCallbacksInFlight.load(std::memory_order_relaxed));
            nextAudioWaitLog = GetTickCount64() + 5000;
        }
    }
}

void Wh_ModUninit() {
    StopBrightnessWorker();
    UserStatus::Uninitialize();
    ModLog(L"Uninit");

    g_lateAttachPending.store(false, std::memory_order_release);
    DetachLateAttachRendezvousHooks();
    if (g_lateAttachCompletedEvent) {
        SetEvent(g_lateAttachCompletedEvent);
    }

    if (g_stopEvent) {
        SetEvent(g_stopEvent);
    }

    if (HWND bridge =
            g_batteryUiBridgeWindow.exchange(
                nullptr, std::memory_order_acq_rel)) {
        DWORD_PTR ignored = 0;
        SendMessageTimeoutW(
            bridge,
            WM_CLOSE,
            0,
            0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK,
            1000,
            &ignored);
    }

    if (g_hookThread) {
        WaitForSingleObject(g_hookThread, INFINITE);
        CloseHandle(g_hookThread);
        g_hookThread = nullptr;
    }

    if (g_lateAttachCompletedEvent) {
        CloseHandle(g_lateAttachCompletedEvent);
        g_lateAttachCompletedEvent = nullptr;
    }

    if (g_batteryResolverThread) {
        // Windhawk's symbol API doesn't expose cancellation. Wait for the
        // worker so it can't execute code after this mod DLL is unloaded.
        WaitForSingleObject(g_batteryResolverThread, INFINITE);
        CloseHandle(g_batteryResolverThread);
        g_batteryResolverThread = nullptr;
    }

    if (g_classicClockResolverThread) {
        // Same rule as the Battery symbol worker: don't unload this mod while
        // a Windhawk symbol enumeration can still call back into our code.
        WaitForSingleObject(g_classicClockResolverThread, INFINITE);
        CloseHandle(g_classicClockResolverThread);
        g_classicClockResolverThread = nullptr;
    }

    if (g_batteryNotificationThread) {
        WaitForSingleObject(g_batteryNotificationThread, INFINITE);
        CloseHandle(g_batteryNotificationThread);
        g_batteryNotificationThread = nullptr;
    }

    if (g_advancedPowerThread) {
        WaitForSingleObject(g_advancedPowerThread, INFINITE);
        CloseHandle(g_advancedPowerThread);
        g_advancedPowerThread = nullptr;
    }

    if (g_dateTimeDialogThread) {
        WaitForSingleObject(g_dateTimeDialogThread, INFINITE);
        CloseHandle(g_dateTimeDialogThread);
        g_dateTimeDialogThread = nullptr;
    }

    if (g_sndVolWindowMonitorThread) {
        WaitForSingleObject(g_sndVolWindowMonitorThread, INFINITE);
        CloseHandle(g_sndVolWindowMonitorThread);
        g_sndVolWindowMonitorThread = nullptr;
    }

    if (g_clockWindowMonitorThread) {
        WaitForSingleObject(g_clockWindowMonitorThread, INFINITE);
        CloseHandle(g_clockWindowMonitorThread);
        g_clockWindowMonitorThread = nullptr;
    }

    // Process-local window classes outlive their windows unless explicitly
    // unregistered. Leaving one registered would retain a WNDPROC pointer into
    // this DLL after hot-disable and can crash the next hot-load via CFG or a
    // jump into the same RVA of a newly mapped mod image.
    UnregisterPrivateProcessWindowClass(
        kBatteryUiBridgeClass, BatteryUiBridgeWndProc);
    UnregisterPrivateProcessWindowClass(
        kBatteryNotifyClass, BatteryNotifyWndProc);
    UnregisterPrivateProcessWindowClass(
        kLegacyVolumeUiWindowClass, LegacyVolumeUiWndProc);

    if (void* cleanupTarget =
            g_cleanupBatteryDataTarget.load(std::memory_order_acquire)) {
        using CleanupBatteryData_t = void(WINAPI*)();
        auto cleanupBatteryData =
            reinterpret_cast<CleanupBatteryData_t>(cleanupTarget);
        cleanupBatteryData();
        ModLog(L"BatMeter backend cleaned up");
    }

    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
}
