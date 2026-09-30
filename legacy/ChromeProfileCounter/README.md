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
The AST/parser checks ran during review, but the behavioral fixtures remain
unrun on this laptop while the Defender quarantine remains unresolved.
