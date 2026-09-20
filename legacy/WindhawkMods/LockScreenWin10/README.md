# Windows 10 lock screen appearance for Windows 11

Current styler: **candidate 4 / 1.0.3**. Current dumper: **0.5.0**, with continuous file-only change recording and timestamp replay. See [the dumper update](markdowns/DUMPER-0.5.0.md) for installation and capture details. This project uses the user's supplied Lock Screen Styler fork and measured Windows 10 22H2 XAML trees. Local builds and isolated UWP checks do not replace a normal lockscreen test.

## Install candidate 4

1. Replace the source of your Windhawk Lock Screen Styler with [builds/windows-11-lockapp-styler.wh.cpp](builds/windows-11-lockapp-styler.wh.cpp), then compile.
2. In **Advanced → Mod settings**, replace the entire field with [preferences/windows-10-lockscreen.advanced-settings.json](preferences/windows-10-lockscreen.advanced-settings.json), then Save. This is the flat JSON file. The current export contains **389 entries, 68 targets, and both helper switches set to numeric 1**.
3. Alternatively, the **Settings tab's YAML editor** accepts [preferences/windows-10-lockscreen.wh.preferences.yaml](preferences/windows-10-lockscreen.wh.preferences.yaml), or its identical `.txt` copy. Do not paste the nested `.preset.json` into the Advanced field.
4. Test by locking normally. Use one XAML styler targeting `LockApp.exe` at a time.

Both the source and preferences changed in candidate 4. The task has not installed the changes into the active Windhawk configuration. Original input files remain in `sources/`; earlier release archives remain in `releases/archive/` for rollback.

Candidate 4 adds Windows 10 heart/broken-heart feedback glyphs, the reference feedback-row typography and spacing, and hover highlighting on the photo location/copyright block. The working circle fades and live text bindings remain in place. See [the change and material assessment](markdowns/CANDIDATE4.md).

## Project map

| Directory | Contents |
|---|---|
| `builds/` | Standalone `.wh.cpp` files to paste into Windhawk |
| `preferences/` | Current Advanced JSON, YAML, text and canonical generator inputs |
| `preferences/compatibility/` | Partial theme for the unmodified original styler |
| `dumps/` | Supplied JSONL captures with descriptive filenames and an index |
| `screenshots/` | Reference and user test screenshots |
| `sources/` | Original upstream/user sources and preferences, retained unchanged |
| `tools/` | Generators, comparison tool and isolated validation hosts |
| `analysis/` | Extracted evidence, logs and migration provenance |
| `markdowns/` | Detailed findings, validation, compatibility and capture instructions |
| `reports/` | Offline XAML explorer and the private preloaded comparison |
| `build/` | Compiler output, isolated test executables, logs and symbols |
| `releases/` | Current source package; previous candidates under `archive/` |

The cleanup moved files without deleting captures or source history. `analysis/project-file-map.json` records the old paths, new paths and hashes at migration time. `markdowns/README-candidate3.md` is a historical record; its old root-level paths are superseded by this map.

## Materials and capture

The supplied schema-3 dumps omit brush opacity, acrylic parameters and corner radii. Media opacity remains the provisional `mediaBackdropOpacity=0.6`; no exact material match is claimed. Windows 10's supplied widget screenshots also have rounded corners. The updated dumper records the missing values and retains the intentional cold-start delays. See [capture instructions](markdowns/NEXT-CAPTURE.md).

## Preferences without modifying the mod

A [preferences-only variant](preferences/compatibility/windows-10-lockscreen.preferences-only.advanced-settings.json) is included for the original supplied styler. It retains the clock/date, widget placement, media layout/templates and status styling. It keeps stock Spotlight and has no compositor fade synchronization. It is a partial theme, not a replacement for the complete candidate. [Technical explanation](markdowns/PREFERENCES-ONLY.md).

## Build and inspect

The `.wh.cpp` files compile directly in Windhawk. The local tests used the installed Windhawk compiler and Windows SDK manifest tool; no new Visual Studio workload was installed.

```powershell
python tools/prepare_styler.py
python tools/prepare_dumper.py
python tools/prepare_preferences_only.py
.\tools\build.ps1
.\tools\validate-xaml.ps1 -Python python
.\tools\validate-helper.ps1
.\tools\validate-dumper.ps1
python tools/validate_live_output.py
node tools/validate_replay.cjs
python -m unittest discover -s tools -p 'test_*.py' -v
node tools/validate_settings_formats.cjs
python tools/package_candidate.py
```

The existing candidate-4 archive preserves the earlier dumper 0.4.1. The separate `LockApp-XamlDumper-0.5.0.zip` contains the newer dumper, capture profile and offline explorer. Generate that update with `python tools/package_dumper.py`.

For comparison, open `reports/dump-explorer.html` and select two captures. `reports/comparison.html` already contains the supplied reference and stock Windows 11 trees; keep it private if sharing only code. No network is needed by either report. Browser visual QA was not performed because the browser tool rejected local-file inspection.

[Validation details](markdowns/VALIDATION.md). The modified sources remain GPLv3; see [LICENSE](LICENSE) and the preserved source notices.
