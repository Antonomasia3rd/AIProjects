"""Windhawk settings export formats (standard library only).

The Settings tab's YAML editor accepts nested data with 0/1 switches.
Advanced > Mod settings accepts a flat JSON map of registry-style leaf keys.
"""
import json
from pathlib import Path


def numeric_switches(value):
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, dict):
        return {key: numeric_switches(item) for key, item in value.items()}
    if isinstance(value, list):
        return [numeric_switches(item) for item in value]
    if isinstance(value, (str, int)):
        return value
    raise TypeError(f'Unsupported Windhawk settings value: {type(value).__name__}')


def flatten_settings(value, prefix=''):
    result = {}
    if isinstance(value, dict):
        for key, item in value.items():
            result.update(flatten_settings(item, f'{prefix}.{key}' if prefix else key))
    elif isinstance(value, list):
        for index, item in enumerate(value):
            result.update(flatten_settings(item, f'{prefix}[{index}]'))
    else:
        value = numeric_switches(value)
        if not isinstance(value, (str, int)):
            raise TypeError('Raw settings must contain only strings and integers')
        result[prefix] = value
    return result


def yaml_scalar(value):
    if isinstance(value, int):
        return str(value)
    if not isinstance(value, str):
        raise TypeError('Expected scalar')
    if any(ord(ch) < 32 for ch in value):
        return json.dumps(value, ensure_ascii=False)
    return "'" + value.replace("'", "''") + "'"


def yaml_lines(value, indent=0):
    pad = ' ' * indent
    lines = []
    if isinstance(value, dict):
        for key, item in value.items():
            if isinstance(item, (dict, list)):
                lines.append(f'{pad}{key}:')
                lines.extend(yaml_lines(item, indent + 2))
            else:
                lines.append(f'{pad}{key}: {yaml_scalar(item)}')
    elif isinstance(value, list):
        for item in value:
            if isinstance(item, dict):
                block = yaml_lines(item, indent + 2)
                lines.append(pad + '- ' + block[0][indent + 2:])
                lines.extend(block[1:])
            elif isinstance(item, list):
                lines.append(pad + '-')
                lines.extend(yaml_lines(item, indent + 2))
            else:
                lines.append(pad + '- ' + yaml_scalar(item))
    else:
        raise TypeError('Expected mapping or list')
    return lines


def export_settings(settings, root, stem, yaml_name):
    root = Path(root)
    nested = numeric_switches(settings)
    flat = flatten_settings(nested)
    yaml_text = '\n'.join(yaml_lines(nested)) + '\n'
    (root / yaml_name).write_text(yaml_text, encoding='utf-8')
    (root / yaml_name.replace('.yaml', '.txt')).write_text(yaml_text, encoding='utf-8')
    (root / (stem + '.preset.json')).write_text(json.dumps(nested, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    (root / (stem + '.advanced-settings.json')).write_text(json.dumps(flat, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return flat


def load_preset(path):
    """Read canonical JSON, or the companion JSON for one of our YAML exports."""
    path = Path(path)
    text = path.read_text(encoding='utf-8-sig')
    if text.lstrip().startswith('{'):
        return json.loads(text)
    endings = {'.wh.preferences.yaml': '', '.wh.preferences.txt': '',
               '.capture.preferences.yaml': '.capture', '.capture.preferences.txt': '.capture',
               '.preferences-only.yaml': '.preferences-only', '.preferences-only.txt': '.preferences-only'}
    for ending, extra in endings.items():
        if path.name.endswith(ending):
            companion = path.with_name(path.name[:-len(ending)] + extra + '.preset.json')
            if companion.exists():
                data = json.loads(companion.read_text(encoding='utf-8-sig'))
                expected = '\n'.join(yaml_lines(numeric_switches(data))).strip()
                if text.strip() != expected:
                    raise ValueError('YAML differs from its companion JSON; pass an updated .preset.json file to the CLI')
                return data
    raise ValueError('Pass the .preset.json file; arbitrary YAML input is not supported by this offline CLI')
