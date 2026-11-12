# Pico Sports: IMU collection guide

Confirmed hardware: **Pico 2**, **MPU6050**, I2C0 **GPIO 8 SDA / GPIO 9 SCL**,
address **0x68**. Keep +Z facing upward during calibration. No Bluetooth,
buttons, joystick, game physics, or gesture classification is needed.

For the separate top-down bowling game prototype, see [BOWLING_DEMO.md](BOWLING_DEMO.md). Flash `BOWLING_DEMO.uf2` for playing or `MPU6050_IMU.uf2` for collection.

## Files and architecture

- `imu_training_data.c`: entry point, newline command protocol, timed capture,
  RAM buffer, VGA plots and phase cues. Core 0 samples; core 1 draws snapshots.
- `imu.c` / `imu.h`: checked initialization, coherent 14-byte burst reads,
  SI scaling, stationary calibration. Uses the existing `mpu6050.c` gyro-range
  function and pin definitions in `mpu6050.h`; the legacy fixed-point API remains.
- `bowling.c`, `basketball.c`, `cup_pong.c`, `sports.h`: sport/category definitions;
  sampling is shared. The existing project layout and entry point are retained.
- `host/collect.py`: sport menu, randomized schedule, calibration, prompts,
  serial validation, trial acceptance, durable storage and resume.
- `host/analyze.py`: plots, per-trial features and category statistics.
- `collect_throws.py`: compatibility entry point for the new collector.

The new firmware uses protocol v2 and does **not** work with the old single-letter
`r`/`d` logger. Flash the new UF2 and use the new host script together.

## Build and install

From this project directory in PowerShell:

```powershell
$cmakeSports = "$env:USERPROFILE/.pico-sdk/cmake/v4.3.4/bin/cmake.exe"
& $cmakeSports -S . -B build -G Ninja "-DCMAKE_MAKE_PROGRAM=$env:USERPROFILE/.pico-sdk/ninja/v1.13.2/ninja.exe" "-DPICO_TOOLCHAIN_PATH=$env:USERPROFILE/.pico-sdk/toolchain/15_2_Rel1"
& $cmakeSports --build build
```

Flash **`build/MPU6050_IMU.uf2`** to the Pico 2 in BOOTSEL mode.
The VS Code Compile Project task uses `build/`, which has been regenerated for this project.
CMake deliberately keeps `PICO_BOARD=pico2` until the Pico 2 W arrives.

VGA uses GPIO 16 through 21 and the resistors documented in `imu_demo.c` and
`VGA/vga16_graphics_v3.h`. IMU pin/address configuration is in `mpu6050.h`.
Use 3.3 V and common ground; use a valid I2C0 pin pair if changing pins.

Set up Python (the `py` launcher did not find Python in this environment):

```powershell
$pythonSports = "$env:LOCALAPPDATA/Programs/Python/Python313/python.exe"
& $pythonSports -m venv --system-site-packages .venv
& ./.venv/Scripts/python.exe -m pip install -r host/requirements.txt
```

Close other serial monitors before starting the collector.

The firmware prints a welcome/status message when the USB serial connection opens
(and again after reconnecting). Send `HELP` with a newline to repeat it, or `INFO`
to check sensor status. Press Enter at the prompts in the **Python terminal** for
calibration, starting a throw and accepting its data. The Serial Monitor by itself
does not save CSV files. Python prints the full destination CSV path at startup.

## Stage 1: sensor verification and calibration

Implementation: `imu.c`, firmware `INFO` and `CAL` commands.

```powershell
& ./.venv/Scripts/python.exe host/collect.py --port COM5 --sport bowling
```

Replace COM5 with the Pico's port. Omit `--port` to list available ports and enter
one. Omit `--sport` for the three-sport menu. For a new session, describe how the
sensor axes face relative to the grip; this goes into session metadata.

The host checks protocol version, sensor identity, sample rate/ranges and sensor
health. Lay the sensor level, +Z upward. Press Enter to run a two-second,
200-sample calibration. It checks variance, orientation and plausible gyro bias;
movement or wrong orientation prompts a retry. Keep it still until calibration
passes, then pick up the controller and assume your consistent throwing grip.

**Hardware check:** at rest raw acceleration magnitude should be near 9.81 m/s²
and gyro magnitude near zero. After calibration corrected X/Y acceleration
should be near zero and corrected Z near +9.81 m/s² in the calibration pose.
This is a single-pose offset estimate; tilt and accelerometer offset cannot be
fully separated. It does not calibrate scale or axis alignment. Raw readings
remain available so calibration can be revised later.

## Stage 2: 100 Hz capture and VGA

Implementation: the common IMU module and `imu_training_data.c` capture loop.
Target rate is 100 Hz with +/-8 g and +/-2000 deg/s sensor ranges and DLPF=3.
A monotonic 64-bit Pico timestamp is taken immediately before each I2C read.
Sequence numbers advance over failed reads and missed software sampling slots.
This is host-timed polling of the sensor; timestamps are not internal sensor
sample timestamps and exact data-ready synchronization is not claimed.

Press Enter at the trial prompt. Default timing is:

1. Three-second countdown: get ready.
2. 0.5 seconds PRE: hold still in your starting pose.
3. Two seconds MOTION: perform one throw.
4. 0.5 seconds POST: hold still after the motion.

Follow the VGA cues. Plots show all six **uncorrected SI** channels plus both
magnitudes; colored boundaries mark phase changes. It also shows sport/category,
trial ID, sample count, peaks, near-rail samples, read errors and missed slots.
The completed plot freezes. Countdown and phase boundaries are experiment cues,
**not measured release events**. No release velocity or launch angle is inferred.

**Hardware check:** perform a stationary trial first. Default capture should
have about 300 samples and measured rate near 100 Hz, with zero I2C errors,
sequence gaps and near-rail samples. Unexpected gaps/range warnings are displayed
and require typing `accept` explicitly to retain that trial.

USB export occurs after the recording ends, avoiding printf in the sample loop.
Samples are held in RAM; at most 1000 samples / ten seconds per trial.
Calibration, export, and idle command output pause acquisition.

## Stage 3: CSV logging

Implementation: `host/collect.py` protocol parser, `Session.accept`, and journal.
Accepted trials go into:

```text
data/
  bowling.csv
  bowling.session.json
  bowling_accepted/01.json ... 50.json
  basketball.csv
  basketball.session.json
  basketball_accepted/...
  cup_pong.csv
  cup_pong.session.json
  cup_pong_accepted/...
```

Each CSV row contains session ID, sport, unique attempt/trial ID, trial number,
category, repetition, calibration ID, sample sequence, absolute Pico timestamp,
relative elapsed time, recording phase, six uncorrected SI channels, near-rail
flag, and six corrected SI channels. Acceleration is m/s²; gyro is rad/s.
Acceleration retains gravity even after offset correction. SI readings are
scaled sensor output, not original integer ADC counts. Absolute Pico time resets
on reboot; use session/trial IDs and elapsed time for comparisons.

One accepted JSON record is committed before atomically rebuilding the sport CSV.
If interrupted between those steps, restart reconstructs the CSV from the records.
Keep the CSV, session metadata and accepted journal together. Do not manually
edit them during collection. These are PC files; the Pico does not write flash.
Rejecting a trial discards that attempt's samples and does not advance the quota.

**Check:** accept the stationary trial in a separate pilot directory, then open
its CSV. Verify phase labels, ~10,000 microsecond intervals, units and calibration.
Use `--data data/pilot` for pilot captures so they do not count toward the actual
sports experiment. Never accept a stationary pilot as a real labeled throw.

## Stage 4: randomized trials, pause and resume

Each sport has exactly five categories with ten repetitions each (50 accepted
throws; 150 across all three sports). The seeded order is saved once. Firmware
and host use matching category definitions.

- Enter before each capture starts the countdown.
- After capture: Enter accepts a clean trial; `r` repeats; `p` pauses without
  accepting the current trial. Quality warnings require the word `accept`.
- `p` before capture exits cleanly. Ctrl+C also exits; previously accepted trials
  survive. An interrupted/unaccepted capture must be repeated.
- Restart the same command to resume. Reconnects run a new calibration and store
  its ID/biases. The saved order, timings and orientation take precedence over
  new command-line values. A disconnect/malformed packet never advances a trial.
- Existing sessions resume; the tool never resets them. For another experiment,
  select a new `--data` directory. Unknown pre-existing CSV data is refused.
- A per-sport `.lock` prevents two collectors writing together. After a hard
  process crash, remove that lock only after confirming no collector is running.

Example new session with explicit settings:

```powershell
& ./.venv/Scripts/python.exe host/collect.py --port COM5 --sport cup_pong --seed 123 --pre 0.5 --motion 2 --post 0.5 --countdown 3 --orientation "+X forward, +Y left, +Z up at rest"
```

Durations are in seconds: PRE/POST at most 3 each, MOTION 0.1..8, total at most
10, countdown at most 10. Categories and repetitions are saved before collection.

**Check:** reject one trial and confirm the same category/repetition repeats.
Accept two, pause, restart, and confirm those remain saved and the next planned
trial is offered. All 50 accepted records must contain ten of each category.

## Stage 5: analysis

```powershell
& ./.venv/Scripts/python.exe host/analyze.py data/bowling.csv --trial 1 --category straight_normal
```

`--trial` accepts the displayed trial number or full ID. The analysis writes a
new timestamped subdirectory under `analysis/` containing:

- `selected_trial.png`: six channels, acceleration/gyro magnitudes and phase shading.
- `category_comparison.png`: repeated throws aligned to the prompted motion phase.
- `trial_features.csv`: peaks, RMS acceleration, measured rate, rail counts and
  a threshold-based motion-duration estimate.
- `category_stats.csv`: means and sample standard deviations; std is blank when
  only one throw exists in a category.
- `notes.txt`: units/threshold caveats and input path.

Analysis defaults to corrected channels; `--raw` selects uncorrected ones.
Features use only MOTION samples. Acceleration peak/RMS retain gravity. Estimated
motion duration spans first to last sample exceeding `abs(|a|-g)>2 m/s²` OR
`|gyro|>0.5 rad/s`; use `--accel-threshold` and `--gyro-threshold` to change these.
It is a heuristic, not release timing or a physical velocity estimate.

**Check:** stationary pilot data should have acceleration magnitude/RMS near g,
small gyro values and zero or small threshold-based motion duration. Compare a
few weak/strong throws before committing to the complete dataset.

## Offline tests and protocol

```powershell
& ./.venv/Scripts/python.exe -m unittest discover -s host -p 'test_*.py' -v
```

Tests cover balanced/reproducible schedules, serial fragmentation and bursts,
corruption, missing samples, interrupted-save recovery, duplicate acceptance,
existing-data protection and stationary feature calculations. These checks do
not verify actual IMU/VGA behavior; perform the hardware checks above.

Protocol commands are ASCII lines ending in newline:

```text
INFO
CAL
START <unique_id> <sport_index> <category_index> <pre_ms> <motion_ms> <post_ms> <countdown_ms>
STOP
DUMP
```

Sport indexes: bowling=0, basketball=1, cup_pong=2. Category indexes follow the
order in `sports.h` definitions / `host/collect.py`. `STOP` ends a capture and
marks it incomplete; `DUMP` re-exports the last completed RAM buffer. A new START
replaces it. While recording, commands other than STOP are ignored to protect
timing. Host validates acknowledgements, trial IDs, row counts, timestamps,
sequence/phase fields and end markers. No hardware release button is assumed.



## Cup-pong left/center/right aiming experiment

Flash the updated `build/MPU6050_IMU.uf2`, close Serial Monitor, then run:

```powershell
& ./.venv/Scripts/python.exe host/collect.py --port COM4 --sport cup_pong_aim
```

This is a separate 30-trial session: ten each of `aim_left`, `aim_center`, and
`aim_right`, in randomized order. VGA displays the requested direction. Accepted
samples save to `data/cup_pong_aim.csv`, with their own session metadata and
accepted-record directory. Existing `cup_pong.csv` is untouched. The collector
checks firmware support before starting this experiment.

Place three target markers left/center/right at the same distance. Use normal
strength and a similar arc for all trials. Start with the controller pointing
at CENTER each time; then aim toward the requested marker during the toss.
Keep grip and mounting consistent, and record marker placement and sensor axes
in the orientation/session notes. Perform the toss at the VGA THROW NOW cue.
Enter accepts a trial, r repeats it, and p pauses. Resume with the same command.
The labels describe intended aim; there is still no measured release instant.
