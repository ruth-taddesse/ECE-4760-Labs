"""Build native bowling adapter, test navigation/physics and render the VGA UI.

Run python3 tests/preview_bowling.py; requires cc and Pillow, no training CSV.
"""
import ctypes as C
import os
from pathlib import Path
import subprocess
import sys
import unittest

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/bowling_ui'
WIDE = OUT / 'monitor_16x9'
SCENES = [
    ('01_game_menu', 0, 1, 0, 0),
    ('02_calibrate', 1, 0, 0, 0),
    ('03_ready', 1, 1, 0, 0),
    ('04_countdown', 1, 3, 0, 0),
    ('05_throw_now', 1, 4, 1, 0),
    ('06_ball_start', 1, 5, 0, 0),
    ('07_ball_rolling', 1, 5, 0, .7),
    ('08_result', 1, 5, 0, 20),
    ('09_settings', 2, 1, 0, 0),
    ('10_basketball_pending', 3, 1, 1, 0),
    ('11_cup_pong_pending', 3, 1, 2, 0),
    ('12_quit', 4, 1, 0, 0),
    ('13_sensor_error', 1, 7, 0, 0),
    ('14_bad_capture', 1, 6, 0, 0),
    ('15_calibrating', 1, 2, 0, 0),
]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    WIDE.mkdir(exist_ok=True)
    # Keep the existing test adapter's expected path; ctypes loads native binaries
    # by content, so the .dll suffix also works on macOS/Linux.
    library = ROOT / 'build/bowling_native.dll'
    subprocess.run([os.environ.get('CC', 'cc'), '-shared', '-fPIC', '-O2',
                    '-I' + str(ROOT / 'tests/stubs'), '-I' + str(ROOT),
                    str(ROOT / 'bowling_model.c'), str(ROOT / 'tests/bowling_native.c'),
                    '-lm', '-o', str(library)], check=True)
    lib = C.CDLL(str(library))
    result = lib.test_navigation()
    if result:
        raise AssertionError(f'Navigation check failed: {result}')
    # Existing physics checks run against the same unmodified C model.
    sys.path.insert(0, str(ROOT / 'tests'))
    from test_bowling_native import BowlingTests
    suite = unittest.TestSuite(BowlingTests(name) for name in (
        'test_joystick_position_and_capture_lock',
        'test_straight_and_first_pin', 'test_hook_sign_and_symmetry',
        'test_speed_gain_and_gutter', 'test_randomness_and_tuning_bounds',
        'test_stationary_rejected'))
    if not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful():
        raise AssertionError('Bowling physics regression')
    lib.test_ui.argtypes = [C.c_int, C.c_int, C.c_uint, C.c_float]
    lib.test_ui.restype = C.POINTER(C.c_ubyte)
    palette = [channel for i in range(16) for channel in
               (255 if i & 8 else 0, (i & 3) * 85, 255 if i & 4 else 0)]
    sheet = Image.new('RGB', (1920, 2520), '#151c22')
    draw = ImageDraw.Draw(sheet)
    for index, (name, view, state, selection, seconds) in enumerate(SCENES):
        pixels = lib.test_ui(view, state, selection, seconds)
        if lib.test_bounds_errors():
            raise AssertionError(f'Out-of-bounds drawing: {name}')
        frame = Image.frombytes('P', (640, 480), C.string_at(pixels, 640 * 480))
        frame.putpalette(palette)
        frame = frame.convert('RGB')
        frame.save(OUT / (name + '.png'))
        # The Dell monitor's Fill/16:9 mode stretches the 640x480 signal to
        # square-looking 16:9 output. Nearest-neighbor preserves VGA pixels.
        frame.resize((854, 480), Image.Resampling.NEAREST).save(WIDE / (name + '.png'))
        x, y = index % 3 * 640, index // 3 * 504
        draw.text((x + 12, y + 6), name.replace('_', ' '), fill='white')
        sheet.paste(frame, (x, y + 24))
    sheet.save(OUT / 'all_states.png')
    wide_sheet = sheet.resize((2562, 2520), Image.Resampling.NEAREST)
    wide_sheet.save(WIDE / 'all_states.png')
    print(f'Navigation passed; rendered {len(SCENES)} states within 640x480.')
    print(OUT / 'all_states.png')
    print(WIDE / 'all_states.png')


if __name__ == '__main__':
    main()
