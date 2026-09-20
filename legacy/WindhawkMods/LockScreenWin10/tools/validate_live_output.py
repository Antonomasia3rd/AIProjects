"""Verify actual callback output from the isolated C++ test, plus browser replay."""
import json
from pathlib import Path
from compare_dumps import load_dump, effective_properties
from replay_dump import replay_nodes

root = Path(__file__).resolve().parent.parent
manifest = json.loads((root/'build/live-test-manifest.json').read_text(encoding='utf-8'))
rows = [json.loads(line) for line in Path(manifest['stream']).read_text(encoding='utf-8').splitlines()]
text_changes = [r['value'] for r in rows if r.get('handle') == 11 and r.get('property') == 'Text']
assert text_changes == [f'event-{i}' for i in range(200)], 'A real property callback was lost or reordered'
assert any(r.get('property') == 'Margin' and r['value'] == '1,2,3,4' for r in rows)
brush = [r['value'] for r in rows if r.get('handle') == 12 and r.get('property') == 'Background']
assert any('TintOpacity=0.4' in value for value in brush), 'In-place brush changes were not observed'
assert any('TintOpacity=0.2' in value for value in brush), 'Replacement brush was not observed'
old_detached = next(i for i, row in enumerate(rows) if row['event'] == 'oldBrushDetached')
assert not any(r.get('handle') == 12 and r['event'] == 'propertyChanged' for r in rows[old_detached+1:])
assert [r['newState'] for r in rows if r['event'] == 'stateChanged'] == ['PointerOver', 'Normal']
stopped = next(i for i, row in enumerate(rows) if row['event'] == 'observersStopped')
assert not any(r['event'] in ['propertyChanged', 'stateChanged', 'geometryChanged'] for r in rows[stopped+1:])
overflow = [json.loads(line) for line in Path(manifest['overflow']).read_text(encoding='utf-8').splitlines()]
assert overflow == [{'event': 'outputGap', 'droppedRecords': 1}]
quota_path = Path(manifest['quota'])
quota = [json.loads(line) for line in quota_path.read_text(encoding='utf-8').splitlines()]
assert quota == [{'event': 'captureStopped', 'reason': 'fileSizeLimit'}] and quota_path.stat().st_size <= 256
dump = load_dump(manifest['stream'])
latest = replay_nodes(dump['baselineNodes'], dump['liveEvents'], 2**53-1)
assert effective_properties(next(n for n in latest if n['handle'] == 11))['Text'] == 'event-199'
(root/'build/replay-fixture.json').write_text(json.dumps({'baseline': dump['baselineNodes'], 'events': dump['liveEvents'], 'expected': latest}), encoding='utf-8')
print('PASS: 200 ordered text callbacks, in-place/replaced brush changes, states, teardown, queue overflow, quota and replay')
