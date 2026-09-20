# Capture needed to settle acrylic and media opacity

Use **builds/lockapp-xaml-dumper.wh.cpp (0.5.0)** on the Windows 10 reference system. Its supplied profile preserves the initial 10-second cold-start delay. The earlier captures identify themselves as schema 3; the new output identifies itself as schema 5 and records changes continuously after the baseline.

1. Compile the dumper and use `preferences/lockapp-xaml-dumper.capture.advanced-settings.json` in Advanced → Mod settings. The YAML counterpart belongs in the Settings tab's YAML editor.
2. Reload the dumper after saving the settings. Start media playback, then lock the Windows 10 system normally. Leave the screen idle for at least 20 seconds so the settled baseline and event subscriptions are ready.
3. During the following capture period, hover the location/copyright area, a feedback row, a media button, and a widget card for several seconds each. Move away between them. Feedback need not be clicked. Take screenshots and note which part is hovered.
4. Copy the newest `LockApp-XamlDump-*.jsonl` from LockApp's LocalState folder (normally `%LOCALAPPDATA%\Packages\Microsoft.LockApp_cw5n1h2txyewy\LocalState`) into `dumps/`, with screenshots under `screenshots/`. Successful paths are no longer printed to the mod log. Avoid using an editor that truncates the file.

One Windows 10 file with both the media panel and cards should provide the missing material parameters. A similarly captured Windows 11 file would let us compare evaluated acrylic and radius values directly. For the optional widget-input investigation, capture Windows 11 once with the styler disabled and note whether the same controls still fail after signing in; the dumper records evaluated IsEnabled and IsHitTestVisible.

The profile captures one full baseline and then records supported XAML notifications until stopped, suspended with LockApp, or the 256-MiB file limit is reached. It does not promise every compositor frame. Open the file in `reports/dump-explorer.html` and use the time slider to inspect recorded hover states. No clipboard operation or administrator control through Codex is needed.
