"""Offline protocol, recovery, experiment balance, and analysis checks."""
import csv
import json
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch

from collect import Session, SPORTS, RAW, WIRE, Link, acquire, parse_rows, make_order
from analyze import features, category_stats

DURATIONS = (500, 2000, 500, 3000)


def sample_lines(n=300):
    lines = [','.join(WIRE)]
    for i in range(n):
        phase = 0 if i < 50 else 1 if i < 250 else 2
        lines.append(f'{i},{1000000+i*10000},{i*10000},{phase},0,0,9.80665,0,0,0,0')
    return lines


class FakeSerial:
    def __init__(self, content, chunk=17):
        self.data = content.encode('ascii')
        self.chunk = chunk
        self.sent = b''

    @property
    def in_waiting(self):
        return len(self.data)

    def read(self, n):
        result, self.data = self.data[:min(n, self.chunk)], self.data[min(n, self.chunk):]
        return result

    def write(self, data):
        self.sent += data


class CollectionTests(unittest.TestCase):
    def test_balanced_reproducible_order(self):
        for sport, categories in SPORTS.items():
            order = make_order(sport, 4760)
            self.assertEqual(order, make_order(sport, 4760))
            self.assertNotEqual(order, make_order(sport, 4761))
            self.assertEqual(len(order), 10*len(categories))
            for category in categories:
                self.assertEqual(sorted(x['repetition'] for x in order if x['category']==category), list(range(1,11)))

    def test_fragmented_and_burst_protocol(self):
        packet = 'ACK,trial\nBEGIN,trial,300,0,0,0\n' + '\n'.join(sample_lines()) + '\nEND,trial\n'
        for chunk in (1, 17, 100000):
            serial = FakeSerial(packet, chunk)
            rows, quality = acquire(Link(serial), 'trial', 'bowling', 'straight_normal', DURATIONS)
            self.assertFalse(quality['warning'])
            self.assertAlmostEqual(quality['measured_hz'], 100)
            self.assertEqual(len(rows), 300)
            self.assertTrue(serial.sent.startswith(b'START trial 0 0 '))

    def test_missing_sample_flagged(self):
        lines = sample_lines()
        del lines[100]
        packet = 'ACK,trial\nBEGIN,trial,299,1,0,0\n' + '\n'.join(lines) + '\nEND,trial\n'
        rows, quality = acquire(Link(FakeSerial(packet)), 'trial', 'bowling', 'straight_normal', DURATIONS)
        self.assertTrue(quality['warning'])
        self.assertEqual(quality['sequence_gaps'], 1)

    def test_aim_session_separate_and_resumable(self):
        with tempfile.TemporaryDirectory() as directory:
            original = Path(directory)/'cup_pong.csv'
            original.write_text('existing toss data')
            session = Session(directory,'cup_pong_aim')
            self.assertEqual(len(session.meta['order']),30)
            self.assertEqual(session.csv.name,'cup_pong_aim.csv')
            bias = [0]*6
            cal = session.calibration(bias)
            rows = parse_rows(sample_lines(),300,DURATIONS)
            session.accept(29,session.attempt(),rows,cal,bias,{})
            resumed = Session(directory,'cup_pong_aim')
            self.assertEqual(set(resumed.accepted),{29})
            self.assertEqual(original.read_text(),'existing toss data')
        packet = 'ACK,trial\nBEGIN,trial,300,0,0,0\n' + '\n'.join(sample_lines()) + '\nEND,trial\n'
        device = FakeSerial(packet)
        acquire(Link(device),'trial','cup_pong_aim','aim_right',DURATIONS)
        self.assertTrue(device.sent.startswith(b'START trial 3 2 '))

    def test_corruption_rejected(self):
        cases = []
        lines = sample_lines(); lines[1] = lines[1].replace('9.80665','nan'); cases.append(lines)
        lines = sample_lines(); lines[2] = lines[1]; cases.append(lines)
        lines = sample_lines(); lines[1] = 'garbage'; cases.append(lines)
        lines = sample_lines(); lines[51] = lines[51].replace(',1,0,0,9.80665',',0,0,0,9.80665'); cases.append(lines)
        for lines in cases:
            with self.assertRaises(ValueError):
                parse_rows(lines,300,DURATIONS)
        with self.assertRaises(TimeoutError):
            Link(FakeSerial('partial')).line(time.monotonic()+0.001)

    def test_session_recovery_and_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            session = Session(directory,'bowling',orientation='+X forward')
            bias = [0,0,0,0,0,0]
            cal = session.calibration(bias)
            rejected = session.attempt()
            self.assertEqual(len(session.accepted),0)
            trial = session.attempt()
            self.assertNotEqual(rejected,trial)
            rows = parse_rows(sample_lines(),300,DURATIONS)
            # Simulate process failure after durable journal, before CSV replacement.
            with patch.object(session,'rebuild_csv',side_effect=OSError('disk interrupted')):
                with self.assertRaises(OSError):
                    session.accept(0,trial,rows,cal,bias,{'warning':False})
            resumed = Session(directory,'bowling',seed=999)
            self.assertEqual(len(resumed.accepted),1)
            self.assertEqual(resumed.meta['seed'],4760)
            with resumed.csv.open(newline='') as f:
                saved = list(csv.DictReader(f))
            self.assertEqual(len(saved),300)
            self.assertEqual(saved[0]['phase'],'pre')
            self.assertEqual(saved[50]['phase'],'motion')
            self.assertAlmostEqual(float(saved[0]['corrected_az_mps2']),9.80665)
            feature = features(saved)
            self.assertAlmostEqual(feature['peak_accel_mps2'],9.80665)
            self.assertEqual(feature['motion_duration_s'],0)
            self.assertEqual(category_stats([feature])[0]['trials'],1)
            with self.assertRaises(ValueError):
                resumed.accept(0,trial,rows,cal,bias,{})

    def test_unknown_data_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'bowling.csv'
            path.write_text('do not overwrite')
            with self.assertRaises(ValueError):
                Session(directory,'bowling')
            self.assertEqual(path.read_text(),'do not overwrite')


if __name__=='__main__':
    unittest.main()
