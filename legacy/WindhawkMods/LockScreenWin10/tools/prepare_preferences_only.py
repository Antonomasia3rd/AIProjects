"""Export the static layout subset for the user's unmodified Lock Screen Styler.

Deliberately excludes the Spotlight rules that depend on generated helper nodes,
hidden native content, and compositor synchronization. It is a partial theme.
"""
import copy
import json
from pathlib import Path
from settings_formats import export_settings

root = Path(__file__).resolve().parent.parent
full = json.loads((root / 'preferences/windows-10-lockscreen.preset.json').read_text(encoding='utf-8'))
rules = [copy.deepcopy(rule) for rule in full['controlStyles']
         if not rule['target'].startswith('LockApp.') and rule['target'] != 'TextBlock#TitleText']
assert rules and all('Hotspot' not in rule['target'] and '#Win10' not in rule['target'] for rule in rules)
settings = {'controlStyles': rules, 'styleConstants': full['styleConstants'],
            'themeResourceVariables': full['themeResourceVariables']}
destination = root / 'preferences/compatibility'
destination.mkdir(parents=True, exist_ok=True)
flat = export_settings(settings, destination, 'windows-10-lockscreen.preferences-only',
                       'windows-10-lockscreen.preferences-only.yaml')
print(f'Prepared preferences-only subset: {len(rules)} targets, {len(flat)} flat entries; stock Spotlight retained')
