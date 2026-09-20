import json
from pathlib import Path
import tempfile
import unittest

from compare_dumps import load_dump, effective_properties, matching_nodes


class DumpTests(unittest.TestCase):
    def read(self, records, tail=""):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "dump.jsonl"
            path.write_text('\n'.join(json.dumps(row) for row in records) + tail, encoding='utf-8')
            return load_dump(path)

    def snapshot(self, seq, name='Time'):
        return [{"event": "snapshotBegin", "sequence": seq, "attempted": 1},
                {"event": "snapshot", "sequence": seq, "handle": 123, "name": name, "properties": []},
                {"event": "snapshotEnd", "sequence": seq, "captured": 1, "failures": 0}]

    def test_last_complete_snapshot_is_not_merged_with_incomplete(self):
        data = self.read([{"event": "header", "schema": 4}] + self.snapshot(1) + self.snapshot(2, 'Date')[:2])
        self.assertEqual(data['sequence'], 1)
        self.assertEqual([n['name'] for n in data['nodes']], ['Time'])
        self.assertTrue(data['warnings'])

    def test_rejects_count_mismatch(self):
        data = self.snapshot(1)
        data[-1]['captured'] = 2
        with self.assertRaisesRegex(ValueError, 'no complete snapshot'):
            self.read(data)

    def test_partial_final_line_preserves_complete_snapshot(self):
        data = self.read(self.snapshot(1), '\n{"event":')
        self.assertEqual(data['sequence'], 1)
        self.assertIn('Ignored incomplete final line', data['warnings'])

    def test_interior_corruption_is_not_silently_discarded(self):
        with self.assertRaisesRegex(ValueError, 'malformed JSON'):
            self.read(self.snapshot(1), '\n{broken}\n{}')

    def test_multiple_sessions_cannot_collide_on_sequence_numbers(self):
        with self.assertRaisesRegex(ValueError, 'Multiple sessions'):
            self.read([{"event": "header", "schema": 4}] + self.snapshot(1) + [{"event": "header", "schema": 4}])

    def test_preserves_winning_chain_value_not_default_tail(self):
        result = effective_properties({'properties': [
            {'name': 'FontSize', 'value': '128'}, {'name': 'FontSize', 'value': '11'}]})
        self.assertEqual(result['FontSize'], '128')

    def test_evaluated_binding_and_overridden_flags(self):
        result = effective_properties({'properties': [
            {'name': 'Text', 'value': '(binding)', 'overridden': False},
            {'name': 'Opacity', 'value': '0', 'overridden': True},
            {'name': 'Opacity', 'value': '1', 'overridden': False}], 'effective': {'Text': '3:27'}})
        self.assertEqual(result, {'Text': '3:27', 'Opacity': '1'})

    def test_index_is_sibling_index_not_type_ordinal(self):
        nodes = [
            {'handle': 1, 'parent': 0, 'childIndex': 0, 'runtimeType': 'Windows.UI.Xaml.Controls.Grid', 'name': 'Root'},
            {'handle': 2, 'parent': 1, 'childIndex': 0, 'runtimeType': 'Windows.UI.Xaml.Controls.Border', 'name': ''},
            {'handle': 3, 'parent': 1, 'childIndex': 1, 'runtimeType': 'Windows.UI.Xaml.Controls.TextBlock', 'name': ''}]
        self.assertEqual(matching_nodes('Grid#Root > TextBlock[1]', nodes), [])
        self.assertEqual(matching_nodes('Grid#Root > TextBlock[2]', nodes), [nodes[2]])
        self.assertEqual(matching_nodes('* > TextBlock', nodes), [nodes[2]])


if __name__ == '__main__':
    unittest.main()
