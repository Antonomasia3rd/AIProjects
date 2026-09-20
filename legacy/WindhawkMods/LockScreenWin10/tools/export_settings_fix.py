"""Re-export the existing candidate without changing any style values or mod code."""
from pathlib import Path
from settings_formats import load_preset, export_settings

root = Path(__file__).resolve().parent.parent / 'preferences'
for stem, name in [('windows-10-lockscreen', 'windows-10-lockscreen.wh.preferences.yaml'),
                   ('lockapp-xaml-dumper.capture', 'lockapp-xaml-dumper.capture.preferences.yaml')]:
    settings = load_preset(root / name)
    flat = export_settings(settings, root, stem, name)
    print(stem, len(flat), 'flat string/integer entries')
