# Active Spoiler

ESP32 / Arduino prototype mapping MPU6050 accelerometer-derived roll and pitch to two servo outputs. This is an experimental controller, not a validated automotive system.

## Firmware behavior

- Three editable presets with 21-point response curves from -90 to +90 degrees.
- Shared pitch response and mirrored roll response; combined output constrained to 0–80 degrees.
- Browser calibration and curve editing; presets and offsets stored in SPIFFS.
- The current orientation calculation uses accelerometer angles. The bundled Kalman header is not used by the main control loop.

## Hardware

- ESP32 DOIT DevKit V1, MPU6050 on the board's I2C bus.
- Left servo on GPIO26; right servo on GPIO27; servo PWM at 50 Hz.
- Provide an appropriate servo power supply and common ground; verify the mechanism before enabling motion.

## Build

Install PlatformIO, then run `pio run`. Use `pio run -t upload` only when ready to flash a connected board. The configuration declares the previously machine-local Arduino dependencies explicitly.

Before connecting, change the demonstration access-point password in `src/dynamicSpoiler.ino`. The local HTTP interface has no authentication. No private Wi-Fi credentials are included.

## Alternative actuator experiment

`servo-drive/` is a separate ESP32 DC-motor position controller with MT6701 output-shaft feedback and a 1 kHz PID loop. Build it independently with `pio run -d servo-drive`. It is not wired into the main two-servo sketch. Its own README describes tuning, pin mapping, and limitations.
