# Clock, Calendar, WuWa and source notifications

Create each source from **Tiles > Add tile...**, then open that tile's **Source settings**. Instances keep their own source, appearance, typography, refresh interval, delivery policy and account settings. No additional source is enabled by adding support for it to the executable.

## Prayer Times and RSS notifications

Inside a Prayer Times or RSS tile's Source settings, open **Notifications for this tile**. Notifications are disabled by default and their default sound is Silent.

Here, **notification means a desktop popup or tray balloon**, using the same
desktop notification backend as "Notify when generation starts." It is separate
from the Windows API's use of "tile notification" to mean a Live Tile update.
`Alerts.Enabled` controls desktop alerts; it does not enable or disable tile
content updates. In particular, RSS alerts announce newly detected feed items
through the chosen desktop popup/balloon API.

- Prayer Times: enable notifications, choose General, Reminder or Alarm, choose sound independently, set lead minutes (20 by default, zero means at the event), and select events. `Sunrise` and `Shuruq` are aliases. The five prayers are selected by default; sunrise alerts require adding Sunrise explicitly.
- RSS: enable notifications, set how many new articles must accumulate, and choose a minimum interval between notifications. The first successful response establishes a quiet baseline. Restarting, reordering an unchanged feed, and changing a feed URL do not announce every old article.
- Application > Notifications selects Auto, Toast or Balloon. Toast uses the existing registered application identity. Balloon provides numeric pin progress and ordinary notices through Explorer's legacy API; it cannot express Windows reminder/alarm scenarios or looping audio. Windows and ExplorerPatcher control its visible style.
- Master notifications off suppresses source sounds and popups. Source failure/recovery notices have their own toggle. Prayer/RSS alerts do not depend on operation-success notices being enabled.

Example configuration for an existing Prayer instance:

```ini
[Tile.prayers]
Type=PrayerTimes
PrayerTimes.City=Surabaya
PrayerTimes.Country=Indonesia
PrayerTimes.TwentyFourHour=0
PrayerTimes.ShowSunrise=1
PrayerTimes.NextIncludesSunrise=0
Alerts.Enabled=1
Alerts.Type=Reminder
Alerts.Sound=Silent
Alerts.LeadMinutes=20
Alerts.Events=Fajr,Dhuhr,Asr,Maghrib,Isha
```

The city above is an example, not a factory location. Fresh setup leaves it unset. Sunrise is shown chronologically between Fajr and Dhuhr in the large timetable. The main time uses the adaptive `title` size (24 effective pixels), and prayer name/location share Secondary styling. AM/PM is the default. The separate minute counter remains configurable through Badge styling.

RSS example for the requested feed:

```ini
[Tile.releases]
Type=RSS
Name=Windhawk releases
RssFeed.FeedUrl=https://mods.windhawk.net/releases.atom
RssFeed.UpdateIntervalSeconds=900
Alerts.Enabled=1
Alerts.MinimumNewItems=2
Alerts.CooldownSeconds=300
Alerts.Type=General
Alerts.Sound=Silent
```

These settings are also accepted through typed `--set "Tile.prayers.Alerts.LeadMinutes=20"` arguments and the tray. Add instance IDs to `[Tiles] Instances` when creating profiles by hand. Use the tray to append a tile without replacing that list accidentally. `[Strings]` controls notification wording; names include `NotificationPrayerBefore`, `NotificationPrayerNow`, `NotificationPrayerTitle`, `NotificationRssNewItems` and `NotificationRssTitle`.

Reminders reuse already fetched data while DesktopStub is running and the tile is demanded. They do not wake a sleeping laptop or schedule an OS alarm after exit. A 90-second grace handles normal polling jitter without replaying old reminders after sleep. Valid cached daily prayer schedules can still supply reminders during a temporary fetch failure. Hashed baseline sidecars are scoped by Windows user, profile and tile, and do not store article URLs, account values or credentials.

## Clock and Calendar

Add multiple **Clock / world clock** instances for different cities. Set `Clock.TimeZone` to Local, a Windows time-zone ID, or a supported IANA alias; e.g. `SE Asia Standard Time` / `Asia/Jakarta`, `GMT Standard Time`, or UTC. Set the location label and select 12-hour, 24-hour, or the Windows regional time format. No per-second worker is used.

Calendar is date-only until a local `.ics` file is selected. Events are an offline preview of that explicit file, without Microsoft account access. Supported recurrence, timezone handling and import limitations are documented in [the Clock/Calendar reference](clock-calendar-2026-10-23.md). Both sources use semantic adaptive text and matching bitmap/status content. Custom text styles, wrapping and line limits remain per tile. Installed app manifests were inspected read-only; their runtime-generated tile XML was not extracted, so this is not a binary-exact reproduction claim.

## Wuthering Waves

Add **Wuthering Waves**, then supply an overseas WuWa SDK game access token. The tray protects it with Windows DPAPI. This is not a HoYoLAB cookie, KuroBBS cookie, password login or automatic browser import. A single linked role can be discovered; multiple roles need the game's player ID and region. Waveplates, refined waveplates, daily activity and weekly challenges remaining use verified launcher fields. Other Kuro games and Chinese-region WuWa are not claimed as supported. See [account setup and protocol references](wuthering-waves-notes-2026-10-23.md).

## Pinning, gallery changes and reset

Bulk pinning reports one changing progress notification when modern toasts are available; legacy balloons show throttled completed/total counts. Windows still asks the user to approve native pin requests. Shrinking a gallery defaults to unpinning only the removed owned tile IDs, notifying about the change, and no extra application confirmation; all three choices are configurable. Surviving tile IDs and image pools stay intact.

A requested settings reset archives the INI and its account/history/notification sidecars together, preserving source photographs and Windows Start layout. Old source pins require reconfiguration or manual removal after a fresh Desktop-only profile. Resetting settings does not imply deleting Windows packages or silently rearranging Start.

Primary references: [adaptive tile typography](https://learn.microsoft.com/en-us/windows/uwp/launch-resume/create-adaptive-tiles), [desktop toast notifier overload](https://learn.microsoft.com/en-us/uwp/api/windows.ui.notifications.toastnotificationmanager.createtoastnotifier), [toast scenarios and actions](https://learn.microsoft.com/en-us/windows/apps/develop/notifications/app-notifications/app-notifications-content), [audio schema](https://learn.microsoft.com/en-us/uwp/schemas/tiles/toastschema/element-audio).
