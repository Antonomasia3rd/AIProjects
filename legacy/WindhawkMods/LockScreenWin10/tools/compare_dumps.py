"""Compare complete LockApp snapshots; create a searchable, offline HTML explorer.

python tools/compare_dumps.py reference.jsonl candidate.jsonl --output comparison.html
No third-party Python packages or network access are needed.
"""
import argparse
from collections import Counter, defaultdict
import html
import json
from pathlib import Path
from settings_formats import load_preset
from replay_dump import LIVE_EVENTS, replay_nodes


def load_dump(path, at_ms=None):
    snapshots, starts, ends, trace = defaultdict(list), {}, {}, []
    header = {}
    warnings = []
    live_events, end_lines = [], {}
    lines = Path(path).read_text(encoding="utf-8-sig").splitlines()
    for number, line in enumerate(lines, 1):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError as error:
            if number != len(lines):
                raise ValueError(f"{path}:{number}: malformed JSON: {error}") from error
            warnings.append("Ignored incomplete final line")
            continue
        event = row.get("event")
        if event == "header":
            if header:
                raise ValueError("Multiple sessions in one file; split the file at its headers first")
            header = row
        elif event == "snapshot":
            snapshots[row["sequence"]].append(row)
        elif event == "snapshotBegin":
            starts[row["sequence"]] = row
        elif event == "snapshotEnd":
            ends[row["sequence"]] = row
            end_lines[row['sequence']] = number
        elif event == "interaction":
            trace.append(row)
        if event in LIVE_EVENTS:
            live_events.append((number, row))
    complete = [seq for seq, rows in snapshots.items()
                if seq in starts and seq in ends and len(rows) == ends[seq].get("captured")]
    if not complete:
        raise ValueError(f"{path}: no complete snapshot (begin/records/end counts must agree)")
    sequence = max(complete)
    if sequence != max(snapshots):
        warnings.append("Using the last complete snapshot; a later snapshot was incomplete")
    if header.get("schema", 0) < 4:
        warnings.append("Schema 3: missing brush opacity, evaluated bindings and live sibling indices; first property-chain entry is used")
    if ends[sequence].get("failures"):
        warnings.append(f"Snapshot reports {ends[sequence]['failures']} element failures")
    live = [row for number, row in live_events if number > end_lines[sequence]] if header.get('schema', 0) >= 5 else []
    for kind, message in [('outputGap', 'The file reports dropped records; continuous coverage is incomplete'),
                          ('monitorError', 'Some elements could not be monitored; continuous coverage is incomplete'),
                          ('captureStopped', 'Recording stopped at the configured file-size limit')]:
        if any(row.get('event') == kind for _, row in live_events): warnings.append(message)
    baseline = snapshots[sequence]
    baseline_ms = ends[sequence].get('elapsedMs', 0)
    nodes = baseline
    if at_ms is not None:
        if at_ms < baseline_ms:
            warnings.append('Requested time precedes the baseline; showing the baseline time')
        nodes = replay_nodes(baseline, live, max(at_ms, baseline_ms))
    return {"file": Path(path).name, "header": header, "sequence": sequence,
            "end": ends[sequence], "nodes": nodes, "baselineNodes": baseline, "baselineMs": baseline_ms,
            "replayMs": max(at_ms, baseline_ms) if at_ms is not None else None,
            "liveEvents": live, "trace": trace + live,
            "warnings": warnings}


def effective_properties(node):
    result = {}
    for prop in node.get("properties", []):
        if prop.get("overridden") is True:
            continue
        result.setdefault(prop["name"], prop["value"])
    result.update(node.get("effective") or {})
    return result


def short_type(name):
    prefix = "Windows.UI.Xaml.Controls."
    if name.startswith(prefix) and "." not in name[len(prefix):]:
        return name[len(prefix):]
    if name == "Windows.UI.Xaml.Shapes.Rectangle":
        return "Rectangle"
    return name


def segment_matches(segment, node):
    import re
    match = re.fullmatch(r"([^#\[@]+)(?:#([^\[@]+))?(?:\[(\d+)\])?(?:@[^ ]+)?", segment.strip())
    if not match:
        raise ValueError(f"Unsupported selector segment in audit: {segment}")
    kind, name, index = match.groups()
    types = [short_type(node.get("runtimeType", "")), short_type(node.get("declaredType", ""))]
    return ((kind == "*" or kind in types) and (name is None or node.get("name") == name)
            and (index is None or node.get("childIndex") == int(index) - 1))


def matching_nodes(target, nodes):
    segments = target.split(" > ")
    by_handle = {n["handle"]: n for n in nodes}
    result = []
    for candidate in nodes:
        node = candidate
        for segment in reversed(segments):
            if node is None or not segment_matches(segment, node):
                break
            node = by_handle.get(node.get("parent"))
        else:
            result.append(candidate)
    return result


def selector_audit(preset, dump):
    result = []
    for rule in preset["controlStyles"]:
        target = rule["target"]
        matches = matching_nodes(target, dump["nodes"])
        result.append({"target": target, "matches": len(matches),
                       "hasIndex": "[" in target,
                       "note": "Created by the Windows 10 helper at runtime" if "#Win10" in target else
                               ("Indices from schema 3 may be stale" if "[" in target and dump["header"].get("schema", 0) < 4 else "")})
    return result


def load_template():
    directory = Path(__file__).parent
    return (directory/'explorer.template.html').read_text(encoding='utf-8').replace(
        '/*LIVE_REPLAY*/', (directory/'replay.js').read_text(encoding='utf-8'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", type=Path, default=Path("comparison.html"))
    parser.add_argument("--preset", type=Path, help="Optional canonical .preset.json or our YAML export with its companion JSON")
    parser.add_argument('--at-ms', type=int, help='Replay recorded changes at this many milliseconds after watcher startup')
    args = parser.parse_args()
    left, right = load_dump(args.reference, args.at_ms), load_dump(args.candidate, args.at_ms)
    audit = selector_audit(load_preset(args.preset), right) if args.preset else []
    data = json.dumps({"dumps": [left, right], "audit": audit}, ensure_ascii=True).replace("<", "\\u003c")
    template = load_template()
    args.output.write_text(template.replace("/*DATA*/null", data), encoding="utf-8")
    summary = {"reference": {k: left[k] for k in ["file", "sequence", "end", "warnings"]},
               "candidate": {k: right[k] for k in ["file", "sequence", "end", "warnings"]},
               "selectorAudit": audit}
    args.output.with_suffix(".audit.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"Reference: {len(left['nodes'])} nodes. Candidate: {len(right['nodes'])} nodes.")
    if audit:
        unmatched = [r["target"] for r in audit if not r["matches"]]
        print(f"Selector audit: {len(audit) - len(unmatched)}/{len(audit)} match the supplied candidate snapshot.")
        for target in unmatched:
            print("NO MATCH:", target)
    print("Wrote", args.output)


if __name__ == "__main__":
    main()
