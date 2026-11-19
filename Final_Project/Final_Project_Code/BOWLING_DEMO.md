# Pico Sports menu and player-view bowling prototype

This separate firmware starts at a game-selection menu and draws bowling from
the player's position at the foul line. The lane converges into the distance;
the ten pins and animated ball use the same perspective projection. A persistent
yellow trajectory shows the throw, and the first contacted pin turns red. It
stops at first contact or a gutter; pin scattering and scoring are not implemented.

The collection dashboard in `imu_training_data.c` is a separate firmware target.
The original bowling capture, classifier, tuning, replay, and trajectory physics
remain; the bowling renderer and navigation are updated here.

The interface is designed for the lab monitor's **16:9 / Fill** display mode.
The electrical VGA signal remains 640x480. Game circles and pin silhouettes are
drawn horizontally compressed so the monitor's 4:3-to-16:9 stretch restores
their intended proportions. Raw framebuffer previews stay 640x480; simulated
monitor previews are written under `build/bowling_ui/monitor_16x9/`.

## Run

**VS Code USB shortcut:** Close Serial Monitor, then choose **Terminal > Run Task > Run Bowling Demo (USB)**. This task builds and explicitly loads `BOWLING_DEMO.uf2`. The generic **Run Project** task uses the extension's selected target and may still load the collection firmware. If USB loading fails, use BOOTSEL and copy the UF2 as described below. The bowling screen says **PICO SPORTS / BOWLING PATH DEMO**, with a lane and ten white pins.

1. Use **Compile Project** in VS Code. Both collection and bowling targets build.
   **Run Project** now loads `build/BOWLING_DEMO.uf2`, which starts at the game
   menu. **Run Collection Firmware** explicitly loads the collection target.
   **Open Game Menu Terminal** runs `host/play_bowling.py` for keyboard controls
   (requires Python with the packages from `host/requirements.txt`).
2. Hold BOOTSEL while connecting the Pico 2, then copy **build/BOWLING_DEMO.uf2** to its USB drive. The collection firmware is **MPU6050_IMU.uf2**, a different target.
3. Keep the existing MPU6050 and VGA wiring. Close VS Code's Serial Monitor so the Python terminal can open the port.
4. From this project folder, run:

```powershell
& ./.venv/Scripts/python.exe host/play_bowling.py --port COM4
```

5. At the VGA menu, type `1` in the host terminal to choose Bowling. Type `CAL`,
   with the IMU flat and +Z upward, and leave it untouched for three seconds.
   Then pick it up using the same grip/orientation used for the training throws.
6. Press Enter to start. Watch the VGA: **GET READY** counts down three seconds, **HOLD STILL** lasts 0.5 seconds, **THROW NOW** lasts two seconds, then hold still for another 0.5 seconds. Make the throwing gesture during **THROW NOW**. The trajectory animates after capture finishes.
7. Press Enter for another throw, or enter a tuning command. `MENU` returns to
   game selection, `SETTINGS` opens tuning, and `QUIT` (or `q`) stops the session
   and closes the host terminal.

If the COM number changes, run without `--port` to list ports. You can instead use a serial monitor at 115200 baud: send `CAL`, then blank Enter or `THROW`. Commands need a newline and are case-insensitive, so `cal`, `Throw`, and `MENU` are equivalent. `SET` parameter names are also case-insensitive. `HELP` prints instructions; `INFO` returns `BOWL,1,<sensor healthy>,<calibrated>`.

## Menu controls

Joystick navigation follows the tile layout: left/right across tiles, up/down between rows. Press SW to select. Hold a direction to repeat after 400 ms, then every 180 ms. SW has 30 ms debounce and triggers only once per press. In Settings SW returns; on Coming Soon or Quit it returns to the main menu. USB commands still work. SW does not start throws; continue using Enter in Bowling.

- `1`: Bowling, the playable trajectory prototype.
- `2` / `3`: Basketball / Cup pong. These open an explicit **coming soon** page;
  only their gesture-collection categories exist. Enter returns to the menu.
- `4` or `SETTINGS`: adjustable starting position, aim, speed, curve, friction,
  hook onset and randomness. `SET name value` changes a value; `RESET` restores
  defaults. `BACK` returns to the screen that opened settings. `CAL` opens bowling
  calibration. Calibration and settings are retained in RAM when returning to menu.
- `5` or `QUIT`: stop the session and show the quit screen. The host terminal
  closes for this explicit command. Reopen it to play again; a serial monitor
  can also send Enter or `MENU` from the quit screen.
- Type `W` then Enter to move up, or `S` then Enter to move down. Press Enter
  to select the highlighted game-menu option.
- `MENU`: return to game selection from any screen, cancelling active capture.

The joystick VRx connection on GPIO 27 / ADC1 moves the ball's starting position
left or right before a throw. Releasing it holds the position; starting a throw
locks the position through capture and replay. Power the joystick from 3.3 V.

`MENU`, `BACK`, `SETTINGS`, and `QUIT` cancel an active countdown/capture when
sent directly over serial. The Python terminal waits for the current throw to
finish before its next input prompt. `STOP` also cancels capture. Settings and
navigation do not add game scoring or implement the other two sports.

The game runs on the Pico. Python only supplies keyboard commands and terminal feedback. This demo does **not** append to or modify any training CSV. Settings and the last throw live in RAM and reset at reboot.

## Tune the path

### Joystick starting position

Wire joystick **Vy to GPIO26 / ADC0 / physical pin 31** (horizontal), **Vx to GPIO27 / ADC1 / physical pin 32** (vertical), and **SW to GPIO28 / physical pin 34** (active-low switch to GND with an internal pull-up), VCC to **3.3 V**, and GND to Pico ground. After IMU calibration, pushing left/right moves the cyan ball along the foul line; releasing the stick holds that position. Greater deflection moves faster. Pressing Enter freezes the position through countdown, capture and animation. After animation finishes, moving the joystick clears the old path and positions the next throw. No joystick button is required.

The input is averaged at 100 Hz with a 256-count center dead zone. Starting position stays within ±1.3 lane units. Adjust `JOY_CENTER`, `JOY_DEADZONE`, `JOY_SPEED`, or `JOY_DIRECTION` (horizontal) or `JOY_Y_DIRECTION` (vertical) near the top of `bowling_demo.c` if needed; set direction to `-1.0f` if left/right are reversed. Rebuild and flash after changing these constants. Keep the joystick connected when using this firmware, since a disconnected ADC input can float.

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

The simulation uses speed `3 + 3*power`, friction, and lateral acceleration `1.8*hook*hook_gain` with a smooth ramp down the lane. Slower balls have more time to curve. It advances at 120 Hz and uses swept ball/pin collision checks. The lane is deliberately schematic. Its player-view projection changes display coordinates and apparent sizes only; the simulation remains in the original lane coordinates.

These are **game units**, not measured ball speed, spin or a fitted physical bowling model: the recordings contain IMU gestures but no tracked real ball outcomes. Training determines gesture class, relative power and hook; the mapping to an enjoyable path is tunable. Default randomness is zero to make initial testing predictable. Weak, stationary, saturated or incomplete captures are rejected.

## Files and verification

- `bowling_demo.c`: capture timing, USB commands and VGA rendering.
- `bowling_model.c/.h`: feature extraction, classification and trajectory math.
- `sports_views.c/.h`: preliminary basketball and cup-pong VGA views.
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
& ./.venv/Scripts/python.exe -m ziglang cc -shared -O2 -Itests/stubs -I. bowling_model.c sports_views.c tests/bowling_native.c -o build/bowling_native.dll '-Wl,--export-all-symbols'
& ./.venv/Scripts/python.exe tests/test_bowling_native.py
```

Tests also render the actual firmware drawing calls into `build/bowling_preview_*.png`. These checks do not replace a physical Pico/VGA timing and motion test.

For a desktop preview without the original training CSV, with Pillow and a
native C compiler installed (macOS/Linux):

```sh
python3 tests/preview_bowling.py
```

This tests menu/calibration/capture cancellation/settings navigation, runs five
existing C physics/classifier checks, and renders 15 screens into
`build/bowling_ui/`, including `all_states.png`. The sample throw is synthetic;
this does not rerun dataset validation or verify physical VGA timing.
