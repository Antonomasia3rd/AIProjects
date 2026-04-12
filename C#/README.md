### Contents:
* AllowContentAboveLock
* YourPhoneHideBanner
* asusblink

# AllowContentAboveLock
## Why was this made?
* starting from Windows 11 22H2 (latest build) and newer, all notifications are set to "Private", which prevents notification content from showing on the Lock Screen
* this program reverts the behavior to Windows 11 21H2 and lower, which doesn't hide notification content on Lock Screen
* (the previous Windows builds behavior was, if `AllowContentAboveLock` does **not** exist, it'll show content by default, the newer Windows builds flipped this behavior)
## How it works
* Listens to HKEY_USERS\SID\Software\Microsoft\Windows\CurrentVersion\Notifications\Settings\*\
* Adds a DWORD named `AllowContentAboveLock` and sets it to `1`
* because this is a Windows service, it listens to HKEY_USERS itself to make it apply to all users
## Disclaimer
* this program is only tested on the following scenario(s): installed as Windows service,
* it might not work if ran directly
## Feature(s)
* Event Viewer logging (see Windows Logs\Applications)
## Planned feature(s)
* customizable configuration
  * all config(s) will be stored as %exename%.ini, on the same folder as the .exe itself
* user(s) to apply
  * apply to all users toggle
  * apply to all users, except...
  * apply to specific users only
    * for this option, make a disclaimer: this program can only see logged-in users, log-in first if you haven't!
* logging options
  * Event Viewer (with its parameters)
  * and/or as a File (customize path, its formatting, etc.)
* running it directly without installing as a Windows service
  * put a tray icon to be able to customize its configuration directly without editing the .ini file
    * [disabled entry] General:
    * [checkbox] Tray Icon
    * [checkbox] Show Console
    * [checkbox] Enable Logging
    * [button] Change Log Path
    * [disabled entry] Current path: [displays path]
    * [separator]
    * [disabled entry] Apply:
    * [radio] ...to all users
    * [radio] ...to specific users
    * [radio] Disabled
    * [dropdown] Exclude: [list users SID along with the username, e.g. "LocalSystem (S-1-5-18)", etc.)
      * disable the button if "...to specific users" was selected, but don't block the dropdown itself
    * [dropdown] Include: [list users SID along with the username, e.g. "LocalSystem (S-1-5-18)", etc.)
      * disable the button if "...to all users" was selected, but don't block the dropdown itself
* localizations
  * if possible, as .mui file
  * otherwise, as a separate .ini file, named l10n.ini
## NOTE: this code was unmaintained
colleg 😭

# YourPhoneHideBanner
## Why was this made?
* Some of my apps that are already installed on this laptop already push notification that is also pushed to my phone, which pushes it to my laptop again (e.g. WhatsApp, Discord, etc.)
* this program disables the banner (and sound) to prevent duplicate notification with the same content
## How it works
this has the similar codebase as `AllowContentAboveLock`, only minor changes
* Listens to HKEY_USERS\SID\Software\Microsoft\Windows\CurrentVersion\Notifications\Settings\Microsoft.YourPhone_8wekyb3d8bbwe!YourPhoneNotifications_*\
* Adds a DWORD named `ShowBanner` and sets it to `0`
* Adds a REG_SZ named `SoundFile` and sets it to empty string `""`
* because this is a Windows service, it listens to HKEY_USERS itself to make it apply to all users
## Disclaimer
* same as `AllowContentAboveLock` (it might not work if ran directly)
## Feature(s)
* same as `AllowContentAboveLock` (Event Viewer logging)
## Planned feature(s)
* same as `AllowContentAboveLock` (5 features)
## NOTE: this code was unmaintained
colleg 😭

# asusblink
## Why was this made?
* experimenting with ASUS' keyboard backlight (and optionally mic mute LED) for testing
* proves that keyboard backlight can also be controlled user-level, from ASUS ATK WMI ACPI I/O driver
* and probably showoff??? even though no RGB you can still make your keyboard backlight "kinda" animates
## Disclaimer
* this code was inspired from [G-Helper](https://github.com/seerge/g-helper)
* only tested on a Vivobook, particularly X1504VAP_A1504VA
## How to use
* trigger the Help page by executing asusblink.exe without any parameters!
* follow the instruction from that Help page to test!
## NOTE: this code was unmaintained
colleg 😭
