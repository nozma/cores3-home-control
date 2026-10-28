import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from remo_ir_capture import extract


def frame(bit):
    return [3450, 1770] + [value for _ in range(40) for value in (380, 1360 if bit else 490)] + [380]


class CaptureTest(unittest.TestCase):
    def test_recovers_intact_repeats_after_damaged_frame_and_truncation(self):
        good = frame(True)
        damaged = frame(False)
        damaged[10] = 1500
        values = good + [65535, 0, 9600] + damaged + [65535, 0, 9600] + good + [65535, 0, 9600] + good + [65535, 0, 9600] + good
        raw = json.dumps({'freq': 36, 'format': 'us', 'data': values}).encode()[:-2]
        bits, message = extract(raw)
        self.assertEqual(bits, '1' * 40)
        self.assertEqual(len(message['data']), 427)
        self.assertLessEqual(len(json.dumps(message, separators=(',', ':')).encode()), 2048)

    def test_does_not_treat_different_commands_as_repeats(self):
        raw = json.dumps({'freq': 36, 'format': 'us', 'data': frame(True) + [65535, 0, 9600] + frame(False)}).encode()
        self.assertIsNone(extract(raw))

    def test_requires_complete_repeat_pair(self):
        raw = json.dumps({'freq': 36, 'format': 'us', 'data': frame(True)}).encode()
        self.assertIsNone(extract(raw))


if __name__ == '__main__':
    unittest.main()
