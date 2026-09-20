"""Create a source-only distribution after verifying the final build hashes."""
from pathlib import Path
import hashlib
import json
import re
import zipfile

root = Path(__file__).resolve().parent.parent
inputs = json.loads((root / 'build/build-inputs.json').read_text(encoding='utf-8-sig'))
if len(inputs) != 2:
    raise RuntimeError('Expected two completed mod builds')
for entry in inputs:
    actual = hashlib.sha256((root / entry['source']).read_bytes()).hexdigest().upper()
    if actual != entry['sha256']:
        raise RuntimeError('Uncompiled source change: ' + entry['source'])
files = [root / p for p in ['README.md','LICENSE','reports/dump-explorer.html']]
for directory in ['builds','preferences','markdowns']:
    files += [p for p in (root / directory).rglob('*') if p.is_file()]
# Include the immutable inputs needed by the assembly scripts. Dumps, images,
# logs from the user's actual LockApp sessions and cached symbols stay private.
files += [root / 'sources' / name for name in ['windows-11-lockapp-styler.wh.cpp',
          'windows-11-lockapp-styler.wh.preferences.mine.txt','lockapp-xaml-dumper.wh.cpp']]
files += [p for p in (root / 'tools').iterdir()
          if p.is_file() and p.suffix in ['.py','.ps1','.inc','.cpp','.html','.manifest','.cjs','.js']
          and p.name not in ['xaml_probe.cpp','organize_project.ps1']]
files += [root / 'build/build-inputs.json']
for name in ['xaml-validation.log','settings-formats-validation.log','helper-validation.log','dumper-validation.log']:
    log = root / 'build' / name
    content = log.read_text(encoding='utf-8-sig')
    if 'PASS' not in content or 'FAIL' in content:
        raise RuntimeError('Missing or failed validation: ' + name)
    files.append(log)
version = re.search(r'^// @version\s+(\S+)', (root/'builds/lockapp-xaml-dumper.wh.cpp').read_text(encoding='utf-8'), re.M).group(1)
label = 'Windows10-LockScreen-Candidate4' + ('' if version == '0.4.1' else '-Dumper' + version)
archive = root / ('releases/' + label + '.zip')
archive.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for path in files:
        z.write(path, Path(label) / path.relative_to(root))
with zipfile.ZipFile(archive, 'r') as z:
    bad = z.testzip()
    if bad:
        raise RuntimeError('Archive verification failed: ' + bad)
print(f'Packaged {len(files)} source/documentation files: {archive.name} ({archive.stat().st_size:,} bytes)')
