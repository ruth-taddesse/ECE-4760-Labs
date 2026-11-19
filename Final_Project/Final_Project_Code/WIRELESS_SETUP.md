# Pico Sports: two Pico W devices over UDP

Use **original Pico W (RP2040)** boards for both roles. The wireless firmware is
built separately from the old Pico 2 single-board demo. No router, Internet,
Python relay, or laptop is required during play.

## What to upload

| Device | Hardware attached | UF2 to copy onto its BOOTSEL drive |
| --- | --- | --- |
| Ground station Pico W | VGA monitor | `build_wireless/SPORTS_GROUND.uf2` |
| Handheld controller Pico W | MPU6050 and joystick | `build_wireless/SPORTS_CONTROLLER.uf2` |

Do not upload `MPU6050_IMU.uf2` or `BOWLING_DEMO.uf2` to this pair. Those are the
older wired targets built for Pico 2. USB serial COM numbers are not Wi-Fi addresses.

## Wiring

All pin numbers below are **GPIO numbers**, except where explicitly marked physical.

Controller:

- MPU6050 SDA **GP8**, SCL **GP9**, address **0x68**, 3.3 V and GND.
- Joystick **Vy -> GP26 / ADC0 / physical pin 31**: horizontal.
- Joystick **Vx -> GP27 / ADC1 / physical pin 32**: vertical, inverted for your mounting.
- Joystick **SW -> GP28 / physical pin 34**: active-low switch to ground; firmware enables the pull-up.
- Joystick VCC **3.3 V**, common GND. ADC inputs must not receive 5 V.

Ground station: existing VGA **GP16 HSync**, **GP17 VSync**, **GP18/19 green**,
**GP20 blue**, **GP21 red**, with the same resistor network and VGA ground as before.
No IMU or joystick is needed on the ground station. Each Pico W needs its own power.

## Build and flash

In this project, choose **Terminal -> Run Task -> Build Wireless Pico W Pair**,
or run from the project folder:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build_wireless.ps1
```

This uses `PICO_BOARD=pico_w`, the RP2040 toolchain, and `build_wireless/`.
It leaves the original `build/` configuration separate. The script uses the
Pico SDK tools already installed under your user profile.

1. Disconnect both boards. Label one **GROUND** and the other **CONTROLLER**.
2. Hold BOOTSEL on the ground station while plugging it into USB. Copy
   `build_wireless/SPORTS_GROUND.uf2` to the `RPI-RP2` drive. It reboots automatically.
3. Repeat for the handheld, copying `build_wireless/SPORTS_CONTROLLER.uf2`.
4. Power both normally. The ground station starts its Wi-Fi AP; the controller
   joins automatically. Either power-up order works; joining may take several seconds.
5. VGA should display the existing main menu and **Wi-Fi linked** at the bottom.
   **Waiting for controller** means no recent valid controller packets have arrived.

Upload one board at a time so you can identify the intended BOOTSEL drive.
The old **Run Bowling Demo (USB)** task flashes the wired demo and is not the
wireless upload task. Building does not automatically flash either board.

## Play without a computer

1. Move the handheld joystick left/right across menu tiles and up/down between rows.
2. **Click and release SW** to select Bowling.
3. Place the controller flat, MPU6050 +Z upward. Click/release SW, then leave it
   untouched for three seconds to calibrate. Wait for **READY TO BOWL**.
4. Pick it up using the training-data grip. Move the joystick to set the starting position.
5. Click/release SW to start the three-second countdown. Hold still for the next
   0.5 seconds, throw during the two-second **THROW NOW** cue, then hold still
   for 0.5 seconds. The controller records locally at 100 Hz.
6. The ground station animates the trajectory after capture. Move the joystick
   after playback to reposition, or click SW for another throw.
7. **Hold SW for one second** to return to the main menu and cancel an active capture.

SW activates on release, so a long press does not also generate a short click.
Settings remains a USB text-tuning screen; SW returns from it. Basketball and
Cup Pong retain the pulled version's display previews, not full playable games.

## Wi-Fi configuration

The defaults in `wireless/wifi_config.h` are:

| Setting | Value |
| --- | --- |
| SSID | `PicoSports` |
| WPA2 password | `picosports4760` |
| Ground station static IP | `192.168.4.1` |
| Controller static IP | `192.168.4.2` |
| UDP port | `4760` |

This is a dedicated two-device network with static addresses. There is **no DHCP
server or Internet service**; your laptop does not need to join it. Use distinct
SSID/password values if another pair nearby uses this firmware. Change the header
and rebuild/reflash **both** boards together. One controller per ground station
is supported. Do not connect this AP to your campus network.

## Optional USB diagnostics and tuning

Open the **ground station's** USB serial port at 115200 baud with newline endings.
Its reconnect banner identifies it as `SPORTS_GROUND`. The controller's serial
output identifies it as `CONTROLLER` and reports detailed calibration failures.
Use these commands on the ground station:

```text
MENU
CAL
THROW
SET hook_gain 1.5
SET start_x -0.5
REPLAY
RESET
INFO
```

`CAL` and `THROW` are sent over UDP; their completion is asynchronous. Enter also
selects a menu item or starts a throw. `INFO` retains the `BOWL,1,healthy,calibrated`
format for host compatibility. The existing `host/play_bowling.py --port COMx`
terminal can be used on the **ground station's COM port** after closing Serial
Monitor. A controller USB port does not accept game commands. The game does not
save or modify the training CSV files.

## What happens over UDP

- Controller core 0 services Wi-Fi, reads the joystick, debounces SW, and sends
  status at approximately 50 Hz. Core 1 calibrates/captures the IMU locally.
- Completed captures are classified using the existing trained bowling model on
  the handheld. The resulting class, power, hook, and features are sent to the
  ground station, where the existing physics, settings and VGA rendering run.
- Ground commands repeat every 100 ms with a request ID and boot-session IDs.
  The controller acknowledges the request in status packets and executes it once.
- Completed results repeat in status until a new command. The ground station
  applies each result once. Sequence numbers reject duplicates/reordered packets;
  packet length, version, sender address and field ranges are validated.
- Button counters survive individual lost status packets. New connections establish
  a counter baseline so a reconnect does not accidentally click the menu.
- After one second without controller status, the ground station cancels the
  current throw and stops joystick movement. The controller cancels after 1.5 seconds
  without commands. Reconnect starts with STOP, so an interrupted throw is not replayed.
- All packet fields use a fixed versioned little-endian/IEEE754 RP2040 layout;
  both devices must run matching builds. This is a prototype protocol, not a
  general-purpose or authenticated Internet service.

## Pico W RAM and display changes

The original two 153,600-byte VGA buffers cannot fit in a Pico W's 264 KB SRAM.
The wireless ground target uses **one** framebuffer, retaining 640x480 and all
16 colors. Static screens redraw only when changed; animation updates are capped
at 30 Hz. Single buffering can produce tearing/flicker during redraws. The
original Pico 2 target keeps double buffering. The VGA timing retains the
existing 150 MHz system-clock setting (an RP2040 overclock); actual monitor and
board stability need bench testing. VGA claims its PIO/DMA resources before
Wi-Fi initialization so the Wi-Fi driver allocates remaining resources.

## Troubleshooting

- **Collection screen instead of menu:** wrong UF2. Reflash `SPORTS_GROUND.uf2`.
- **Waiting for controller:** check power, matching SSID/password/builds, and that
  controller/ground UF2s were not swapped. Wait 5-10 seconds; the controller retries.
- **Menu works, IMU error:** check the handheld's GP8/9 wiring, 3.3 V and address 0x68.
- **Calibration fails:** +Z upward on a stationary surface; the controller USB
  terminal prints the detailed reason, and the ground terminal prints a shortened reason.
- **Invalid capture:** keep the gesture inside THROW NOW; I2C errors, timing gaps,
  sensor saturation and weak/stationary gestures are rejected.
- **Unexpected motion on menu:** verify Vy/ADC0 and Vx/ADC1, common ground and 3.3 V.
- **PC says no Internet or cannot obtain an IP:** expected; this is a static-address
  device network, not a laptop hotspot. Use USB for tuning.

## Verification

Both UF2s are built for `pico_w`. Desktop tests cover packet validation, sequence
wrap/reordering, stale/duplicate results, remote menu input, cancellation, timeout,
and reconnect behavior. Existing bowling regression tests cover the game model.
These are software checks; two-board Wi-Fi range, latency, power stability,
IMU timing under load, and physical VGA output still require bench verification.
