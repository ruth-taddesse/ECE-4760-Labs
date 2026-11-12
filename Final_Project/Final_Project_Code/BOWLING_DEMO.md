# Bowling path prototype

This separate firmware draws a top-down lane, ten pins, an animated ball and a persistent yellow trajectory. The first contacted pin turns red. It stops at first contact or a gutter; pin scattering and scoring are not implemented.

## Run

1. Use **Compile Project** in VS Code. Both collection and bowling targets build.
2. Hold BOOTSEL while connecting the Pico 2, then copy **build/BOWLING_DEMO.uf2** to its USB drive. The collection firmware is **MPU6050_IMU.uf2**, a different target.
3. Keep the existing MPU6050 and VGA wiring. Close VS Code's Serial Monitor so the Python terminal can open the port.
4. From this project folder, run:

```powershell
& ./.venv/Scripts/python.exe host/play_bowling.py --port COM4
```

5. Place the IMU flat with +Z upward. Press Enter to calibrate, leaving it untouched for three seconds. Then pick it up using the same grip/orientation used for the training throws.
6. Press Enter to start. Watch the VGA: **GET READY** counts down three seconds, **HOLD STILL** lasts 0.5 seconds, **THROW NOW** lasts two seconds, then hold still for another 0.5 seconds. Make the throwing gesture during **THROW NOW**. The trajectory animates after capture finishes.
7. Press Enter for another throw, or enter a tuning command. Type `q` to close the host terminal.

If the COM number changes, run without `--port` to list ports. You can instead use a serial monitor at 115200 baud: send `CAL`, then blank Enter or `THROW`. Commands need a newline. `HELP` prints instructions; `INFO` returns `BOWL,1,<sensor healthy>,<calibrated>`.

The game runs on the Pico. Python only supplies keyboard commands and terminal feedback. This demo does **not** append to or modify any training CSV. Settings and the last throw live in RAM and reset at reboot.

## Tune the path

Enter commands such as `SET hook_gain 1.5`. Each successful change automatically replays the last throw, allowing comparisons without another gesture. `REPLAY` repeats it; `RESET` restores defaults. `STOP` cancels a capture when sent directly over serial.

| Parameter | Default | Range | Effect |
| --- | ---: | --- | --- |
| `start_x` | 0 | -1.3 to 1.3 | Starting position; negative is left |
| `aim_deg` | 0 | -10 to 10 | Initial heading; positive is right |
| `speed_gain` | 1 | 0.5 to 2 | Ball speed multiplier |
| `hook_gain` | 1 | 0 to 3 | Curve strength; zero makes a straight path |
| `friction` | 0.12 | 0 to 1 | Speed loss in game units/s² |
| `hook_start` | 0.10 | 0 to 0.9 | Fraction of lane before curve begins ramping up |
| `randomness` | 0 | 0 to 1 | At 1, up to ±0.5° heading and ±2% speed variation |

Examples: `SET start_x -0.5`, `SET aim_deg 2`, `SET hook_gain 1.5`, `SET randomness 0.2`. Replays preserve the random seed so tuning comparisons are repeatable. A new gesture gets a new seed.

## Training and model limits

`host/train_bowling.py` fits normalized nearest-centroid classification from the 49 accepted throws in `data/bowling.csv`. It uses the two-second motion phase: peak acceleration magnitude, gyro minima/maxima/integrals, and mean acceleration on each axis (13 features). Categories are straight slow/normal/fast and hook left/right. Leave-one-out classification was **44/49**; this small, single-session result is not validation for other people or grips.

Power varies continuously between the recorded slow/fast median acceleration peaks (about 14.17 and 34.00 m/s²). Relative class similarities produce a signed hook value. Starting position and aim are manual because this dataset does not provide measured launch heading. Similarity weights are not calibrated probabilities.

The simulation uses speed `3 + 3*power`, friction, and lateral acceleration `1.8*hook*hook_gain` with a smooth ramp down the lane. Slower balls have more time to curve. It advances at 120 Hz and uses swept ball/pin collision checks. The lane is deliberately schematic, with equal horizontal and vertical display scales.

These are **game units**, not measured ball speed, spin or a fitted physical bowling model: the recordings contain IMU gestures but no tracked real ball outcomes. Training determines gesture class, relative power and hook; the mapping to an enjoyable path is tunable. Default randomness is zero to make initial testing predictable. Weak, stationary, saturated or incomplete captures are rejected.

## Files and verification

- `bowling_demo.c`: capture timing, USB commands and VGA rendering.
- `bowling_model.c/.h`: feature extraction, classification and trajectory math.
- `bowling_trained.h`: generated training constants.
- `bowling_model_report.json`: dataset fingerprint, class counts and leave-one-out results.
- `host/play_bowling.py`: keyboard terminal.

Regenerate the model after deliberately changing training data:

```powershell
& ./.venv/Scripts/python.exe host/train_bowling.py
```

Then rebuild firmware. Offline tests exercise the actual C classifier and trajectory with all 49 recorded throws, feature parity, first contact, left/right symmetry, speed, gutters, randomness, parameter bounds and stationary rejection. With the development environment's `ziglang` and Pillow installed:

```powershell
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path (Get-Location) 'build/zig-cache'
& ./.venv/Scripts/python.exe -m ziglang cc -shared -O2 -Itests/stubs -I. bowling_model.c tests/bowling_native.c -o build/bowling_native.dll '-Wl,--export-all-symbols'
& ./.venv/Scripts/python.exe tests/test_bowling_native.py
```

Tests also render the actual firmware drawing calls into `build/bowling_preview_*.png`. These checks do not replace a physical Pico/VGA timing and motion test.
