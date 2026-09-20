import unittest
from replay_dump import replay_nodes


class ReplayTests(unittest.TestCase):
    def test_cutoff_and_baseline_preservation(self):
        baseline = [{'handle': 1, 'properties': [{'name': 'Text', 'value': 'before'}]}]
        events = [{'event': 'propertyChanged', 'elapsedMs': 10, 'handle': 1, 'property': 'Text', 'value': 'after'}]
        self.assertNotIn('effective', replay_nodes(baseline, events, 9)[0])
        self.assertEqual(replay_nodes(baseline, events, 10)[0]['effective']['Text'], 'after')
        self.assertNotIn('effective', baseline[0])

    def test_tree_insertion_removal_and_descendants(self):
        baseline = [{'handle': 1, 'parent': 0, 'childIndex': 0}, {'handle': 2, 'parent': 1, 'childIndex': 0}]
        events = [{'event': 'add', 'elapsedMs': 1, 'handle': 3, 'parent': 1, 'childIndex': 0},
                  {'event': 'add', 'elapsedMs': 2, 'handle': 4, 'parent': 3, 'childIndex': 0},
                  {'event': 'remove', 'elapsedMs': 3, 'handle': 3}]
        mid = {node['handle']: node for node in replay_nodes(baseline, events, 2)}
        self.assertEqual(mid[2]['childIndex'], 1)
        end = {node['handle']: node for node in replay_nodes(baseline, events, 3)}
        self.assertEqual(set(end), {1, 2})
        self.assertEqual(end[2]['childIndex'], 0)

    def test_geometry_state_and_scroll(self):
        events = [{'event': 'geometryChanged', 'handle': 1, 'rectangle': {'x': 36, 'y': 20}},
                  {'event': 'stateChanged', 'handle': 1, 'group': 'CommonStates', 'newState': 'PointerOver'},
                  {'event': 'viewChanged', 'handle': 1, 'horizontalOffset': 0, 'verticalOffset': 50, 'zoomFactor': 1}]
        node = replay_nodes([{'handle': 1}], events, 100)[0]
        self.assertEqual(node['rectangle']['y'], 20)
        self.assertEqual(node['visualStateGroups'][0]['current'], 'PointerOver')
        self.assertEqual(node['effective']['VerticalOffset'], 50)


if __name__ == '__main__':
    unittest.main()
