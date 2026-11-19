"""Render the actual collection VGA UI with synthetic sensor signals.

Run: python3 tests/preview_collection.py
Requires a native C compiler and Pillow. Outputs: build/vga_preview/.
"""
import ctypes
import os
from pathlib import Path
import subprocess
import sys

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "vga_preview"
# VGA green has two bits; red and blue each have one.
PALETTE = [(255 if i & 8 else 0, (i & 3) * 85, 255 if i & 4 else 0)
           for i in range(16)]
SCENES = [
    ("01_calibration_needed", 0, 0, 1, 0),
    ("02_calibrating", 5, 0, 1, 0),
    ("03_ready", 0, 0, 3, 0),
    ("04_countdown", 1, 0, 3, 0),
    ("05_pre_hold", 2, 0, 3, 0),
    ("06_throw_now", 2, 1, 3, 3),
    ("07_post_hold", 2, 2, 3, 0),
    ("08_complete_review_in_host", 3, 2, 3, 0),
    ("09_quality_warning", 3, 2, 19, 0),
    ("10_stopped", 3, 0, 11, 0),
    ("11_sensor_error", 4, 0, 1, 0),
    ("12_calibration_failed", 0, 0, 5, 0),
    ("13_usb_offline", 0, 0, 0, 0),
]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    library = OUT / ("collection_native.dylib" if sys.platform == "darwin" else "collection_native.so")
    subprocess.run([os.environ.get("CC", "cc"), "-shared", "-fPIC", "-O2",
                    "-I" + str(ROOT / "tests/stubs"), "-I" + str(ROOT),
                    str(ROOT / "tests/collection_native.c"),
                    *(str(ROOT / name) for name in ("bowling.c", "basketball.c", "cup_pong.c")),
                    "-lm", "-o", str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    lib.collection_render.argtypes = [ctypes.c_int, ctypes.c_uint, ctypes.c_int, ctypes.c_int]
    lib.collection_render.restype = ctypes.POINTER(ctypes.c_ubyte)
    lib.collection_bounds_errors.restype = ctypes.c_uint
    if lib.collection_check_commands() != 0:
        raise AssertionError("Collection command/state check failed")
    sheet = Image.new("RGB", (3 * 640, 5 * 504), "#151c22")
    draw = ImageDraw.Draw(sheet)
    for index, (name, state, phase, flags, sport) in enumerate(SCENES):
        data = lib.collection_render(state, phase, flags, sport)
        if lib.collection_bounds_errors():
            raise AssertionError(f"Drawing outside 640x480: {name}")
        frame = Image.frombytes("P", (640, 480), ctypes.string_at(data, 640 * 480))
        frame.putpalette([channel for rgb in PALETTE for channel in rgb])
        frame = frame.convert("RGB")
        frame.save(OUT / (name + ".png"))
        x, y = (index % 3) * 640, (index // 3) * 504
        draw.text((x + 12, y + 6), name.replace("_", " "), fill="white")
        sheet.paste(frame, (x, y + 24))
    sheet.save(OUT / "all_states.png")
    print(f"Rendered {len(SCENES)} states within 640x480; CAL/START/STOP checks passed.")
    print(OUT / "all_states.png")


if __name__ == "__main__":
    main()
