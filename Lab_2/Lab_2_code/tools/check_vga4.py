"""Check assembled RGB palette instructions and the generated background.

Run after building: python tools/check_vga4.py
This models instruction ordering/timing with a supplied FIFO; it does not
validate physical sync timing, bus contention, or FIFO starvation on hardware.
"""
from pathlib import Path
import importlib.util
import random
import re
import struct

ROOT = Path(__file__).resolve().parents[1]


def instructions(name):
    text = (ROOT / 'build' / f'{name}.pio.h').read_text()
    body = text.split(f'{name}_program_instructions[] = {{', 1)[1].split('};', 1)[0]
    return [int(value, 16) for value in re.findall(r'^\s*(0x[0-9a-f]+),', body, re.M)]


def check_pio():
    code = instructions('rgb2')
    assert len(code) <= 32
    assert sum(len(instructions(n)) for n in ('hsync', 'vsync', 'line_sync')) <= 32
    rng = random.Random(4760)
    frame = bytes(rng.randrange(256) for _ in range(38400))
    fifo = iter([639, *struct.unpack('<9600I', frame)])
    expected = [((byte >> shift) & 1) for byte in frame for shift in range(8)]
    palette = [0, 12]
    pc, osr, shifted, x, y, cycles = 2, 0, 32, 0, 0, 0
    output = []
    last_pixel_cycle = None
    waits = 0
    while len(output) < 640 * 480:
        inst = code[pc]
        opcode, delay, dest = inst >> 13, (inst >> 8) & 31, (inst >> 5) & 7
        count = (inst & 31) or 32
        next_pc = 5 if pc == 13 else pc + 1
        if opcode == 4:  # PULL setup word
            osr, shifted = next(fifo), 0
        elif opcode == 5:  # MOV y, osr / MOV x, y
            if dest == 2:
                y = osr
            else:
                assert dest == 1
                x = y
        elif opcode == 3:  # OUT NULL or OUT PC with 32-bit autopull
            if shifted >= 32:
                osr, shifted = next(fifo), 0
            value = osr & ((1 << count) - 1)
            osr >>= count
            shifted += count
            if dest == 5:
                next_pc = value
            else:
                assert dest == 3
        elif opcode == 0:  # unconditional or X-- jump
            if dest == 0:
                next_pc = inst & 31
            else:
                assert dest == 2
                if x:
                    next_pc = inst & 31
                x = (x - 1) & 0xffffffff
        elif opcode == 1:  # WAIT GPIO22; start a new scanline
            assert (inst & 31) == 22
            assert len(output) == waits * 640
            waits += 1
            last_pixel_cycle = None
        elif opcode == 7:  # SET PINS: blanking or a decoded pixel
            if pc != 5:
                assert (inst & 31) == palette[expected[len(output)]]
                if last_pixel_cycle is not None:
                    assert cycles - last_pixel_cycle == 12
                last_pixel_cycle = cycles
                output.append(inst & 31)
        else:
            raise AssertionError(f'Unexpected opcode {opcode}')
        cycles += 1 + delay
        pc = next_pc
    assert waits == 480
    assert next(fifo, None) is None
    print('PASS: assembled PIO emits 307,200 correct colors, 640 pixels per row, '
          '12 cycles per pixel; both instruction memories fit.')


def check_background():
    spec = importlib.util.spec_from_file_location('bg', ROOT / 'tools/generate_galton_background.py')
    bg = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bg)
    source = (ROOT / 'galton_16_peg_opt.c').read_text()
    frame, labels = bg.generate(source, (ROOT / 'VGA/font_glcd.c').read_text())
    assert len(frame) == 38400
    get = lambda x, y: (frame[y * 80 + (x >> 3)] >> (x & 7)) & 1
    value = lambda name: int(re.search(r'^#define\s+' + name + r'\s+(\d+)', source, re.M)[1])
    for row in range(value('PEG_ROWS')):
        for col in range(row + 1):
            x = value('FIRST_PEG_X') - row * value('PEG_HORIZONTAL_SPACING') // 2
            x += col * value('PEG_HORIZONTAL_SPACING')
            y = value('FIRST_PEG_Y') + row * value('PEG_VERTICAL_SPACING')
            assert get(x, y) == 1
    for i, label in enumerate(labels):
        assert any(get(x, y) == 1 for x in range(10, 10 + 6 * len(label))
                   for y in range(20 + i * 10, 28 + i * 10))
    print('PASS: 38,400-byte background, peg palette indices and all four labels.')


if __name__ == '__main__':
    check_pio()
    check_background()
