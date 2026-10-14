# BIO-MOUSE Testing Procedure

## Using VS Code and PlatformIO

This project has two PlatformIO environments: `uno` and `esp32cam`. Upload and
test one board at a time first. PlatformIO usually detects the upload COM port
automatically. To check it, open **PlatformIO Home → Devices** after plugging
in the board. If no port appears, check Windows **Device Manager → Ports (COM &
LPT)**, try a USB data cable/another USB port, or install the USB-to-serial
driver required by that board or adapter.

1. Open this project folder in VS Code and click the PlatformIO alien icon.
2. Under **PROJECT TASKS**, expand `uno` or `esp32cam`, then **General**.
3. Click **Build** to compile, or **Upload** to compile and flash the connected
   board. If asked, select the COM port that appeared when that board was
   plugged in.
4. To see serial output, expand **Monitor** under the matching environment.
   Set it to **115200 baud**. Stop the monitor with **Ctrl+C** before uploading
   again because it keeps the port open.

Optional PlatformIO Core CLI commands from the project root (replace `COM5` or
`COM6` with the port detected for the connected board):

```powershell
pio run -e uno
pio run -e uno -t upload --upload-port COM5
pio run -e esp32cam
pio run -e esp32cam -t upload --upload-port COM6
pio device monitor -p COM5 -b 115200
```

These terminal commands do the same build/upload/monitor actions as the
PlatformIO task buttons; they are alternatives, not extra setup steps.

### Before applying power

- Compare every connection with `docs/PIN_CONFIGURATION.md` and the reviewed
  prototype wiring guide. Follow actual board pin labels, not wire colors.
- Keep the wheels off the ground for all first motor tests.
- Check battery polarity and regulator output with a multimeter before
  connecting the ESP32-CAM. Keep battery/motor voltage, UNO supply and regulated
  5 V positive rails separate as shown in the wiring guide. Never feed 9 V into
  a 5 V pin or connect the battery directly to A0.
- Join grounds where the wiring guide calls for common GND. Never connect UNO
  D1/TX (5 V) directly to ESP32-CAM GPIO14/RX (3.3 V); use the specified level
  shifter or divider.
- Do not use the UNO USB serial connection and ESP32 UART on D0/D1 at once.
  Disconnect both UART signal wires while uploading the UNO, then reconnect
  them with power off. Keep common ground for normal system operation.

If Windows shows a device with a warning symbol but no COM port, the board may
need its USB-to-serial driver (often CH340 or CP210x, depending on the board).

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

This tests the UNO with no ESP32 UART wires attached.

1. Power off and unplug the ESP32-CAM UART signal wires from UNO D0 and D1.
   Connect the UNO to the PC by USB.
2. In PlatformIO click **PROJECT TASKS → uno → General → Upload**. Select the
   UNO COM port if prompted.
3. Open **PROJECT TASKS → uno → General → Monitor** at 115200 baud. Set the
   monitor line ending to **Newline**.
4. Confirm the startup banner and `SENSOR:READY` if the AMG8833 is connected.

Send each command in the monitor input box and press Enter. Commands are
newline-terminated. Keep the wheels lifted, use low speed, and send `CMD:STOP`
immediately after each brief movement.

```text
CMD:MODE:MANUAL
CMD:SPEED:30
CMD:FORWARD
CMD:STOP
CMD:LEFT
CMD:STOP
CMD:RIGHT
CMD:STOP
CMD:BACKWARD
CMD:STOP
CMD:SCAN
CMD:STOP
```

Other standalone checks:

```text
CMD:SELFTEST
CMD:BUZZER:TEST
CMD:BUZZER:ON
CMD:BUZZER:OFF
CMD:MODE:AUTO
CMD:STOP
CMD:MODE:MANUAL
```

Expect `ACK:` responses and matching movement/status lines. `CMD:SELFTEST`
reports sensor and battery state. Battery readings are only meaningful when
the A0 divider and firmware thresholds match the actual battery. The reviewed
prototype wiring guide says to leave its divider disconnected for the shown
2S pack until the 3S firmware thresholds are corrected. Test AUTO only with
the wheels lifted and the thermal sensor working.

The UNO stops when it receives no valid command for about 3 seconds. During
USB-only tests, send `CMD:PING` periodically if you need to keep the watchdog
fed. In normal operation the ESP32-CAM sends those pings automatically.

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
For the reviewed prototype's 7.4 V (2S) pack, **skip battery percentage
validation and leave the A0 divider disconnected** until firmware thresholds
are changed from the current 3S assumptions. Never connect the battery
directly to A0. Once the divider and thresholds match the pack, send
`CMD:SELFTEST` and compare its `BATTERY:` value with a multimeter.

### B5. Whiskers
Touch each whisker. Expect `OBSTACLE:LEFT`, `OBSTACLE_LEFT:1`, and in MANUAL
mode `ROBOT:STOPPED:OBSTACLE`. Hold it against a wall for 5 s and confirm the
robot does **not** reverse repeatedly (that is the cooldown working).

### B6. E-STOP button
The current prototype has no physical E-STOP fitted and firmware sets
`PIN_ESTOP_BUTTON = -1`; skip this test unless you install and configure one.

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

This stage checks the real UART link. Upload both boards separately first.

1. Power off and disconnect the UNO USB cable and ESP32-CAM programmer cable.
2. Connect crosswise per `docs/PIN_CONFIGURATION.md`: ESP32-CAM GPIO13 TX →
   UNO D0/RX; UNO D1/TX → level shifter/divider → ESP32-CAM GPIO14 RX. Connect
   ESP32-CAM GND to common GND. Leave microSD unused because GPIO13/14 are used
   by this UART setup.
3. Power both boards from the reviewed supplies. Do not drive the motors yet.
4. To view ESP32 logs, reconnect its USB/programmer serial and open the
   `esp32cam` Monitor at 115200. This USB serial is UART0 debug; the Arduino
   link uses ESP32 UART2 on GPIO13/14.

For a motor/command relay check, leave the wheels lifted and send commands from
the browser panel in Stage D. The ESP32 monitor should show forwarded commands
and Arduino replies. Do not use the UNO USB monitor while D0/D1 are connected
to the ESP32.

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

### D1. Access point and manual browser test

1. Leave both boards connected and powered as in Stage C. Keep the wheels
   lifted for movement checks.
2. In ESP32-CAM Monitor, wait for the Wi-Fi SSID. It is `BIO-MOUSE-XXXX` with
   a board-specific suffix; password is `biomouse123`.
3. Connect the phone to that Wi-Fi. If it says there is no internet, stay
   connected to the robot network.
4. Open **http://192.168.4.1** in the phone browser. Confirm the control panel
   loads and robot status says online before testing movement.
5. Tap **STOP** first. Select **MANUAL**, set low speed, briefly test each
   direction and tap **STOP** after each. Test **SCAN**, buzzer and video; end
   with STOP.

For an ESP32-CAM-only test, upload and boot it without the Arduino UART link.
Connect the phone to its Wi-Fi and open the same address to check the panel and
camera stream. Robot status and drive commands will not work without the UNO.

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
- [ ] If a physical E-STOP is installed and configured, it latches until released
- [ ] Failsafe stops the robot within 3 s of losing the link
- [ ] Whiskers block driving in both MANUAL and AUTO
- [ ] Thermal guidance steers correctly for LEFT / CENTER / RIGHT
- [ ] Close heat triggers stop + buzzer + alert
- [ ] Battery % matches a multimeter
- [ ] Losing phone Wi-Fi stops the robot
- [ ] The app says "heat detected" / "possible survivor", **not** "survivor found"
