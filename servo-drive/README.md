# Spoiler Servo Drive

Closed-loop DC-motor servo for an ESP32, built with PlatformIO / Arduino. An **MT6701** magnetic encoder on the **output shaft** (after the gearbox) provides position feedback. A **1 kHz PID loop** drives a TA6586-style H-bridge. Everything is tuned and controlled from a **built-in Wi-Fi web dashboard**.

All firmware lives in [`src/main.cpp`](src/main.cpp).

## Features

- 1000 Hz PID control loop, pinned to core 1 (FreeRTOS task)
- 14-bit absolute encoder (MT6701 over I²C) with **multi-turn tracking** (wrap-around detection)
- 19.5 kHz, 12-bit PWM (0–4095) for silent, high-resolution motor drive
- Independent **P / I / D enable toggles**
- Derivative on measurement with a low-pass filter (no derivative kick; less encoder noise)
- Integral with clamped anti-windup; it is decayed inside the deadband
- Stiction compensation (`Min PWM`) and a configurable **deadband**
- Web UI with a draggable 0–180° dial, live telemetry, and PID tuning
- **Auto-detect motor polarity** with a short test pulse
- Settings and zero position persist in flash (`Preferences` / NVS)
- Fail-safe: PWM is cut if the encoder stops responding on I²C
- Motor is **disarmed at every boot**

## Hardware

| MT6701 module | ESP32 DevKit |
| --- | --- |
| SDA | GPIO 21 |
| SCL | GPIO 5 |
| GND | GND |
| VCC | 3.3 V |

| H-bridge (TA6586) | ESP32 | Notes |
| --- | --- | --- |
| Forward input | GPIO 23 | PWM channel 0 |
| Reverse input | GPIO 22 | PWM channel 1 |

- I²C runs at 400 kHz. The MT6701 address is `0x06` and the angle register is `0x03`.
- Target board: `esp32dev`, Arduino framework (compatible with Arduino-ESP32 core 2.x and 3.x LEDC APIs).

## Build & Upload

Requires [PlatformIO](https://platformio.org/).

```bash
platformio run                    # build
platformio run --target upload    # flash
platformio device monitor         # serial monitor, 115200 baud
```

Configuration is in [`platformio.ini`](platformio.ini) (`espressif32`, `esp32dev`, `arduino`, `monitor_speed = 115200`).

## Wi-Fi & Accessing the UI

The ESP32 runs in **AP + STA** mode:

| Mode | Details |
| --- | --- |
| Access point (always on) | SSID `ESP32_Servo_Drive`, password `12345678`, IP typically `192.168.4.1` |
| Station (optional) | Set `WIFI_SSID` / `WIFI_PASS` at the top of `main.cpp`. The IP is printed on the serial monitor. |

If `WIFI_SSID` is left at `YOUR_WIFI_SSID`, station mode is skipped. If the connection times out (8 s), the AP stays available.

Open `http://<ip>/` in a browser.

> [!NOTE]
> Change `AP_PASS` before deploying. There is **no authentication** on any endpoint.

## Web Dashboard

- **Dial**: drag the blue knob to set the target angle (0–180°). The green line shows the real angle.
- **Telemetry**: target, real (multi-turn) angle, raw MT6701 angle, PWM output (−4095…4095), and sensor status.
- **MOTOR ARM button**: enables or disables the motor. The motor is disabled on every boot.
- **Calibrate Current Position as 0°**: sets the current position as zero. The offset is saved to flash and restored on the next boot.
- **PID & Drive configuration**: Kp, Ki, Kd (each with an ON/OFF toggle), Min PWM, Deadband, and Direction polarity.
- **Apply & Save to EEPROM**: stores the settings in flash. Toggling P/I/D or polarity also saves immediately.
- **Auto-Detect Polarity (Test Pulse)**: see below.

## Control Algorithm

The loop runs every 1 ms (`dt = 0.001`) in `pidLoopTask`:

1. Read the 14-bit angle (`0..16383` counts, `360/16384` deg per count).
2. Detect wrap-around: a jump of more than ±8192 counts increments or decrements `rotationCount`. This gives a continuous multi-turn angle.
3. `realAngle = continuousAngle − zeroContinuousAngle`
4. `error = target − realAngle`
5. If the motor is disarmed **or** `|error| ≤ deadband`, output 0 and decay the integral (×0.95).
6. Otherwise:
   - `P = Kp · error`
   - `I += error · Ki · dt`, clamped to ±4095
   - `D = Kd · filteredDerivative`, where the derivative is taken on the measurement with a filter `0.85·old + 0.15·new`
   - `pid = P + I + D`
   - Stiction compensation: add `±minPwmOffset` in the direction of `pid`
   - Clamp to ±4095
7. `writePWM()` splits the signed value into forward/reverse duty cycles. If `invertMotor` is set, the sign is flipped first.

### Default Parameters

Tuned for roughly **1.28° of output-shaft backlash**:

| Parameter | Default | Rationale |
| --- | --- | --- |
| Kp | 35.0 | |
| Ki | 0.5 (**OFF**) | Prevents windup while the motor crosses the backlash gap |
| Kd | 2.5 | Acts as an electronic brake once the gear teeth engage |
| Deadband | 0.70° | About half the backlash; stops hunting/chatter |
| Min PWM | 350 | Overcomes stiction |
| Polarity | Normal | Use auto-detect if the motor runs away |

Settings saved in flash override these defaults.

## HTTP API

All endpoints are `GET`.

| Endpoint | Parameters | Description |
| --- | --- | --- |
| `/` | – | Web dashboard |
| `/status` | – | JSON: `raw`, `real`, `pwm`, `err`, `target` (polled every 100 ms by the UI) |
| `/config` | – | JSON: `kp`, `ki`, `kd`, `minPwm`, `db`, `enP`, `enI`, `enD`, `motor`, `inv`, `target` |
| `/setTarget` | `val` (degrees) | Set the target angle |
| `/setMotor` | `en` (`1`/`0`) | Arm or disarm the motor |
| `/setZero` | – | Zero the angle at the current position (persisted) |
| `/setPid` | `kp`, `ki`, `kd`, `enP`, `enI`, `enD`, `minPwm`, `db`, `inv` | Update and persist parameters |
| `/testPolarity` | – | Run the polarity auto-detect test |

Example:

```bash
curl "http://192.168.4.1/setMotor?en=1"
curl "http://192.168.4.1/setTarget?val=45"
curl "http://192.168.4.1/status"
```

## Polarity Auto-Detect

`/testPolarity` (or the UI button) disarms the motor, applies a positive PWM of 700 for 80 ms, waits 50 ms for the shaft to settle, and measures the angle change:

| Result | Meaning |
| --- | --- |
| `|Δ| < 0.3°` | Shaft barely moved. Increase the test PWM in code to overcome friction. |
| `Δ > 0` | Polarity is correct. |
| `Δ < 0` | `invertMotor` is flipped and saved to flash. |

> [!WARNING]
> The test moves the motor open-loop. Make sure the mechanism is free to move a few degrees, and keep the mechanism clear while testing. After the test the motor is left **disarmed**, so re-arm it from the UI. If a result is "inverted", run the test again to confirm.

## Tuning Guide

Recommended order for a geared output-shaft setup:

1. Run **Auto-Detect Polarity** and confirm positive PWM increases the angle.
2. Calibrate zero at your mechanical home position.
3. Start with **Ki OFF**, set the deadband to about half the measured backlash, and raise Kd until overshoot is damped.
4. Increase Kp until the response is fast without oscillation.
5. Raise **Min PWM** only as far as needed to start the shaft moving.
6. Enable a small Ki last, only if there is a steady-state error beyond the deadband.

## Persistence

Stored in NVS under the namespace `servo`:

| Key | Type | Description |
| --- | --- | --- |
| `kp`, `ki`, `kd` | float | PID gains |
| `enP`, `enI`, `enD` | bool | Term enables |
| `minPwm` | int | Stiction offset |
| `db` | float | Deadband (deg) |
| `inv` | bool | Motor polarity |
| `zeroOff`, `hasZero` | float, bool | Saved zero reference (raw degrees) |

The motor-armed state and target angle are **not** persisted (the motor boots disarmed, target 0).

## Safety Notes

- The motor boots disarmed. Arm it only when the mechanism is clear.
- Output is cut automatically on an I²C/encoder fault. Check **Sensor Status** in the UI.
- There is no authentication and no travel limit in firmware. The UI limits the dial to 0–180°, but `/setTarget` accepts any value.
- The dashboard is served unencrypted over HTTP. Do not expose it to untrusted networks.

## Project Structure

```
.
├── platformio.ini     # PlatformIO environment
├── esp pinout.md      # Wiring notes
└── src/
    └── main.cpp       # Firmware + embedded web UI
```
