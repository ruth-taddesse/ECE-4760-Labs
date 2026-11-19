"""Exercise the driver's actual frame-signal function without Pico hardware."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class VgaSyncTests(unittest.TestCase):
    def test_frame_signals_for_all_buffer_modes(self):
        driver = (Path(__file__).resolve().parents[1] /
                  "VGA/vga16_graphics_v3.c").read_text()
        start = driver.index("int draw_start_signal(void){")
        end = driver.index("\n}", start) + 2
        source = "#include <assert.h>\nvolatile int start_flag;\n"
        source += driver[start:end]
        source += """
int main(void) {
    assert(draw_start_signal() == 0);
    for (int mode = 1; mode <= 3; ++mode) {
        for (int frame = 0; frame < 3; ++frame) {
            start_flag = mode;
            assert(draw_start_signal() == 1);
            assert(start_flag == 0);
            assert(draw_start_signal() == 0);
        }
    }
}
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "sync.c").write_text(source)
            subprocess.run(["cc", "-O2", str(path / "sync.c"),
                            "-o", str(path / "sync")], check=True)
            subprocess.run([str(path / "sync")], check=True)


if __name__ == "__main__":
    unittest.main()
