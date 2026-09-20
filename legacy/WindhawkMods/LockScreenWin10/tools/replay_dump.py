"""Materialize schema-5 change records at a chosen capture timestamp.

The full baseline property chain is retained; effective values contain updates.
This replays recorded notifications, not unreported compositor/layout frames.
"""
import copy

LIVE_EVENTS = {'liveElement', 'propertyChanged', 'geometryChanged', 'stateChanging',
               'stateChanged', 'liveStart', 'liveReady', 'liveStop', 'add', 'remove',
               'outputGap', 'monitorError', 'captureStopped', 'viewChanged'}


def replay_nodes(baseline, events, at_ms):
    nodes = {node['handle']: copy.deepcopy(node) for node in baseline}
    for event in events:
        if event.get('elapsedMs', 0) > at_ms:
            continue
        kind, handle = event.get('event'), event.get('handle')
        if kind == 'add' and handle not in nodes:
            parent, index = event.get('parent', 0), event.get('childIndex', 0)
            for sibling in nodes.values():
                if sibling.get('parent') == parent and sibling.get('childIndex', 0) >= index:
                    sibling['childIndex'] += 1
            nodes[handle] = {**event, 'properties': [], 'effective': {}}
        elif kind == 'liveElement':
            node = nodes.setdefault(handle, {'handle': handle, 'properties': []})
            node.update({key: value for key, value in event.items()
                         if key not in {'event', 'elapsedMs', 'properties', 'effective'}})
            effective = node.setdefault('effective', {})
            effective.update({prop['name']: prop['value'] for prop in event.get('properties', [])})
            effective.update(event.get('effective') or {})
        elif kind == 'remove' and handle in nodes:
            removed = nodes[handle]
            pending = [handle]
            while pending:
                current = pending.pop()
                pending.extend(key for key, node in nodes.items() if node.get('parent') == current and key != current)
                nodes.pop(current, None)
            for sibling in nodes.values():
                if sibling.get('parent') == removed.get('parent') and sibling.get('childIndex', 0) > removed.get('childIndex', 0):
                    sibling['childIndex'] -= 1
        elif handle in nodes:
            node = nodes[handle]
            if kind == 'propertyChanged':
                node.setdefault('effective', {})[event['property']] = event['value']
            elif kind == 'geometryChanged':
                node['rectangle'] = event['rectangle']
            elif kind == 'viewChanged':
                node.setdefault('effective', {}).update({'HorizontalOffset': event['horizontalOffset'],
                    'VerticalOffset': event['verticalOffset'], 'ZoomFactor': event['zoomFactor']})
            elif kind == 'stateChanged':
                groups = node.setdefault('visualStateGroups', [])
                group = next((g for g in groups if g['name'] == event['group']), None)
                if group is None:
                    group = {'name': event['group'], 'states': []}
                    groups.append(group)
                group['current'] = event['newState']
    return list(nodes.values())
