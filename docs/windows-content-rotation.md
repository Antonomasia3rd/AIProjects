# Windows-managed content rotation

DesktopStub can hand two to five enabled content items to Windows' Live Tile
notification queue. Windows rotates those items itself; DesktopStub replaces a
tagged item when its image, text or activation link changes. Unchanged items do
not trigger tile updates or repeated image rendering.

Choose **Updates and cycling > Who rotates contents** in the tray menu:

- **DesktopStub** retains the app's chosen cycle interval.
- **Auto** uses Windows rotation when the current mode supports it, with the app
  timer as a fallback.
- **Windows** requests Windows rotation and reports why the app timer is needed
  when the layout cannot use the queue.

The INI equivalent is `[Content] CycleDriver=DesktopStub`, `Auto` or `Windows`.
Existing profiles default to `DesktopStub`. New content presets choose `Auto`.
Only enabled entries count toward Windows' five-item limit; disabled saved
entries can stay in the profile.

Windows decides the display order and animation timing. DesktopStub's cycle
interval applies to app-managed rotation and fallback; it cannot impose an
exact ten-second Windows rotation. Microsoft documents the queue and tagged
replacement behavior in
[TileUpdater.EnableNotificationQueue](https://learn.microsoft.com/en-us/uwp/api/windows.ui.notifications.tileupdater.enablenotificationqueue).

Static/registration updates use the app timer. More than five enabled items also
use that timer. Discord sending and Caps/ASUS hardware actions require a known
active app-managed item, so enabling those actions prevents Windows rotation.
Read-only previews can use the queue. A pending mode registration completes
before a native queue batch is delivered.
Native batching also requires the resident itself to have package identity,
so it can acknowledge direct tile updates. Unpackaged Windows 8/8.1 compatibility
helpers retain app-managed rotation. Their shared pending file and trace do not
yet provide per-item acknowledgments or fence late requests after a timeout;
queued batching through those helpers remains deferred.

## Wallpaper and now playing preset

**Wallpaper and now playing (two-item cycle)** creates a wallpaper item with
CustomText and a separate transparent item with SMTC text. It copies the current
wallpaper item's name and custom text, keeps the existing source-refresh and
fallback-cycle intervals, and chooses Auto text delivery and Auto rotation.
Applying a preset appends new items and disables older items without deleting
their saved fields. Reapplying an unchanged active preset makes no changes.

This is separate from putting SMTC text over a wallpaper in a single item. A
transparent source image also cannot force every Windows shell to draw a fully
transparent tile surface; the shell controls its own background and branding.

## Activity and limits

Windows handles animation between queued items. DesktopStub still performs
lightweight host/configuration and wallpaper checks so it can detect changes.
In Windows rotation, SMTC session, metadata and playback events wake its worker;
timeline-progress events are deliberately omitted. A supported subscription has
a fallback refresh of at least thirty seconds. Unsupported media event
interfaces retain the configured polling interval.

Queue delivery is serialized. Only successful updates are acknowledged; a
failed batch stops and waits before retrying. New layouts reset the queue once,
while changes within the same layout replace individual tags. Image files used
by unchanged queued items remain retained. Native text updates reuse their
background image; bitmap Overlay text still requires rendering a changed image.
Disabling every queued item publishes a blank tile rather than leaving the old
items rotating. Returning to app rotation retries a failed departure update
until Windows has accepted it, with the same retry delay.

Portable planner/transport tests and inert Windows host tests cover these
contracts. Actual Windows rotation timing, shell presentation and sustained
provider/service CPU behavior still require local acceptance testing.
