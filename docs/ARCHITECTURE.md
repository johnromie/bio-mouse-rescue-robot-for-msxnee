# BIO-MOUSE Architecture

## The big picture

```
   ┌──────────────┐   Wi-Fi HTTP    ┌──────────────┐   UART 115200   ┌──────────────┐
   │  BIO-MOUSE   │ <------------>  │  ESP32-CAM   │ <------------> │   Arduino    │
   │  mobile app  │   JSON / MJPEG  │              │   CMD: / :VAL   │  UNO / Nano  │
   └──────────────┘                 │  Wi-Fi + cam │                 │ motors+sense │
                                     └──────────────┘                 └──────────────┘
                                                                             │
                                                                   L298N, AMG8833,
                                                                whiskers, buzzer,
                                                                 battery divider
```

**Why the split this way?** All safety-critical decisions live on the Arduino.
If the Wi-Fi radio glitches, drops packets, or the ESP32 hangs, the robot stops —
it never wanders off because the network went quiet. The ESP32-CAM is a
*communications and vision* device; the Arduino is the *motion and sensing*
device. Neither can drive the robot alone.

---

## 1. How a command reaches the motors

Take the operator tapping **FORWARD**:

| Step | Where | What happens |
|---|---|---|
| 1 | `app/robot-client.js` | `robot.forward()` issues `GET /api/cmd?c=FORWARD` |
| 2 | `http_api.h` → `translateCommand()` | Matches `FORWARD` against a **whitelist**; produces `"CMD:FORWARD"` |
| 3 | `uart_bridge.h` → `sendToArduino()` | Writes `CMD:FORWARD\n` to UART2 |
| 4 | `uart.h` → `uartService()` | Reads bytes, assembles the line, calls `handleCommandLine()` |
| 5 | `uart.h` | Validates: AUTO mode? obstacle present? Sets `activeCommand` |
| 6 | `modes.h` → `manualModeService()` | Turns `activeCommand` into left/right speed targets |
| 7 | `motor.h` → `applyRampedSpeed()` | Ramps PWM, writes `IN1..IN4` and `ENA`/`ENB` |
| 8 | L298N | H-bridge reverses polarity and applies power |
| 9 | Motors | The robot moves |

The return path mirrors it: the Arduino emits `TEMP:`, `BATTERY:` etc. every
250 ms; `uart_bridge.h` parses them into `RobotStatus`; `/api/status` serves
them as JSON; `robot-client.js` merges them into `status` and calls your update
callback.

### What each layer decides

| Layer | Responsibility |
|---|---|
| `robot-client.js` | What the operator wants; handles connection state |
| `http_api.h` | Which commands are legal at all (whitelist) |
| `uart_bridge.h` | Wire format, and the keep-alive that keeps the robot alive |
| `uart.h` | Mode conflicts, obstacle blocking, valid command parsing |
| `modes.h` | **Which mode owns the motors right now** |
| `navigation.h` | The thermal-guided steering decision (AUTO only) |
| `motor.h` | The only code that touches a motor pin |
| `safety.h` | E-STOP and the failsafe watchdog |

---

## 2. Mode arbitration — how conflicting commands are prevented

The most reliable way to stop two things fighting over the motors is to make it
*impossible* for two things to drive at once. In `loop()`:

```c
if (robotMode == MODE_AUTO) autoModeService();     // [10]
else                         manualModeService();   // [9]
```

Exactly one branch runs per pass. On top of that, the parser rejects
conflicting commands at the source:

```c
if (isDrive) {
  if (robotMode == MODE_AUTO) { Serial.println(F("ERROR:MODE_IS_AUTO")); return; }
  if (obstacleLeft || obstacleRight || obstacleRear) {
    Serial.println(F("ERROR:OBSTACLE_BLOCKED")); return;
  }
  ...
}
```

`CMD:STOP` is checked **before** the drive commands, so it is honoured in both
modes — and in AUTO it also latches `manualOverrideLatch`, which keeps autonomy
suspended until the operator deliberately clears it.

---

## 3. Thermal-guided navigation

The AMG8833 gives an 8×8 array. Each frame is reduced to three sectors:

```
cols 0-2  = LEFT     cols 3-5 = CENTER     cols 6-7 = RIGHT
```

**Ambient tracking.** Rather than comparing against a fixed 25 °C, the firmware
tracks the rolling minimum temperature as the ambient reference. Ambient snaps
*down* to a new minimum and creeps *up* slowly. This matters because a rescue
site may be 15 °C or 35 °C, and a fixed threshold would either flood with false
alerts or detect nothing.

**Direction confirmation.** The strongest sector must win `HEAT_CONFIRM_FRAMES`
(3) consecutive frames before `heatDirection` changes, so one noisy frame cannot
swing the robot the wrong way.

**The decision** (`navigation.h`):

| Condition | Action |
|---|---|
| Sensor missing | `STOP` — never drive blind |
| Alert active | `STOP` |
| Heat rise < 3 °C | `SCAN` — pivot to sweep the array |
| Strongest heat LEFT | `LEFT` |
| Strongest heat CENTER | `FORWARD` |
| Strongest heat RIGHT | `RIGHT` |
| Max ≥ 32 °C **and** held 1.5 s | `STOP` + buzzer + alert |

`SCAN` is rate-limited to one 1.2 s burst at a time, so the robot sweeps rather
than spinning continuously.

**Wording.** `THERMAL:HEAT_DETECTED` and `THERMAL:POSSIBLE_SURVIVOR` are claims
about temperature only. The firmware has no way to know what produced the heat;
an engine or an animal would read identically.

---

## 4. The safety layers

| Layer | Trigger | Response | Configured in |
|---|---|---|---|
| App STOP | Operator | Instant `motorsHardStop()` | `uart.h` |
| E-STOP button | Physical | Latched stop, held until released | `safety.h` |
| Failsafe watchdog | No UART traffic for 3 s | `ROBOT:STOPPED:FAILSAFE_LINK_LOST` | `safety.h` |
| No operator | Last HTTP client gone | ESP32-CAM sends `CMD:STOP` | ESP32-CAM `.ino` |
| Thermal alert | Close, sustained heat | Stop + repeating buzzer + alert | `thermal.h` |
| Obstacle | Whisker hit | MANUAL: stop. AUTO: reverse and steer away | `whisker.h` |

The physical left and right whiskers are made from guitar strings.

`motorsHardStop()` is the only emergency path: it writes all four direction pins
LOW and both speed pins 0 with no ramp, so the motors stop immediately rather
than decelerating.

---

## 5. Wi-Fi topology

**Access point mode (default).** The ESP32-CAM creates `BIO-MOUSE-XXXX`. The
phone joins it and reaches `http://192.168.4.1`. No router, no internet, no PC —
the right choice for a rescue site.

**Station mode.** Set `USE_STATION_MODE = true` to join an existing router. If
the router cannot be reached, it falls back to `BIO-MOUSE-SETUP` as an access
point, so the robot never becomes unreachable.

---

## 6. Video stream vs. the failsafe

`handleStream()` runs until the client disconnects, which blocks the rest of
`loop()` — including `uartBridgePingService()`. The Arduino then hears nothing
for 3 s and stops.

This is a genuine design tension, not something to ignore. Three options:

1. **Poll still frames** (`/api/snapshot`) instead of streaming. The built-in
   control panel does exactly this, at 1 fps.
2. **Close the stream when the video panel is hidden** in the app.
3. If you need continuous video while driving, move the ping onto a hardware
   timer via `esp_timer`, or drop to `FRAMESIZE_QVGA`.

For a rescue robot, option 1 or 2 is the right default: a 1 fps thumbnail is
plenty for navigating rubble, and it keeps the safety link alive.
