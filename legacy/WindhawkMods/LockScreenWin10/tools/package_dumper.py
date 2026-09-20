"""Package the standalone continuous dumper without replacing the styler release."""
import hashlib
import json
from pathlib import Path
import zipfile

root = Path(__file__).resolve().parent.parent
source = root/'builds/lockapp-xaml-dumper.wh.cpp'
builds = json.loads((root/'build/build-inputs.json').read_text(encoding='utf-8-sig'))
entry = next(row for row in builds if row['source'] == 'builds/lockapp-xaml-dumper.wh.cpp')
assert hashlib.sha256(source.read_bytes()).hexdigest().upper() == entry['sha256'], 'Uncompiled dumper change'
log = (root/'build/dumper-validation.log').read_text(encoding='utf-8-sig')
assert 'PASS: real DP/brush/state callbacks' in log and 'FAIL' not in log
files = ['builds/lockapp-xaml-dumper.wh.cpp','LICENSE','markdowns/DUMPER-0.5.0.md',
         'reports/dump-explorer.html','sources/lockapp-xaml-dumper.wh.cpp',
         'build/dumper-validation.log','build/continuous-validation.log',
         'tools/prepare_dumper.py','tools/settings_formats.py','tools/compare_dumps.py',
         'tools/replay_dump.py','tools/replay.js','tools/explorer.template.html',
         'tools/validate_dumper.cpp','tools/validate-dumper.ps1','tools/xaml-test.manifest',
         'tools/validate_live_output.py','tools/validate_replay.cjs','tools/test_compare_dumps.py','tools/test_replay.py']
files += [p.relative_to(root).as_posix() for p in (root/'tools').glob('dumper_*.inc')]
files += [p.relative_to(root).as_posix() for p in (root/'preferences').glob('lockapp-xaml-dumper.capture.*')]
archive = root/'releases/LockApp-XamlDumper-0.5.0.zip'
prefix = Path('LockApp-XamlDumper-0.5.0')
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as package:
    for name in files: package.write(root/name,prefix/name)
    package.write(root/'markdowns/DUMPER-0.5.0.md',prefix/'README.md')
    package.writestr((prefix/'build/dumper-build-input.json').as_posix(),json.dumps(entry,indent=2)+'\n')
with zipfile.ZipFile(archive) as package:
    assert package.testzip() is None
print(f'Packaged {archive.name}: {archive.stat().st_size:,} bytes; original candidate-4 archive preserved')
