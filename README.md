# Active Spoiler · Project Tsintsadze

Two ESP32 DevKit firmwares: **N1 controls four encoder-feedback DC motors**;
**N2 reads the MPU6050, computes wing/motor targets, and serves the dashboard**.
This repository contains the current two-board implementation. It replaces the previous standalone mapping and servo-drive programs. The mechanical prototype is intended for a third-generation BMW 6 Series; aerodynamic performance is not established by firmware tests.

## Design overview

- Four bars, one guided motion: each wing rides on connected arms that lift and tilt it along a defined path, rather than simply turning around a hinge.
- Getting the dimensions right: arm lengths and pivot spacing must produce the required lift, tilt, and clearance together. Near the flat starting position, small errors or joint play can make the linkage bind or move unpredictably.
- Two motors per wing: powering both arms is intended to share the load. The unequal arms need different, coordinated angles; encoder feedback helps each motor follow its target rather than fight the linkage.
- Tune the response visually: set the sensor's neutral reference, then drag curve points or enter values to choose how sensed roll and pitch move the wings. Three presets let different responses be compared.
- Wing angles first, motor angles second: roll gives a mirrored left/right response, while pitch acts on both wings. The curves combine into 0–70° wing commands, and the linkage calculation converts each command into two motor targets.
- Keep motion separate from the dashboard: one ESP32 handles sensing and tuning; the other controls the motors. Manual wing and individual-motor modes help check the mechanism before trying automatic operation.
- Test the physical result: position feedback corrects tracking errors, and lost encoder data or communication disarms the motors. The sensor estimates tilt from acceleration, so response under braking, cornering, and aerodynamic load still needs testing.

The four-bar geometry and its sensitivity near aligned links are explained in [Carnegie Mellon's linkage reference](https://www.cs.cmu.edu/~rapidproto/mechanisms/chpt5.html). The paired targets and editable mappings are implemented in `include/model.h`. The two motors follow different arm angles for a single commanded wing angle; there is no separate torque-sharing controller.

## Mechanics and wiring

**M1 and M3 have 125 mm arms. M2 and M4 have 100 mm arms.**
M1/M2 drive the left wing; M3/M4 drive the mirrored right linkage.
Each linkage has 55 mm shaft spacing and a 30 mm connecting link carrying the wing.
Horizontal arms and horizontal wing define zero. Positive local angles raise each linkage.
Dashboard/curve wing commands remain **0–70°**, and the solver uses their **negative
physical wing angle** on both sides, choosing the branch where both motor arms rise.
For example, a +40° wing command solves for −40° physically: long arm **+72.3402°**,
short arm **+86.6165°**.
Do not assume the same encoder signs on the two sides: calibrate each motor.

| N1 motor | PWM1 / PWM2 GPIO | Encoder bus SDA / SCL | MT6701 address |
|---|---|---|---|
| M1 | 13 / 12 | 22 / 21 | `0x06` |
| M2 | 14 / 27 | 22 / 21 | `0x46` |
| M3 | 26 / 25 | 32 / 33 | `0x06` |
| M4 | 5 / 18 | 32 / 33 | `0x46` |

N1 bus 1 intentionally uses **SDA22/SCL21**, unlike N2's MPU bus.
Connect the MPU6050 to **N2 SDA21/SCL22**, at `0x68` (or `0x69`, auto-detected).
Use pull-ups to 3.3 V on each I²C bus; the two MT6701 addresses must be configured
on the modules before connecting both sensors to the same bus.

UART2 wiring: **N1 GPIO17/TX2 → N2 GPIO16/RX2**, **N2 GPIO17/TX2 → N1 GPIO16/RX2**,
and **GND ↔ GND**. Both UARTs use 115200 baud, 8N1, 3.3 V logic.
Power motors through their H-bridges from a suitable motor supply, not ESP32 power pins;
join logic/driver grounds. GPIO12 and GPIO5 are ESP32 strapping pins: external driver
circuitry must not force an incompatible level during reset.

## Build, flash, and connect

From this directory, with PlatformIO installed:

```sh
pio run                         # build both boards
pio run -e esp32_n1 -t upload --upload-port /dev/cu.YOUR_N1_PORT
pio run -e esp32_n2 -t upload --upload-port /dev/cu.YOUR_N2_PORT
pio device monitor --port /dev/cu.YOUR_PORT --baud 115200
python3 test/run_host.py         # host logic tests; requires clang++ or g++
node test/dashboard_api.js       # request queue, debounce, timeout, STOP tests; requires Node.js
```

Choose each board's actual USB port and flash the matching environment.
The platform is pinned to Espressif32 6.12.0 (Arduino ESP32 2.0.17); ArduinoJson 7.4.2
is the only external library. No filesystem upload or internet assets are required.
Build outputs are `.pio/build/esp32_n1/firmware.bin` and `.pio/build/esp32_n2/firmware.bin`.

Connect to Wi-Fi **SpoilerTuner**, password **spoiler123**, and open **http://192.168.4.1**.
Both boards boot disarmed. The dashboard remains available without an MPU6050;
automatic mode requires a healthy MPU. The AP uses a published demonstration credential for local commissioning; change it in `src/n2.cpp` for your own setup.

## Physical setup, test, and calibration

1. **Power off.** Verify arm lengths, shaft/link spacing, motor numbering, addresses,
   power wiring, common grounds, and crossed UART connections.
2. **Remove both 30 mm connecting links.** Flash N1 and N2; power up disarmed.
3. Open the dashboard. Confirm UART connected and all four motor rows show valid
   encoder readings. Rotate each disconnected arm by hand to identify its sensor.
4. Select **Bench · individual motors**, then **Apply mode & disarm**. Leave 5° of
   clearance in both directions and run **Test polarity (5°)** for one motor at a time.
   N1 sets a temporary target 5° from the current encoder angle (normally +5°, or −5°
   near the +45° bench limit). It applies PWM 1500 using the current drive polarity
   until the encoder measures 5° of travel **toward or away from that target**.
   Driving stops at that threshold; after 50 ms settling, it retains polarity for
   motion toward the target or flips output polarity for motion away. PID gains,
   logical angle direction, and saved zero remain unchanged. All motors stay disarmed.
   A **5-second timeout**, STOP ALL, encoder fault, or UART loss stops the test;
   incomplete/aborted tests leave polarity unchanged. The dashboard remains responsive.
5. Configure each **Angle: normal/reversed** setting so its positive logical rotation
   raises the arm in that wing's local frame. M3/M4 are mirrored, so their signs may
   differ. Logical reversal changes feedback and drive signs together. It does not
   replace output-polarity calibration.
6. Disarm and place all arms at their **horizontal mechanical reference**. Use each
   **Set zero** or **Set all horizontal positions to 0°**. Wait for **Settings stored**.
   Saved zeros are required before arming. With links removed, initially set all manual
   targets to zero; arm, command a small positive target (for example 2°) individually,
   and verify upward movement. Stop, correct direction, and repeat as needed.
7. Bench targets allow ±45° per motor. Check small individual moves first. Then select
   **Manual · wing angles** and test paired targets with links still removed. At a 70°
   wing command (−70° physical), M1/M3 target **82.6462°**, M2/M4 **106.7024°**.
   Each wing is independent. These paired-mode targets can exceed the separate ±45° bench limit.
8. Return both wings to 0°, disarm, **remove motor power**, and install the connecting
   links without moving the horizontal references.
9. Test assembled motion in manual-wing mode, starting at a few degrees. Increase
   gradually toward 70°, checking clearance and tracking. STOP ALL is always visible.
   Tune shared PID settings as needed; applying tuning disarms all motors.
10. Hold the MPU stationary at the desired vehicle reference and select **Calibrate MPU
    here**. Edit the active preset's roll/pitch curves, select automatic mode, and re-arm.
    Roll curves mirror left/right; pitch is shared. Curve values are wing degrees;
    contributions add and saturate at 70°. The sensor-axis signs are defined in `sampleMpu()` in `src/n2.cpp`.
11. Verify STOP ALL; unplug one encoder and confirm all motors stop; disconnect UART
    and confirm shutdown after 300 ms; reboot each ESP32 and confirm no automatic
    re-arm. Restore connections, check healthy readings, then explicitly re-arm.

Output polarity, direction changes, zeros, mode changes, and PID updates disarm the
system. Reversing all directions flips each motor's current direction setting.
Calibration and tuning persist on N1; curves, active preset, and MPU offsets persist
on N2. Writes are deferred until disarmed; leave boards powered until storage completes.
Arm state, operating mode, and manual targets are not persisted.

Dashboard buttons acknowledge clicks immediately, suppress duplicate clicks for
250 ms, and queue different actions. Only the pending action's button is disabled.
Curve edits queued for the same point keep the latest value. ARM must be clicked
when the current action finishes; it is never queued for later execution. STOP ALL
discards queued actions and interrupts a pending motor command. All remote commands
wait for N1 confirmation asynchronously, keeping HTTP available for other requests.
Telemetry updates every 200 ms (400 ms while applying commands); clicks cancel stale
background reads. HTTP request timeouts release controls if the connection stalls.
N2 groups flash saves after a one-second pause in editing, while disarmed.

## Timing and limits

| Board/core | Work |
|---|---|
| N1 core 1 | M1/M2, I²C controller 0, 1 kHz PID task |
| N1 core 0 | M3/M4, I²C controller 1, 1 kHz PID task; lower-priority UART |
| N2 core 1 | MPU sampling at 100 Hz; mapping/IK and UART targets at 50 Hz |
| N2 core 0 | Wi-Fi, HTTP/dashboard up to 5 Hz, deferred persistence |

N1 uses concurrent 400 kHz I²C buses, repeated-start two-byte encoder reads, 2 ms
transaction timeouts, measured PID `dt`, and 19.5 kHz 12-bit PWM. UART uses interrupt-driven
TX buffers so sending a frame does not wait for its wire transmission, plus bounded
COBS frames, CRC-16/CCITT-FALSE, sequence numbers, boot IDs, and fault epochs.
Any encoder failure or 300 ms UART timeout disarms all motors; recovery needs manual
re-arming. A failed MPU stops automatic operation. Bench mode does not need an MPU.

Normal modes enforce valid paired inverse-kinematics targets and a **0–70° commanded
wing range**, corresponding to **0 to −70° physical**, on both boards. This is a target limit; PID overshoot and mechanical
clearance must be verified physically. Bench mode bypasses linkage constraints and
is only for disconnected links. Saved encoder zeros restore the nearest turn (±180°);
do not move the mechanism through extra full turns while unpowered.

The dashboard reports maximum task work time, missed 1 ms periods, and I²C/UART errors.
Flash writes and sensor faults can produce missed periods while disarmed; verify
steady-state timing on the actual boards. Motor polarity, sensor orientation, loop
timing, power-stage behavior, and physical travel cannot be established by host tests.

## Source and HTTP interfaces

`src/n1.cpp` owns motor control; `src/n2.cpp` owns MPU/control/networking.
`include/model.h` contains portable PID, mapping, encoder, and linkage logic;
`include/protocol.h` defines the versioned UART frames; `include/dashboard.h` holds
the standalone UI. N2 reads the MPU registers directly to check every I²C transaction.

GET `/status`, `/config`, `/preset?p=0`, and `/` provide telemetry, configuration,
curves, and UI. POST requests use JSON:

| Route | Example body |
|---|---|
| `/mode` | `{"mode":1}` (0 automatic, 1 wings, 2 bench) |
| `/manual` | `{"angles":[10,20]}` (two wing or four bench angles) |
| `/arm` | `{"enabled":false}` |
| `/motor` | `{"operation":"zero","mask":15}` |
| `/motor` | `{"operation":"direction","mask":4,"value":4}` |
| `/motor` | `{"operation":"polarity","mask":1,"value":0}` |
| `/motor` | `{"operation":"test","mask":1}` (one motor, bench mode) |
| `/pid` | `{"kp":35,"ki":0.5,"kd":2.5,"db":0.7,"minPwm":350,"enP":true,"enI":false,"enD":true}` |
| `/set` | `{"preset":0,"field":"rollToRight","index":12,"value":15}` |
| `/save`, `/use`, `/reset` | `{"preset":0}` |
| `/calibrate` | `{}` |

Motor masks use M1=1, M2=2, M3=4, M4=8, all=15. Direction/polarity `value` specifies
which selected motors are reversed. `/mode`, `/arm`, `/motor`, and `/pid` return
HTTP 202 with `request`, `kind`, and `timeoutMs`. Poll GET
`/operation?request=ID`: HTTP 202 means pending, HTTP 200 confirms N1 completed it,
and an error reports failure/cancellation. The last eight operation results are
retained. The browser handles polling automatically; STOP ALL takes priority over
any pending action. Mode changes apply after N1 confirms its stop. A stale state is
rejected: refresh and retry; it never silently arms. Local curve/manual commands
return immediately after N2 validates and applies them in RAM.
