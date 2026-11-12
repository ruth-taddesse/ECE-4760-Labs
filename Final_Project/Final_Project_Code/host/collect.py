"""Pico Sports protocol v2: randomized, resumable experiments. Python 3 + pyserial."""
import argparse
import csv
import io
import json
import math
import os
from pathlib import Path
import random
import time
import uuid

SPORTS = {
    'bowling': ['straight_normal', 'straight_slow', 'straight_fast', 'hook_left', 'hook_right'],
    'basketball': ['normal_shot', 'high_arc', 'low_arc', 'strong_shot', 'weak_shot'],
    'cup_pong': ['normal_toss', 'high_lob', 'low_flat', 'strong_toss', 'gentle_toss'],
    'cup_pong_aim': ['aim_left', 'aim_center', 'aim_right'],
}
RAW = ['ax_mps2', 'ay_mps2', 'az_mps2', 'gx_rads', 'gy_rads', 'gz_rads']
WIRE = ['seq', 'pico_us', 'elapsed_us', 'phase', *RAW, 'near_rail']
FIELDS = ['session_id', 'sport', 'trial_id', 'trial_number', 'throw_category',
          'repetition', 'calibration_id', *WIRE, *['corrected_' + c for c in RAW]]


def atomic_text(path, text):
    """Write and fsync before atomic replacement; only for files owned by this session."""
    path = Path(path)
    tmp = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    with tmp.open('x', encoding='utf-8', newline='') as f:
        f.write(text)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def save_json(path, value):
    atomic_text(path, json.dumps(value, indent=2) + '\n')


def make_order(sport, seed):
    order = [{'category': c, 'repetition': r}
             for c in SPORTS[sport] for r in range(1, 11)]
    random.Random(seed).shuffle(order)
    return order


class Session:
    """Accepted JSON journals are authoritative; the sport CSV is rebuilt from them.

    A crash after journaling but before CSV update is recovered on resume.
    Existing sessions are always resumed, never reset or silently overwritten.
    """
    def __init__(self, root, sport, seed=4760, durations=(500, 2000, 500, 3000), orientation=None):
        self.root, self.sport = Path(root), sport
        self.root.mkdir(parents=True, exist_ok=True)
        self.path = self.root / f'{sport}.session.json'
        self.csv = self.root / f'{sport}.csv'
        self.journal = self.root / f'{sport}_accepted'
        if self.path.exists():
            self.meta = json.loads(self.path.read_text())
            if self.meta.get('schema') != 2 or self.meta['sport'] != sport:
                raise ValueError('Incompatible session metadata; use a different --data directory')
            print('Resuming saved order, durations, and orientation; new CLI values are ignored.')
        else:
            if self.csv.exists() or self.journal.exists():
                raise ValueError('Existing data without session metadata: refusing to overwrite')
            self.meta = dict(schema=2, session_id=uuid.uuid4().hex, sport=sport,
                             seed=seed, order=make_order(sport, seed), durations_ms=list(durations),
                             orientation=orientation or 'not specified', imu='MPU6050', board='pico2',
                             sample_hz=100, accel_range_g=8, gyro_range_dps=2000, dlpf=3,
                             timestamp='Pico monotonic microseconds just before I2C read; resets on boot',
                             release_marker=None, calibrations=[], next_attempt=1)
            save_json(self.path, self.meta)
        self.journal.mkdir(exist_ok=True)
        self.accepted = {}
        for path in sorted(self.journal.glob('*.json')):
            record = json.loads(path.read_text())
            index = record['index']
            if (record['session_id'] != self.meta['session_id'] or index in self.accepted or
                    not 0 <= index < len(self.meta['order']) or record['plan'] != self.meta['order'][index]):
                raise ValueError(f'Invalid/duplicate accepted record: {path}')
            self.accepted[index] = record
        if self.csv.exists():
            known_trials = {record['trial_id'] for record in self.accepted.values()}
            with self.csv.open(newline='') as f:
                reader = csv.DictReader(f)
                if reader.fieldnames != FIELDS:
                    raise ValueError('Existing CSV schema is not owned by this collector; refusing replacement')
                for row in reader:
                    if row['session_id'] != self.meta['session_id'] or row['trial_id'] not in known_trials:
                        raise ValueError('CSV contains data outside this session journal; refusing replacement')
        self.rebuild_csv()

    def rebuild_csv(self):
        out = io.StringIO(newline='')
        writer = csv.DictWriter(out, fieldnames=FIELDS)
        writer.writeheader()
        for index in sorted(self.accepted):
            writer.writerows(self.accepted[index]['rows'])
        atomic_text(self.csv, out.getvalue())

    def calibration(self, bias):
        ident = len(self.meta['calibrations']) + 1
        self.meta['calibrations'].append(dict(id=ident, bias_si=bias, pose='+Z up, level, stationary',
                                             method='200-sample single-pose offset; scale not calibrated',
                                             time_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())))
        save_json(self.path, self.meta)
        return ident

    def attempt(self):
        n = self.meta['next_attempt']
        self.meta['next_attempt'] += 1
        save_json(self.path, self.meta)
        return f'{self.meta["session_id"]}-{n}'

    def accept(self, index, trial, rows, calibration_id, bias, quality):
        if index in self.accepted:
            raise ValueError('Trial position already accepted')
        plan = self.meta['order'][index]
        enriched = []
        for row in rows:
            result = dict(session_id=self.meta['session_id'], sport=self.sport, trial_id=trial,
                          trial_number=index+1, throw_category=plan['category'],
                          repetition=plan['repetition'], calibration_id=calibration_id, **row)
            result['phase'] = ['pre', 'motion', 'post'][row['phase']]
            result.update({'corrected_'+c: row[c]-b for c, b in zip(RAW, bias)})
            enriched.append(result)
        record = dict(session_id=self.meta['session_id'], index=index, trial_id=trial,
                      plan=plan, rows=enriched, quality=quality)
        destination = self.journal / f'{index+1:02d}.json'
        if destination.exists():
            raise ValueError('Accepted record exists; refusing overwrite')
        save_json(destination, record)
        self.accepted[index] = record
        self.rebuild_csv()


class Link:
    def __init__(self, device):
        self.device = device
        self.pending = bytearray()

    def send(self, command):
        self.device.write((command + '\n').encode('ascii'))

    def line(self, deadline):
        while time.monotonic() < deadline:
            if b'\n' in self.pending:
                raw, _, rest = self.pending.partition(b'\n')
                self.pending = bytearray(rest)
                return raw.decode('ascii').strip()
            self.pending.extend(self.device.read(min(512, self.device.in_waiting or 1)))
            if b'\n' not in self.pending and len(self.pending) > 4096:
                raise ValueError('Oversized/malformed packet')
        raise TimeoutError('Pico response timed out (disconnected, reset, or wrong firmware)')

    def response(self, prefix, timeout=5):
        deadline = time.monotonic() + timeout
        while True:
            line = self.line(deadline)
            if line.startswith('ERR,'):
                raise ValueError(line)
            if line.startswith(prefix):
                return line.split(',')


def parse_rows(lines, count, durations):
    if not lines or lines[0].split(',') != WIRE or len(lines)-1 != count or not 1 <= count <= 1000:
        raise ValueError('Missing samples, empty capture, or unexpected CSV header/count')
    rows = []
    previous = None
    pre, motion, post, _ = durations
    for text in lines[1:]:
        cells = text.split(',')
        if len(cells) != len(WIRE):
            raise ValueError('Malformed sample row')
        row = dict(zip(WIRE, cells))
        for key in ('seq', 'pico_us', 'elapsed_us', 'phase', 'near_rail'):
            row[key] = int(row[key])
        for key in RAW:
            row[key] = float(row[key])
            if not math.isfinite(row[key]):
                raise ValueError('Non-finite sensor measurement')
        expected_phase = 0 if row['elapsed_us'] < pre*1000 else 1 if row['elapsed_us'] < (pre+motion)*1000 else 2
        if (row['phase'] != expected_phase or row['near_rail'] not in (0, 1) or
                row['seq'] < 0 or row['pico_us'] < row['elapsed_us'] or
                not 0 <= row['elapsed_us'] < (pre+motion+post)*1000):
            raise ValueError('Invalid timestamp, sequence, phase, or flag')
        if previous and (row['seq'] <= previous['seq'] or row['pico_us'] <= previous['pico_us'] or
                         row['elapsed_us'] <= previous['elapsed_us'] or
                         row['pico_us']-row['elapsed_us'] != previous['pico_us']-previous['elapsed_us']):
            raise ValueError('Out-of-order or inconsistent timestamps/sequences')
        rows.append(row)
        previous = row
    return rows


def acquire(link, trial, sport, category, durations):
    link.send(f'START {trial} {list(SPORTS).index(sport)} {SPORTS[sport].index(category)} ' +
              ' '.join(map(str, durations)))
    ack = link.response('ACK,')
    if ack != ['ACK', trial]:
        raise ValueError('Wrong trial acknowledgement')
    print('Countdown started. Follow VGA: HOLD STILL -> THROW NOW -> HOLD STILL.')
    deadline = time.monotonic() + sum(durations)/1000 + 15
    begin = link.line(deadline).split(',')
    if len(begin) != 6 or begin[:2] != ['BEGIN', trial]:
        raise ValueError('Missing or mismatched trial header')
    count, errors, missed, stopped = map(int, begin[2:])
    if not 0 <= count <= 1000 or min(errors, missed) < 0 or stopped not in (0, 1):
        raise ValueError('Invalid capture counters')
    lines = [link.line(deadline) for _ in range(count+1)]
    if link.line(deadline) != f'END,{trial}':
        raise ValueError('Missing end marker or wrong trial ID')
    rows = parse_rows(lines, count, durations)
    span = (rows[-1]['pico_us']-rows[0]['pico_us'])/1e6
    hz = (len(rows)-1)/span if span else 0
    gaps = rows[0]['seq'] + sum(b['seq']-a['seq']-1 for a, b in zip(rows, rows[1:]))
    expected = sum(durations[:3])/10
    rails = sum(r['near_rail'] for r in rows)
    quality = dict(samples=len(rows), measured_hz=hz, sequence_gaps=gaps,
                   i2c_errors=errors, missed_slots=missed, stopped=bool(stopped), near_rail=rails)
    quality['warning'] = bool(errors or missed or gaps or stopped or rails or
                              abs(len(rows)-expected) > 1 or not 95 <= hz <= 105)
    print(f'{len(rows)} samples; {hz:.2f} Hz; sequence gaps={gaps}; I2C={errors}; missed={missed}; near rail={rails}')
    return rows, quality


def run_session(session, port):
    import serial
    print(f'Accepted {session.sport} throws will be saved to: {session.csv.resolve()}')
    print('Use THIS terminal for Enter/repeat/pause prompts. Keep the Serial Monitor closed.')
    total = len(session.meta['order'])
    if session.sport == 'cup_pong_aim':
        print('Aim experiment: normal strength and similar arc for every toss.')
        print('Start pointing at the CENTER target each time; aim toward the prompted target during the toss.')
        print('Keep grip, sensor mounting, and target positions consistent. These labels record intended aim, not measured release.')
    while True:
        try:
            session.rebuild_csv()
            if len(session.accepted) == total:
                print(f'Complete: {total} accepted throws in {session.csv}')
                return
            with serial.Serial(port, 115200, timeout=0.1, write_timeout=2) as device:
                # Stop an orphaned recording after host interruption; discard its export.
                device.write(b'STOP\n')
                time.sleep(0.3)
                device.reset_input_buffer()
                link = Link(device)
                link.send('INFO')
                info = link.response('INFO,')
                if len(info) != 8 or info[1:7] != ['2', 'MPU6050', '100', '8', '2000', '1']:
                    raise ValueError(f'Unexpected firmware or sensor error: {info}')
                if session.sport == 'cup_pong_aim':
                    link.send('CAPS')
                    caps = link.response('CAPS,')
                    if 'cup_pong_aim' not in caps[1:]:
                        raise ValueError('Flash the updated aiming firmware before collecting this session')
                while True:
                    if input('Rest IMU flat on a table, +Z upward. Enter, then leave untouched for 3 seconds (p to pause): ').strip().lower() == 'p':
                        return
                    link.send('CAL')
                    try:
                        cal = link.response('CAL,', 6)
                        bias = list(map(float, cal[1:]))
                        if len(bias) != 6 or not all(map(math.isfinite, bias)):
                            raise ValueError('Malformed calibration')
                        break
                    except ValueError as exc:
                        print(f'Calibration failed: {exc}. Reposition and try again.')
                calibration_id = session.calibration(bias)
                print('Calibration passed. Raw and corrected values will both be saved.')
                for index, plan in enumerate(session.meta['order']):
                    if index in session.accepted:
                        continue
                    while True:
                        print(f'\n{session.sport}: trial {index+1}/{total} | {plan["category"]} | repetition {plan["repetition"]}/10')
                        if input('Enter to start; p to pause and exit: ').strip().lower() == 'p':
                            return
                        trial = session.attempt()
                        rows, quality = acquire(link, trial, session.sport, plan['category'], session.meta['durations_ms'])
                        if quality['warning']:
                            print('QUALITY WARNING: inspect counters; repeat recommended. Explicit "accept" required to keep.')
                        choice = input('Enter to accept, r to repeat, p to pause without accepting: ').strip().lower()
                        if choice == 'p':
                            return
                        if choice == 'r' or (quality['warning'] and choice != 'accept'):
                            print('Trial not counted; repeating the same category/repetition.')
                            continue
                        if choice not in ('', 'accept'):
                            print('Unrecognized choice: trial not accepted; repeat.')
                            continue
                        session.accept(index, trial, rows, calibration_id, bias, quality)
                        print(f'Saved: {session.csv} ({len(session.accepted)}/{total} accepted)')
                        break
        except (serial.SerialException, OSError, TimeoutError, ValueError, UnicodeError) as exc:
            print(f'Collection interrupted: {exc}. Accepted trials remain saved.')
            if input('Reconnect Pico, then Enter to retry; p to pause: ').strip().lower() == 'p':
                return


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    parser.add_argument('--sport', choices=SPORTS)
    parser.add_argument('--data', type=Path, default=Path(__file__).resolve().parents[1]/'data')
    parser.add_argument('--seed', type=int, default=4760)
    parser.add_argument('--pre', type=float, default=0.5)
    parser.add_argument('--motion', type=float, default=2)
    parser.add_argument('--post', type=float, default=0.5)
    parser.add_argument('--countdown', type=float, default=3)
    parser.add_argument('--orientation', help='Sensor axes relative to grip; recorded on session creation')
    args = parser.parse_args()
    from serial.tools import list_ports
    if not args.port:
        for p in list_ports.comports():
            print(f'{p.device}: {p.description}')
        args.port = input('Pico COM port: ').strip()
    if not args.sport:
        print('1: bowling  2: basketball  3: cup pong  4: cup pong aiming')
        selection = input('Sport: ').strip()
        if selection not in ('1', '2', '3', '4'):
            parser.error('Choose 1, 2, 3, or 4')
        args.sport = list(SPORTS)[int(selection)-1]
    seconds = [args.pre, args.motion, args.post, args.countdown]
    if not all(math.isfinite(x) and x >= 0 for x in seconds):
        parser.error('Durations must be finite and nonnegative')
    durations = tuple(round(x*1000) for x in seconds)
    pre, motion, post, delay = durations
    if pre > 3000 or post > 3000 or not 100 <= motion <= 8000 or pre+motion+post > 10000 or delay > 10000:
        parser.error('pre/post <=3s, motion 0.1..8s, total <=10s, countdown <=10s')
    args.data.mkdir(parents=True, exist_ok=True)
    lock = args.data / f'{args.sport}.lock'
    try:
        handle = lock.open('x')
    except FileExistsError:
        parser.error(f'{lock} exists: close the other collector. If it crashed, remove this lock and retry.')
    try:
        with handle:
            handle.write(str(os.getpid()))
            handle.flush()
            if not (args.data / f'{args.sport}.session.json').exists() and not args.orientation:
                args.orientation = input('Describe sensor orientation relative to grip: ').strip() or 'not specified'
            session = Session(args.data, args.sport, args.seed, durations, args.orientation)
            run_session(session, args.port)
    finally:
        lock.unlink(missing_ok=True)


if __name__ == '__main__':
    try:
        main()
    except (KeyboardInterrupt, EOFError):
        print('\nPaused. Restart the same command to resume accepted trials.')
