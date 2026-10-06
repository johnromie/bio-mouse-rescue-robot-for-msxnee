# BIO-MOUSE Rescue Robot — Firmware

Hardware firmware for the BIO-MOUSE search-and-rescue robot: an Arduino that
drives the motors and sensors, an ESP32-CAM that provides Wi-Fi and video, and
a mobile app that controls it all.

```
   ┌──────────────┐   Wi-Fi HTTP    ┌──────────────┐   UART 115200   ┌──────────────┐
   │  BIO-MOUSE   │ <------------>  │  ESP32-CAM   │ <------------> │   Arduino    │
   │  mobile app  │   JSON / MJPEG  │  Wi-Fi + cam │   CMD: / :VAL   │  UNO / Nano  │
   └──────────────┘                 └──────────────┘                 └──────────────┘
                                                                          │
                                                                L298N, AMG8833,
                                                                 whiskers, buzzer,
                                                                 battery divider
```

---

## Project layout

```
robot/
├── firmware/
│   ├── arduino/BioMouse_Arduino/       Arduino UNO / Nano
│   │   ├── BioMouse_Arduino.ino       setup() + loop() only
│   │   ├── config.h                   >>> EVERY PIN AND THRESHOLD <<<
│   │   ├── state.h                    shared state + utilities
│   │   ├── motor.h                    [1]  Motor control (L298N, 4WD)
│   │   ├── thermal.h                  [2]  AMG8833 thermal sensing
│   │   ├── navigation.h               [3]  Thermal-guided navigation
│   │   ├── whisker.h                  [4]  Whisker obstacle detection
│   │   ├── buzzer.h                   [5]  5V active buzzer
│   │   ├── battery.h                  [6]  Battery monitoring
│   │   ├── ultrasonic.h          >>> [11] HC-SR04 PRIMARY front detection <<<
│   │   ├── obstacle.h            >>> NEW avoidance state machine <<<
│   │   ├── uart.h                     [7]  UART command parser + telemetry
│   │   ├── safety.h                   [8]  Safety / emergency stop
│   │   └── modes.h                    [9] Manual  [10] Automatic thermal
│   │
│   └── esp32cam/BioMouse_ESP32CAM/    ESP32-CAM
│       ├── BioMouse_ESP32CAM.ino      setup() + loop()
│       ├── config.h                   >>> EVERY PIN AND SETTING <<<
│       ├── wifi_manager.h             AP / station setup
│       ├── uart_bridge.h              UART link, parses telemetry into state
│       ├── camera.h                   OV2640 initialisation
│       └── http_api.h                 HTTP endpoints the app calls
├── app/
│   └── robot-client.js                drop-in client for your app
├── tools/
│   ├── sim_robot.js                   Arduino simulator (test with no hardware)
│   └── sim_send.js                    send commands to the simulator
└── docs/
    ├── PROTOCOL.md                    the app <-> ESP32 <-> Arduino contract
    ├── PIN_CONFIGURATION.md           every pin, wiring, power budget
    ├── ARCHITECTURE.md                how commands reach the motors
    └── TESTING.md                     staged test procedure
```

The ten requested modules are separate files, so you can tune one behaviour
without reading the rest of the firmware.

---

## Quick start

### 1. Test the protocol with no hardware

```powershell
node tools\sim_robot.js
# in a second terminal:
node tools\sim_send.js CMD:PING
node tools\sim_send.js CMD:FORWARD
```

Follow `docs/TESTING.md` Stage A for the full suite.

### 2. Flash the Arduino

1. Arduino IDE -> Library Manager, install **Adafruit AMG8833 IR Sensor**,
   **Adafruit BusIO** and **Adafruit Unified Sensor**.
2. Open `firmware/arduino/BioMouse_Arduino/BioMouse_Arduino.ino`.
3. **Edit `config.h` to match your wiring.**
4. Upload. Open the serial monitor at 115200 baud.

### 3. Flash the ESP32-CAM

1. Arduino IDE -> Boards Manager, install **esp32** by Espressif Systems (2.0.x+).
2. Select the board and the correct flash size (usually 4MB).
3. Open `firmware/esp32cam/BioMouse_ESP32CAM/BioMouse_ESP32CAM.ino`,
   **edit `config.h`**, and upload.

### 4. Drive it

1. Join the `BIO-MOUSE-XXXX` Wi-Fi network (password `biomouse123`).
2. Open **`http://192.168.4.1`** in a phone browser for an instant control panel.
3. Or point your app at the same address using `app/robot-client.js`.

---

## Control surface

**Commands the app sends:** `FORWARD` `BACKWARD` `LEFT` `RIGHT` `STOP` `SCAN`
`SPEED` `MODE:MANUAL|AUTO`, plus `BUZZER`, `ALERT:RESET`, `SELFTEST`.

**Status the app receives:** `TEMPERATURE` `THERMAL_STATUS` `OBSTACLE_LEFT`
`OBSTACLE_RIGHT` `BATTERY` `ROBOT_STATUS` `BUZZER_STATUS` `CONNECTION_STATUS`,
plus `heatDirection`, `obstacleRear`, `mode`, `move`, `speed`, `alert`, `sensor`.

Full detail: `docs/PROTOCOL.md`.

---

## Safety design

| Layer | Trigger | Result |
|---|---|---|
| `CMD:STOP` | Operator | Instant stop, honoured in **both** modes, **cancels any avoidance manoeuvre** |
| E-STOP button | Physical | Latched stop until released |
| Failsafe watchdog | No UART traffic for 3 s | Robot stops by itself |
| No operator | Last HTTP client gone | ESP32-CAM sends `CMD:STOP` |
| Thermal alert | Close, sustained heat | Stop + repeating buzzer + alert |
| **Obstacle (ultrasonic or whisker)** | **Distance ≤ 30 cm, or whisker strike** | **Stop → reverse → turn away → resume when clear** |

Conflicts are prevented structurally: only one mode function runs per loop
iteration, and drive commands are **rejected** (`ERROR:MODE_IS_AUTO`,
`ERROR:OBSTACLE_BLOCKED`, `ERROR:AVOID_IN_PROGRESS`) rather than silently
queued. The ESP32-CAM forwards only whitelisted commands — nothing arbitrary
from the network reaches a motor pin.

---

## Obstacle detection: two layers

| Layer | Sensor | Role |
|---|---|---|
| **Primary** | HC-SR04 on `TRIG=D8`, `ECHO=D7` | Range detection, alerts at ≤ 30 cm |
| **Secondary** | LEFT / RIGHT / BOTH whiskers | Physical backup for irregular debris |

The left and right whiskers are made from guitar strings.

Sound reflects poorly off thin poles, angled glass and soft fabric, so the
ultrasonic cannot be the only defence. Both sensors feed **one** state-based
avoidance sequence:

```
REACTING → STOPPED → REVERSING → TURNING → RESUMING
```

It is fully **non-blocking** (`millis()` timing, no `delay()`), so an emergency
STOP from the app is still honoured mid-manoeuvre. Turn direction follows the
sensor: left whisker → turn right, right whisker → turn left, both whiskers →
a wider escape turn.

A **no-echo reading is treated as invalid** — never as an obstacle, and never
as "clear" either.

---

## Reporting heat, not people

The firmware reports temperature facts only:

- `NONE` — nothing above ambient
- `HEAT_DETECTED` — a rise of ≥ 3 °C above ambient
- `POSSIBLE_SURVIVOR` — a rise of ≥ 6 °C above ambient
- ≥ 32 °C sustained for 1.5 s → stop, buzz, and alert

An engine, an animal, a fire or a sunlit wall all read the same. There is no
"survivor confirmed" state, by design. Your app should present these as
indications of heat and recommend that a trained rescue team confirms them.

---

## Before you power up

- [ ] `config.h` pins match your actual wiring
- [ ] **ENA/ENB are PWM pins** and the L298N jumpers are removed
- [ ] ESP32 3.3 V logic is not connected straight to a 5 V Arduino TX pin
- [ ] Battery negative, L298N GND and Arduino GND are tied together
- [ ] Motors run from a battery, not the Arduino's 5 V regulator
- [ ] The robot is on a bench with the wheels lifted for the first test

See `docs/PIN_CONFIGURATION.md` for the full wiring guide and `docs/TESTING.md`
for the staged procedure.

---

**Academic / prototype project.** This robot assists a search; it does not
replace a trained rescue team.
