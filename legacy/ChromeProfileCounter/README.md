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
and verifies the resulting value. The replacement uses a same-directory
temporary file and `File.Replace`, which also backs up the exact replaced file
under Chrome's User Data folder. Edits by this tool are serialized; changed
state detected after confirmation is rejected. Chrome itself does not take the
tool's lock, so keep it closed throughout the operation. Atomic replacement
does not guarantee persistence through power loss.

This remains an interactive utility. INI, command-line actions, tray, and Startup
controls remain unfinished requirements. A resident companion can start at
sign-in while keeping counter changes behind a deliberate action.

`tools\ChromeProfileCounterSourceCheck.ps1` parses and checks the source without
reading Chrome data or starting Chrome. `tools\ChromeProfileCounterTests.ps1`
loads selected functions and tests synthetic JSON and temporary directories;
only its Windows fixture checks mutex/file replacement. CTest includes both.
Both checks passed on synthetic inputs in Windows PowerShell 5.1 and
PowerShell 7 on 2026-10-08. The counter edit also proves that its selected
numeric span belongs to the parsed profile counter, so an escaped property
name cannot redirect it into a same-valued unrelated field.

The planned compiled migration starts with
`dependencies/ChromeProfileCounter/chrome_profile_counter_engine.cs`. It is
not connected to the launcher yet. It reuses the existing managed ecosystem
without introducing runtime plugin DLLs; the current PowerShell UI remains
available during migration. `tools/BuildChromeProfileCounterEngineTests.cmd`
at the repository root compiles its synthetic fixture only. Scan the resulting
executable before running it. The fixture supplies all paths and browser
observations, and never reads actual Chrome data or discovers browser processes.
