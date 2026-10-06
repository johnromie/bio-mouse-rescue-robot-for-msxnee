# BIO-MOUSE Testing Procedure

Test from the inside out. Each stage assumes the previous one passed, so a
failure points at one thing rather than five.

```
Stage A  Simulator (no hardware)      -> protocol correctness
Stage B  Arduino alone (USB serial)   -> motors, sensors, safety
Stage C  Arduino + ESP32-CAM (UART)   -> the real link
Stage D  Phone browser / app (Wi-Fi)  -> the real operator experience
```

> **Before any motor test:** lift the robot off the ground, or remove the
> wheels. A firmware bug during bring-up drives the robot off the bench.

---

## Stage A — Protocol simulator (no hardware)

Reproduces the Arduino end of the protocol so you can build and test the app
and the ESP32-CAM logic before any hardware exists.

```powershell
# terminal 1
node tools\sim_robot.js --port 7777

# terminal 2
node tools\sim_send.js CMD:PING
```

### A1. Basic commands

| Send | Expect |
|---|---|
| `CMD:PING` | `ACK:PING` |
| `CMD:FORWARD` | `ACK:FORWARD`, then `MOVE:FORWARD` in telemetry |
| `CMD:BACKWARD` / `CMD:LEFT` / `CMD:RIGHT` | matching `ACK:` and `MOVE:` |
| `CMD:STOP` | `ROBOT:STOPPED:CMD`, `ACK:STOP`, `MOVE:STOP` |
| `CMD:SPEED:75` | `SPEED:75`, `ACK:SPEED` |
| `CMD:WARP_DRIVE` | `ERROR:UNKNOWN:CMD:WARP_DRIVE` |

### A2. Conflict rule — AUTO mode rejects drive commands

```
CMD:MODE:AUTO      -> MODE:AUTO, ACK:MODE:AUTO
CMD:FORWARD        -> ERROR:MODE_IS_AUTO     (must be rejected)
CMD:STOP           -> ROBOT:STOPPED:CMD      (must still work)
```

**Verified passing.**

### A3. Obstacle blocking

```
SIM:OBSTACLE:LEFT:1   -> OBSTACLE_LEFT:1, OBSTACLE:LEFT
CMD:FORWARD           -> ERROR:OBSTACLE_BLOCKED   (must be rejected)
SIM:OBSTACLE:LEFT:0   -> OBSTACLE:CLEAR
CMD:FORWARD           -> ACK:FORWARD              (allowed again)
```

**Verified passing.**

### A4. Failsafe watchdog

Stay connected, send a drive command, then send **nothing** for 4 seconds:

```
CMD:FORWARD  -> ACK:FORWARD
(wait 4s, no CMD:PING)
             -> EVENT:FAILSAFE_LINK_LOST
             -> ROBOT:STOPPED:FAILSAFE_LINK_LOST
             -> MOVE:STOP
```

The most important safety test in the suite. **Verified passing.**

### A5. Thermal guidance

```
SIM:HEAT:LEFT:36    -> TEMP=36.0  THERMAL:POSSIBLE_SURVIVOR  HEAT:LEFT
SIM:HEAT:CENTER:36  -> TEMP=36.0  THERMAL:POSSIBLE_SURVIVOR  HEAT:CENTER
SIM:HEAT:RIGHT:36   -> TEMP=36.0  THERMAL:POSSIBLE_SURVIVOR  HEAT:RIGHT
SIM:HEAT:LEFT:NONE  -> back to    THERMAL:NONE                HEAT:NONE
```

On real firmware these drive LEFT → `CMD:LEFT`, CENTER → `CMD:FORWARD`,
RIGHT → `CMD:RIGHT`. **Verified passing.**

### A6. Close-range alert

```
SIM:HEAT:CENTER:34  -> TEMP=34.0  THERMAL:POSSIBLE_SURVIVOR  ALERT:ON
```

34 °C exceeds `CLOSE_RANGE_TEMP` (32 °C). On real hardware the robot must also
stop and start buzzing. **Verified passing.**

### A7. Low battery and missing sensor

```
SIM:BATTERY:15  -> BATTERY:15
SIM:SENSOR:0    -> SENSOR:MISSING
```

### A8. HC-SR04 distance reporting

```
SIM:DIST:120    -> OBSTACLE:CLEAR:120  DISTANCE:120.0CM  DISTANCE_ALERT:OFF
SIM:DIST:30     -> OBSTACLE:DETECTED:30 DISTANCE:30.0CM  DISTANCE_ALERT:ON
SIM:DIST:29     -> OBSTACLE:DETECTED:29 DISTANCE:29.0CM  DISTANCE_ALERT:ON
SIM:DIST:31     -> OBSTACLE:CLEAR:31   DISTANCE:31.0CM   DISTANCE_ALERT:OFF
SIM:DIST:NONE   -> ULTRASONIC:NO_ECHO   DISTANCE:-1.0CM    OBSTACLE:CLEAR:NA
```

The boundary matters: **30 cm alerts, 31 cm does not.**

### A9. Full avoidance sequence

Drive forward, then place an obstacle at 20 cm:

```
CMD:FORWARD          -> MOVE:FORWARD
SIM:DIST:20          -> AVOID:REACTING   MOVE:STOP        (immediate stop)
                     -> AVOID:STOPPED    MOVE:STOP
                     -> AVOID:REVERSING  MOVE:BACKWARD
                     -> AVOID:TURNING    MOVE:SCAN
                     -> AVOID:RESUMING   MOVE:STOP
SIM:DIST:150         (path clear)
                     -> AVOID:IDLE       MOVE:FORWARD     (resumed)
```
**Verified passing**, with the whole manoeuvre taking ~1.4 s.

### A10. Cooldown / no oscillation

Hold an obstacle at 15 cm for 7 s and count the manoeuvres:

```
SIM:DIST:15   (hold)   -> manoeuvre starts, gaps of 2581 ms and 2601 ms
```

**Verified passing**: every retry is bounded by `AVOID_COOLDOWN_MS` (2500 ms).
The robot retries periodically against a *permanent* wall, which is correct;
it does not thrash. Note this only applies while still driving forward — a
stopped robot never reverses on its own.

### A11. Operator STOP overrides the manoeuvre

```
CMD:MODE:AUTO
SIM:DIST:15              -> AVOID:REACTING ... AVOID:REVERSING
CMD:STOP                 -> must stay STOPPED; no further reversing/turning
```
**Verified passing.** This is the highest-priority rule in the firmware.

### A12. Invalid reading is not an obstacle

```
SIM:DIST:NONE   -> ULTRASONIC:NO_ECHO, OBSTACLE:CLEAR:NA, DISTANCE_ALERT:OFF
```
**Verified passing.** No echo must never be reported as a detected obstacle,
and never as "clear" either — the whiskers remain the backup.

### A13. Distance blocks a drive command

```
SIM:DIST:20
CMD:FORWARD    -> ERROR:OBSTACLE_BLOCKED     (must be rejected)
SIM:DIST:120
CMD:FORWARD    -> ACK:FORWARD                (allowed again)
```
**Verified passing.**

---

## Stage B — Arduino standalone (USB serial)

Upload `BioMouse_Arduino.ino`, open the serial monitor at **115200 baud**, and
confirm the banner shows `Thermal sensor: READY`.

### B1. Sensor detection
| Test | Expect |
|---|---|
| AMG8833 connected | `EVENT:THERMAL_SENSOR_READY`, `SENSOR:READY` |
| AMG8833 unplugged | `EVENT:THERMAL_SENSOR_MISSING`, `SENSOR:MISSING` |
| Warm hand in front | `TEMP:` climbs, then `HEAT_DETECTED`, then `POSSIBLE_SURVIVOR` |
| Hand to the left | `HEAT:LEFT` |
| Move hand to centre | `HEAT:CENTER` |
| Nothing warm for 30 s | `THERMAL:NONE`, ambient settles to room temperature |

### B2. Motors — wheels off the ground
| Command | Expect |
|---|---|
| `CMD:FORWARD` | `ACK:FORWARD`; **all four** wheels forward |
| `CMD:LEFT` / `CMD:RIGHT` | correct arc |
| `CMD:STOP` | all four stop immediately |
| `CMD:SPEED:30` then `CMD:FORWARD` | visibly slower |
| `CMD:SCAN` | pivots in place |

If one side drives backwards, set `LEFT_MOTORS_REVERSED` or
`RIGHT_MOTORS_REVERSED` in `config.h`. If `CMD:SPEED` has no effect, the ENA/ENB
pins are not PWM pins, or the L298N jumpers were not removed.

### B3. Buzzer
```
CMD:BUZZER:TEST  -> short beep, BUZZER:ON then OFF
CMD:BUZZER:ON    -> repeating pattern until CMD:BUZZER:OFF
```
Confirm commands still respond *during* buzzing — this proves the buzzer is
non-blocking.

### B4. Battery
Send `CMD:SELFTEST` and compare `BATTERY:` against a multimeter. Adjust
`BATTERY_FULL_VOLTS`, `BATTERY_EMPTY_VOLTS` or `BATTERY_DIVIDER_RATIO` if off.

### B5. Whiskers
Touch each whisker. Expect `OBSTACLE:LEFT`, `OBSTACLE_LEFT:1`, and in MANUAL
mode `ROBOT:STOPPED:OBSTACLE`. Hold it against a wall for 5 s and confirm the
robot does **not** reverse repeatedly (that is the cooldown working).

### B6. E-STOP button
Press it. Expect `EVENT:ESTOP_BUTTON`, `ROBOT:STOPPED:ESTOP_BUTTON`, and the
robot staying stopped until released.

### B7. Failsafe — the real test
Stop sending anything for 4 s. Expect `EVENT:FAILSAFE_LINK_LOST` and
`ROBOT:STOPPED:FAILSAFE_LINK_LOST`. Send `CMD:PING` again; expect
`EVENT:FAILSAFE_CLEARED`.

### B8. Thermal navigation
Enable AUTO (`CMD:MODE:AUTO`) with the wheels lifted. Move a warm object:
- left → `MOVE:LEFT`
- centre → `MOVE:FORWARD`
- right → `MOVE:RIGHT`
- remove it → `MOVE:SCAN`
- very close and sustained → `ROBOT:STOPPED:CLOSE_HEAT_ALERT`, buzzer, `ALERT:ON`

### B9. HC-SR04 ultrasonic — measure against a real wall

Put a flat board in front of the robot, measure with a tape, then compare with
`[ULTRA] ... cm` in the serial monitor.

| Real distance | Expected reading | Note |
|---|---|---|
| 10 cm | ~10 cm | |
| 30 cm | ~30 cm | The alert boundary |
| 50 cm | ~50 cm | |
| 100 cm | ~100 cm | |
| > 400 cm | `NO ECHO` / invalid | Outside usable range |

Accuracy is typically within 1–2 cm up to ~3 m. **Measure against a flat
surface square-on** — angled surfaces and soft materials return wrong or no
readings, which is precisely why the whiskers are kept as the backup.

### B10. Ultrasonic no-echo handling
Disconnect ECHO, or point the sensor at a soft blanket.
- `ULTRASONIC:NO_ECHO` only after ~5 consecutive failed readings
- `DISTANCE:-1.0CM`, and the app shows "NO ECHO"
- **Must NOT report `OBSTACLE:DETECTED`** — no echo is invalid, not an obstacle
- The whiskers must still work normally

### B11. Ultrasonic triggers the avoidance manoeuvre

Wheels lifted, MANUAL:
```
CMD:FORWARD              -> ACK:FORWARD, MOVE:FORWARD
board at ~20 cm          -> AVOID:REACTING,  MOVE:STOP
                         -> AVOID:STOPPED,   MOVE:STOP
                         -> AVOID:REVERSING, MOVE:BACKWARD
                         -> AVOID:TURNING,   MOVE:SCAN
                         -> AVOID:RESUMING,  MOVE:STOP
remove the board         -> AVOID:IDLE, MOVE:STOP  (MANUAL never auto-resumes)
CMD:FORWARD              -> drives again
```
In **AUTO** the robot resumes automatically once the path clears.

### B12. Ultrasonic vs whisker precedence
Board 15 cm ahead (beyond whisker reach) → the ultrasonic alone must trigger
`EVENT:OBSTACLE_DETECTED:ULTRASONIC:15CM`.

Move it closer until a whisker touches → `EVENT:OBSTACLE_DETECTED:WHISKER_LEFT`
or `WHISKER_RIGHT`, and confirm the robot turns **away** from that whisker.

### B13. STOP during a manoeuvre — THE IMPORTANT TEST
Start driving forward into a board, and while in `AVOID:REVERSING` send
`CMD:STOP` from the serial monitor.
- Must stop immediately
- Must **not** continue reversing or turning
- Expect `EVENT:OBSTACLE_AVOID_CANCELLED:OPERATOR_STOP`

This proves the manoeuvre never blocks the UART.

---

## Stage C — Arduino + ESP32-CAM (UART)

Wire per `docs/PIN_CONFIGURATION.md`. **ESP32 is 3.3 V logic; never connect a 5 V
Arduino TX straight into the ESP32 RX pin.**

### C1. UART link
Expect `ROBOT:ONLINE:CMDS=0:RX=0:UPTIME=0s` every second. If silent:
- TX/RX crossed?
- common ground connected?
- both at 115200?
- level shifting in place?

### C2. Command relay
Expect the ESP32 to log `[APP->ROBOT] CMD:FORWARD` and the Arduino to answer
`ACK:FORWARD`.

### C3. Telemetry relay
Expect `[ROBOT->APP] EVENT:` lines and steady parsing with no `ERROR:UNKNOWN`.

### C4. Failsafe with the real link
Kill the UART while driving. Both boards must report the link lost within 3 s
and the robot must stop.

---

## Stage D — Phone browser and app (Wi-Fi)

### D1. Access point
Join `BIO-MOUSE-XXXX` (password `biomouse123`) and open **`http://192.168.4.1`**.
The built-in control panel appears with live status and working buttons.

### D2. Each button
Tap every button: FWD, BACK, LEFT, RIGHT, STOP, SCAN, AUTO, MANUAL, buzzer test,
speed slider. Each should move the robot and update the status panel.

### D3. Status values
Confirm all eight required values update live:

| Requirement | Panel element |
|---|---|
| `TEMPERATURE` | Temp |
| `THERMAL_STATUS` | Thermal |
| `OBSTACLE_LEFT` / `OBSTACLE_RIGHT` | Obstacle L/R |
| `BATTERY` | Battery |
| `ROBOT_STATUS` | Robot |
| `BUZZER_STATUS` | Buzzer |
| `CONNECTION_STATUS` | Connection |

### D4. Realistic failures
- Turn off the phone's Wi-Fi → connection becomes `DISCONNECTED` and the robot
  stops within ~3 s.
- Close the browser tab while driving → robot stops.
- Block the whiskers → obstacle banner, robot stops.
- Hold a warm object in front in AUTO mode → alert banner, robot stops, buzzer.

### D5. App integration
```js
import { RobotClient } from './robot-client.js';
const robot = new RobotClient('http://192.168.4.1');
robot.onUpdate(s => { /* render s.temperature, s.thermalStatus, ... */ });
await robot.connect();
```
Verify the `visibilitychange` reconnect in the example wiring, and that
`robot.healthMessage()` drives a banner. See `app/robot-client.js` for a
complete commented example.

---

## Checklist before a field demo

- [ ] Both boards flash cleanly from a cold power cycle
- [ ] `SENSOR:READY` (or the demo accounts for a missing sensor)
- [ ] All four wheels drive the correct way
- [ ] `CMD:SPEED` visibly changes speed
- [ ] STOP is instant from every mode
- [ ] E-STOP latches until released
- [ ] Failsafe stops the robot within 3 s of losing the link
- [ ] Whiskers block driving in both MANUAL and AUTO
- [ ] Thermal guidance steers correctly for LEFT / CENTER / RIGHT
- [ ] Close heat triggers stop + buzzer + alert
- [ ] Battery % matches a multimeter
- [ ] Losing phone Wi-Fi stops the robot
- [ ] The app says "heat detected" / "possible survivor", **not** "survivor found"