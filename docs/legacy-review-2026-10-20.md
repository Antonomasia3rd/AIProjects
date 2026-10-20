# Bounded legacy review, October 20

DesktopStub remained the priority. The secondary read-only pass inspected
WindhawkMods, Discord HA Bot Starter and Minecraft-Bedrock-AFK without running
their installed integrations, accounts, live services or Startup entries.

## Confirmed Windhawk lifecycle repair

`legacy/WindhawkMods/local@center-window-geometry.wh.cpp` could hang during
unload if the owner called uninitialization before the worker published its
overlay HWND. The old code sent WM_CLOSE only when that HWND was already
available, then waited indefinitely while the worker could subsequently enter
GetMessage with no shutdown request pending.

Version 0.2 creates a manual-reset stop event before starting the worker.
Startup checks it, idle message waits observe it, and message draining stops
when it is signaled. The owner joins the worker before closing the event. This
changes the helper's lifecycle, not private shell hooks, offsets or window
centering policy.

The inert `CenterGeometryLifecycleTests.cpp` includes the actual helper through
a test-only guard. It creates private threads, events and message queues,
without windows or hooks. A fresh clean scan preceded **232 passing checks**:
prepublication stop, idle interruption, pending messages, duplicate start and
100 immediate worker-failure/reinitialization cycles. The process handle count
returned exactly to its baseline. Final actual mod source passed installed
Windhawk Clang syntax checks for x64 and x86 with warnings treated as errors.

No installed mod was copied, reloaded or enabled. The source change and fixture
are reviewable separately from DesktopStub.

## Other two projects

The bounded HA bot and Minecraft pass found no additional proven issue in the
inspected lifecycle, archive, reconciliation and world-cache paths beyond the
existing October 15 audit fixes. No speculative change was made. This is not
an exhaustive live-service validation: databases, authentication, Minecraft,
HA/MQTT, Startup and real server behavior were not exercised.

Private fixture evidence is under
`build/review-validation/center-geometry-lifecycle-oct20*`.
