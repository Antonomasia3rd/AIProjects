# Explicit main Desktop host setup

An unpackaged fresh DesktopStub instance exposes **Tiles > Set up Desktop tile...**.
The equivalent command is:

```text
DesktopStub.exe --setup-desktop --ini DesktopStub.ini
```

This command can be combined only with the profile selector, help or version.
Offline rendering/configuration, settings/Startup edits, imports and generation
commands are rejected as combinations. An existing resident receives a profile-
scoped request and posts it to its UI queue; registration is never attempted
inside the synchronous instance-message callback.

Setup requires a valid primary Wallpaper instance and an available image. It
selects that tile's LiveTile delivery preference, composes only its background
and text, and freezes that one frame into the ordinary generation worker queue.
It does not wait for or start game, RSS, prayer or other tile providers. The worker
renders the main package's assets and uses the existing **main manifest/native
registration** path. It does not create a separate Registration tile identity.

Normal tile generation cannot replace the setup request. The UI remains usable
while registration runs. After the worker records successful completion, the UI
clears setup markers, requests graceful shutdown and uses the existing registered
main-package activation path to restart with Live Tile identity. A failed job
restores its previous registration-pending state, shows an actionable retry
message and leaves DesktopStub running. A packaged resident reports that its
identity is already ready instead of registering itself while running.

An interrupted setup has an explicit persisted marker. On the next start it is
recovered to the previous registration-pending state with a warning; it does not
silently rerun registration. The user can retry setup. Ordinary launch does not
invoke this explicit bootstrap action.

The inert host fixture uses a private INI/image, an injected queue and injected
shutdown callback. It checks CLI/menu availability, one-primary-frame isolation,
no provider activation, pending/completion sequencing, failure recovery, success-
only relaunch and interrupted-marker recovery. Actual native package deployment
is reserved for the explicitly authorized installation step; the fixture does
not install, sign, activate or pin a package.
