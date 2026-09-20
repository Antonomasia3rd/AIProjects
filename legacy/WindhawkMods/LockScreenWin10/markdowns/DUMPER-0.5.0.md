# Continuous LockApp XAML dumper 0.5.0

This update addresses the unused-header diagnostic and adds continuous, file-only change recording. The styler remains version 1.0.3.

## Install

1. Replace the dumper source in Windhawk with `builds/lockapp-xaml-dumper.wh.cpp`, then compile.
2. Replace **Advanced → Mod settings** with `preferences/lockapp-xaml-dumper.capture.advanced-settings.json`. It has 14 flat numeric entries. The YAML counterpart is for the Settings tab's YAML editor.
3. Reload the dumper after saving settings. Settings are read at initialization so one capture has consistent settings.
4. Start media playback if needed, lock normally, wait about 10–20 seconds for the initial baseline, then hover the controls of interest. Recording continues while LockApp runs and delivers events, up to the file-size limit.

Output is the newest `LockApp-XamlDump-*.jsonl` in LockApp's actual ApplicationData LocalState folder, normally:

```text
%LOCALAPPDATA%\Packages\Microsoft.LockApp_cw5n1h2txyewy\LocalState
```

The mod obtains the folder through ApplicationData rather than assuming the drive/profile path. Successful captures no longer announce their path in the mod log. Filenames use CREATE_NEW and a collision suffix so reloads in the same second do not overwrite an existing file.

## What continuous recording means

The supplied profile preserves the intentional **10-second minimum initial delay**, waits for a quiet tree or the 20-second timeout, and captures one complete diagnostic baseline. It then records changes when XAML delivers notifications. The snapshot scheduler exits after this baseline in continuous mode: there is no repeating full-tree property-chain query or sampling interval.

The stream includes tree additions/removals, watched public dependency properties, in-place and replacement brush changes, supported transform changes, element size changes, scroll view changes, and visual-state transitions. New elements receive an evaluated baseline and subscriptions as they are added. Existing style-relevant property-chain metadata remains in the initial full snapshot.

The implementation uses [RegisterPropertyChangedCallback](https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.dependencyobject.registerpropertychangedcallback) and [VisualStateGroup.CurrentStateChanged](https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.visualstategroup.currentstatechanged), with corresponding cleanup. The `<memory>` header is now used directly by shared/weak pointer lifetime management. The old unused-header message was an editor warning, not a compile error; it is no longer an unused include in 0.5.0.

This does not capture every rendered frame or every private property. Composition-only animation values, private CLR properties, and movement of every descendant do not all have a public dependency-property notification. Size and scroll events record the geometry/offsets available at those events. The earlier interval-based mode remains available with `continuousCapture=0` and appropriate snapshot/trace settings; continuous mode ignores those periodic settings.

## File output and performance

Callbacks read the changed value and queue a JSON record. UTF-8 conversion and WriteFile run on one background worker. The initial full snapshot and subscription installation still access XAML on its owning thread, so actual VM performance needs a live test.

- Dump records never fall back to Windhawk logging. The obsolete `logFallback` setting is removed and ignored.
- Only errors, queue overflow and capture-limit failures are logged. Routine initialization, file paths and successful snapshots are not logged.
- Queued UTF-16 payload is limited to 32 MiB; the worker can additionally hold one drained batch. An `outputGap` record reports dropped records. A full snapshot batch is enqueued or rejected together.
- The supplied file limit is 256 MiB. A `captureStopped` record marks that limit.
- Data is written as it arrives using normal Windows file caching. It is flushed when the writer closes, rather than forcing a disk flush for every UI event. The last queued records can be lost if the process is terminated abruptly.
- Callback cleanup runs on the XAML thread. If that thread cannot be reached during teardown, callbacks are deactivated and the DLL is retained until LockApp exits to avoid jumping into unloaded code.

## Inspect a moment in the recording

Open `reports/dump-explorer.html` manually and load the JSONL file. Schema-5 captures provide a time slider per pane. It starts at the full baseline; advance it to a recorded hover or interaction. Times are milliseconds/seconds since the dumper watcher started. The selected element's trace shows its recorded events. The original baseline is retained when moving the slider backwards.

For a fixed time in the generated comparison:

```powershell
python tools/compare_dumps.py reference.jsonl candidate.jsonl --at-ms 18000 --output reports/at-18s.html
```

Replay combines the baseline with recorded changes. It does not invent unobserved compositor frames or turn missing geometry notifications into exact positions. Gaps and monitoring errors are shown as warnings. Existing schema-3/4 dumps remain supported.

## Validation

The isolated UWP test records 200 ordered text changes, margin changes, changes inside an existing acrylic brush, replacement-brush updates, and real visual-state events from a connected XAML island in an offscreen, nonactivating test window. It checks revocation, 100 attach/detach cycles, queue overflow markers and file-size termination. The initial disconnected fixture did not raise state events; the connected fixture verifies the actual event path. Live baselines read group/state names without traversing runtime setter targets that can throw when unresolved.

Python validates the emitted JSONL and replays it. Browser replay is checked against that same output with Node, alongside cutoff/tree-removal tests and JavaScript syntax checks. The code is compiled with the Windhawk compiler and import library. These checks do not install the mod into LockApp, measure Secure Desktop performance, or constitute browser visual QA.
