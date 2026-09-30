# ChromeProfileCounter

Interactive PowerShell utility for inspecting and repairing Chrome's local
`profile.profiles_created` counter. The compatibility entry point remains in
this folder; its implementation is in
`dependencies\ChromeProfileCounter\chrome_profile_counter_app.ps1`.

Run it from PowerShell:

```powershell
.\ChromeProfileCounter.ps1
```

The tool refuses a counter change while Chrome is running, checks both on-disk
and registered numbered profiles, requires `YES` before changing `Local State`,
and verifies the resulting value. It creates a uniquely named backup under
Chrome's User Data folder before the change. The backup and the replacement
write use same-directory temporary files and atomic renames, so an interrupted
write does not truncate the existing `Local State` file.

This remains a one-shot, interactive utility. It has no INI profile, tray icon,
or Startup option yet; automatically editing Chrome data at sign-in would be
unsafe. A future companion would need a deliberate, reviewable action surface
rather than a hidden startup task.

`tools\ChromeProfileCounterSourceCheck.ps1` parses and checks the source without
reading Chrome data or starting Chrome. It is not run as part of this source-only
continuation while the Defender quarantine remains unresolved.
