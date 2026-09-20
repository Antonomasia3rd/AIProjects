"""Generate setter cases for the independent UWP XAML validation host."""
from pathlib import Path
from html import escape
import json
import re
from settings_formats import load_preset

root = Path(__file__).resolve().parent.parent
preset = load_preset(root / "preferences/windows-10-lockscreen.wh.preferences.yaml")
constants = dict(c.split("=", 1) for c in preset["styleConstants"] if "=" in c)
output = ['struct Case { const wchar_t* target; const wchar_t* style; const wchar_t* element; };', 'static const Case cases[] = {']
expanded_rules = []
for rule in preset['controlStyles']:
    states = sorted({raw.split('=', 1)[0].split('@', 1)[1].rstrip(':')
                     for raw in rule['styles'] if '@' in raw.split('=', 1)[0]})
    if not states:
        expanded_rules.append(rule)
        continue
    for state in states:
        styles = []
        for raw in rule['styles']:
            name, value = raw.split('=', 1)
            if '@' not in name:
                styles.append(raw)
            elif name.split('@', 1)[1].rstrip(':') == state:
                styles.append(name.split('@', 1)[0] + (':' if name.endswith(':') else '') + '=' + value)
        expanded_rules.append({'target': rule['target'], 'styles': styles, 'state': state})
for rule in expanded_rules:
    kind = re.split(r"[#\[@]", rule["target"].split(" > ")[-1])[0]
    if kind.startswith("LockApp."):
        kind = "Control"  # Test inherited property types; private LockApp classes are not loadable here.
    prefix = '<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"'
    if "." in kind:
        namespace, simple = kind.rsplit(".", 1)
        prefix += f' xmlns:t="using:{namespace}"'
        target = 't:' + simple
        element = f'<t:{simple} xmlns:t="using:{namespace}" xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"/>'
    else:
        target = kind
        element = f'<{kind} xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"/>'
    xml = prefix + f'><Style TargetType="{target}">'
    for raw in rule["styles"]:
        name, value = raw.split("=", 1)
        for key, val in constants.items():
            value = value.replace('$' + key, val)
        if name.endswith(":"):
            xml += f'<Setter Property="{escape(name[:-1], quote=True)}"><Setter.Value>{value}</Setter.Value></Setter>'
        else:
            xml += f'<Setter Property="{escape(name, quote=True)}" Value="{escape(value, quote=True)}"/>'
    xml += '</Style></ResourceDictionary>'
    label = rule['target'] + ('::' + rule['state'] if 'state' in rule else '')
    literals = [f'LR"TEST({s})TEST"' for s in [label, xml, element]]
    output.append('{' + ','.join(literals) + '},')
output.append('};')
(root / "build/xaml_cases.inc").write_text('\n'.join(output), encoding='utf-8')
print('Generated', len(expanded_rules), 'XAML setter/state cases')
