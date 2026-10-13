# DesktopStub topology

Source topology updated on 2026-10-13, including the current transparent
background and media-worker lifecycle changes. The earlier `53d9e32` snapshot
described the October 3 checkpoint. The source engine is built into the host;
there is no external plugin DLL loading interface. Current source/inert test
results do not imply that a newly built product has passed real shell acceptance.

## Build-time composition

```mermaid
flowchart LR
    Overlay["DesktopStub/: identity, resources, build and includes"] --> Host["DesktopStub.exe"]
    Baseline["dependencies/: tray, INI, CLI, Startup, AppX helpers"] --> Host
    Product["dependencies/DesktopStub/: host, UI, rendering and delivery"] --> Host
    Engine["content_engine.h / content_source_host.h: composition and lifecycle"] --> Host
    Sources["Shared Notes, SMTC, Discord, RSS and hardware providers"] --> Host
    BrokerSource["ga_livetile_broker_app.inc"] --> Broker["DesktopStubLiveTileBroker.exe"]
```

[`DesktopStub.cpp`](../DesktopStub/DesktopStub.cpp) includes ordered C++ source
fragments. [`BuildDesktopStub.cmd`](../DesktopStub/BuildDesktopStub.cmd) also
compiles Discord and, by default, ASUS implementation objects and links them
into the host. These object files and source folders are build inputs, not
runtime plugins. The compiled host does not need the `dependencies` source
directory beside it.

`dependencies/DesktopStub` still contains substantial product-specific code
and globals. Shared mechanisms have been extracted, but physical relocation
does not mean the entire application has reached the final overlay design.

## Runtime files

| File | Purpose |
| --- | --- |
| `DesktopStub.exe` | Tray/configuration, source scheduling, composition, rendering and delivery; built-in provider code. |
| `DesktopStubLiveTileBroker.exe` | Separately built packaged WinRT helper, primarily for Windows 8/8.1 tile delivery. Included in the normal build. |
| `DesktopStub.ini` | Product configuration, normally beside the host; an explicit custom INI path is supported. |
| `AppxManifest.xml`, `Assets/` | Generated registration metadata and tile images when those features are used. |
| `DesktopStubAppxStub.exe` | Compatibility activation helper for the relevant legacy package path. The current implementation copies the host executable under this name; it is not a lean separate plugin. The broker path checks for its separately built broker instead. |
| `DesktopStubLiveTileTask.dll` | Disabled experimental background-task path. Source remains, but the normal build does not produce this DLL. |

Renamed product copies derive their helper names and identity from the product
configuration. The table uses the default DesktopStub names. OS libraries
remain ordinary Windows runtime dependencies.

## Content sources

The canonical catalog is in [`content_engine.h`](../dependencies/content_engine.h):

- Background: desktop wallpaper, live wallpaper capture, custom image, solid
  color (`None`), current-session SMTC artwork, or no app background
  (`Transparent`).
- Text/data: custom text, RSS/Atom, Caps Lock state, SMTC, Notes, Discord Rich
  Presence, CapsBlink, and AsusBlink.
- Up to 32 entries can each combine a background and ordered text/data sources.
  Refresh timing, cycling and Auto/Overlay/Off text presentation belong to the
  host.

The legacy Wallpaper/RSS choice is a preset. RSS can appear over wallpaper;
selecting it does not imply that the wallpaper feature has been removed.
See [the content guide](content-engine.md) for configuration and remaining UI
limitations.

`Transparent` is a separate choice from the existing solid-color `None`.
Adaptive automatic text can omit the image, while bitmap/static delivery keeps
PNG alpha through the shared image pipeline. Windows controls the final tile
plate; no desktop-through-tile transparency is promised. See
[transparent background behavior](transparent-tile-background-2026-10-13.md).

Notes uses `dependencies/content_sources/notes.inc` directly. DesktopStub does
not load `RealTimeNotesDeskband.dll`. Its former Explorer host and the standalone
NowPlaying/Caps/ASUS hosts are retired from the source/build catalog. Their
shared providers and explicit configuration imports remain inside DesktopStub.
Discord's donor is also retired; its advanced editor and shared transports remain
inside the host. Source retirement does not uninstall older programs or shell integrations;
see [migration](donor-retirement-2026-10-11.md).

The standard host includes hardware code, but the hardware actions default
off. `DESKTOPSTUB_ENABLE_HARDWARE_SOURCES=0` can omit those backends at build
time; this is an existing build boundary, not a finding about the Defender
detection.

## Media worker ownership

SMTC retains one native session manager on the existing asynchronous source
worker. The worker lazily initializes and pins its MTA; subsequent snapshots
reuse that connection. A shared worker-cleanup hook releases the manager on its
owning thread before uninitializing COM when disabled, when the profile changes,
or during Stop. Disconnected managers are released and reconnect only on a later
configured refresh. Text and artwork still come from one frozen media snapshot.
The host, helper and provider modules remain portable files in the existing
layout; this change introduces no runtime source DLL.

The CPU investigation and fake-manager/lifecycle checks are recorded in
[the October 13 validation](desktopstub-cpu-validation-2026-10-13.md). They support
the lifetime/reuse correction, but do not prove that the reported NPSMSvc CPU
episode is fixed on the user's installation.

## Current direction

The user's preference is to preserve the current portable layout and introduce
compiled source DLLs only if the detection investigation establishes a need.
No provider was converted to a DLL during this investigation. Detection logs
identify an executable and classification, not a responsible C++ module; they
do not currently justify a DLL split. The historical Microsoft submission was
deferred by the user and has not been rechecked; its evidence remains in
[the incident record](audit-defender-detection.md).
