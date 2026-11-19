"""Run hardware-free wireless/input and VGA sync checks with Python and a C compiler."""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
library = ROOT / "build_wireless/wireless_native.dll"
library.parent.mkdir(exist_ok=True)
subprocess.run([
    os.environ.get("CC", "cc"), "-shared", "-fPIC", "-O2",
    "-I" + str(ROOT / "tests/stubs"), "-I" + str(ROOT),
    *[str(ROOT / source) for source in (
        "bowling_model.c", "sports_views.c", "wireless/wire.c", "tests/wireless_native.c")],
    "-lm", "-o", str(library),
], check=True)
for test in ("test_wireless.py", "test_vga_sync.py"):
    subprocess.run([sys.executable, str(ROOT / "tests" / test)], check=True)
