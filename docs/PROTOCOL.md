# BIO-MOUSE Communication Protocol v1.0.0

The contract between the **mobile app**, the **ESP32-CAM**, and the **Arduino**.

```
MOBILE APP  --HTTP/Wi-Fi-->  ESP32-CAM  --UART-->  ARDUINO  -->  L298N  -->  MOTORS
   (JSON)                     (relays)          (decides)     (drives)
                              <--UART--          <--status--
```

---

## 1. Layer 1: Mobile app to ESP32-CAM (HTTP)

Base URL in default access-point mode: **`http://192.168.4.1`**

| Method | Path | Purpose |
|---|---|---|
| `GET`  | `/` | Built-in control panel (test from any phone browser) |
| `GET`  | `/api/status` | Full status snapshot as JSON |
| `GET`  | `/api/cmd?c=CMD&v=VALUE` | Send a command (easy to test in a browser) |
| `POST` | `/api/cmd` | Same, with a JSON body (what the app should use) |
| `GET`  | `/api/camera` | Live MJPEG video stream |
| `GET`  | `/api/snapshot` | Single JPEG still frame |
| `GET`  | `/api/ping` | Connectivity check |

### Sending a command

GET form:
```
GET /api/cmd?c=FORWARD
GET /api/cmd?c=SPEED&v=60
GET /api/cmd?c=MODE&v=AUTO
```

POST form:
```json
POST /api/cmd
Content-Type: application/json

{ "command": "SPEED", "value": 60 }
```

Both answer with:
```json
{ "ok": true, "sent": "CMD:SPEED:60" }
```
or, for an unknown command:
```json
{ "ok": false, "error": "unknown command" }
```

### `GET /api/status` response

```json
{
  "robotStatus": "ONLINE",
  "connection": "CONNECTED",
  "robotOnline": true,
  "linkLost": false,
  "mode": "MANUAL",
  "temperature": 36.4,
  "ambient": 28.1,
  "thermalStatus": "POSSIBLE_SURVIVOR",
  "heatDirection": "LEFT",
  "obstacle": "DETECTED",
  "obstacleLeft": false,
  "obstacleRight": false,
  "obstacleRear": false,
  "distanceCm": 25.4,
  "distanceThreshold": 30,
  "distanceAlert": true,
  "distanceValid": true,
  "ultrasonic": "READY",
  "avoidState": "TURNING",
  "battery": 78,
  "buzzer": "OFF",
  "move": "STOP",
  "alert": false,
  "sensor": "READY",
  "speed": 55,
  "lastStopReason": "",
  "commandsReceived": 42,
  "uptimeMs": 512340,
  "clientCount": 1,
  "wifiAddress": "192.168.4.1"
}
```

### Required status value mapping

| Status value required | JSON key | Example |
|---|---|---|
| `TEMPERATURE` | `temperature` | `36.4` |
| `THERMAL_STATUS` | `thermalStatus` | `"POSSIBLE_SURVIVOR"` |
| `OBSTACLE_LEFT` | `obstacleLeft` | `false` |
| `OBSTACLE_RIGHT` | `obstacleRight` | `true` |
| **`FRONT DISTANCE`** | `distanceCm` + `distanceValid` | `25.4` / `true` |
| **`DISTANCE ALERT`** | `distanceAlert` | `true` |
| **`ULTRASONIC`** | `ultrasonic` | `"NO_ECHO"` |
| **`AVOID PHASE`** | `avoidState` | `"TURNING"` |
| `BATTERY` | `battery` | `78` |
| `ROBOT_STATUS` | `robotStatus`, `robotOnline` | `"ONLINE"`, `true` |
| `BUZZER_STATUS` | `buzzer` | `"ON"` |
| `CONNECTION_STATUS` | `connection` | `"CONNECTED"` |

**Value vocabularies**

- `robotStatus`: `ONLINE` | `OFFLINE` | `ALERT` | `FAILSAFE`
- `connection`: `CONNECTED` | `DISCONNECTED` | `UNSTABLE` | `NO_WIFI`
- `thermalStatus`: `NONE` | `HEAT_DETECTED` | `POSSIBLE_SURVIVOR`
- `heatDirection`: `LEFT` | `CENTER` | `RIGHT` | `NONE`
- `obstacle`: `CLEAR` | `LEFT` | `RIGHT` | `BOTH` | `REAR` | `DETECTED`
- `mode`: `MANUAL` | `AUTO`
- `ultrasonic`: `READY` | `NO_ECHO`
- `avoidState`: `IDLE` | `REACTING` | `STOPPED` | `REVERSING` | `TURNING` | `RESUMING`

> ⚠️ **`distanceCm = -1` means NO ECHO, not "0 cm".** Always check
> `distanceValid` (or test for a negative value) before displaying a number.
> Showing 0 cm would read as "the robot is touching something", which is worse
> than showing "no reading". The helper `robot.distanceText` already does
> this for you.

---

## 2. Layer 2: ESP32-CAM to Arduino (UART)

- **115200 baud, 8N1**, one command per line, `\n` terminated.
- UART2. Default pins: ESP32-CAM `GPIO13` (TX) to Arduino `D1` (RX), and
  `GPIO14` (RX) from Arduino `D0` (TX). See `config.h` in both projects.
- `CR` characters are ignored, so `\r\n` also works.

### Commands (ESP32-CAM to Arduino)

| Command | Meaning |
|---|---|
| `CMD:FORWARD` | Drive forward |
| `CMD:BACKWARD` | Reverse |
| `CMD:LEFT` | Arc left |
| `CMD:RIGHT` | Arc right |
| `CMD:STOP` | **Stop immediately** (works in both modes) |
| `CMD:SCAN` | Pivot in place to sweep the thermal array |
| `CMD:SPEED:<0-100>` | Set speed, e.g. `CMD:SPEED:60` |
| `CMD:MODE:MANUAL` | Operator drives |
| `CMD:MODE:AUTO` | Firmware drives from the AMG8833 |
| `CMD:BUZZER:ON` / `CMD:BUZZER:OFF` | Buzzer control |
| `CMD:BUZZER:TEST` | Short beep, used by self-test |
| `CMD:BUZZER:ALERT` | Repeating alert pattern |
| `CMD:ALERT:RESET` | Clear a thermal alert |
| `CMD:SELFTEST` | Report sensor and battery state |
| `CMD:PING` | Keep-alive; **also resets the failsafe watchdog** |

### Status (Arduino to ESP32-CAM)

Telemetry is sent every 250 ms, with a heartbeat every 1000 ms.

| Line | Example | Meaning |
|---|---|---|
| `ROBOT:ONLINE:CMDS=n:RX=n:UPTIME=ns` | `ROBOT:ONLINE:CMDS=42:RX=44:UPTIME=512s` | Heartbeat |
| `ROBOT:STOPPED:<reason>` | `ROBOT:STOPPED:OBSTACLE` | Stopped, with cause |
| `TEMP:<celsius>` | `TEMP:36.4` | Peak temperature in frame |
| `AMBIENT:<celsius>` | `AMBIENT:28.1` | Rolling ambient reference |
| `THERMAL:<status>` | `THERMAL:POSSIBLE_SURVIVOR` | Thermal classification |
| `HEAT:<sector>` | `HEAT:LEFT` | Strongest-heat sector |
| `OBSTACLE:<side>[:<dist>]` | `OBSTACLE:DETECTED:25` | `CLEAR`/`LEFT`/`RIGHT`/`BOTH`/`REAR`/`DETECTED`, plus distance |
| `OBSTACLE_LEFT:<0\|1>` | `OBSTACLE_LEFT:0` | Left whisker |
| `OBSTACLE_RIGHT:<0\|1>` | `OBSTACLE_RIGHT:1` | Right whisker |
| `OBSTACLE_REAR:<0\|1>` | `OBSTACLE_REAR:0` | Rear whisker |
| **`DISTANCE:<cm>CM`** | `DISTANCE:25.4CM` | **NEW** front distance; `-1.0CM` = no valid echo |
| **`DISTANCE_THRESHOLD:<cm>`** | `DISTANCE_THRESHOLD:30` | **NEW** configured alert limit |
| **`DISTANCE_ALERT:<ON\|OFF>`** | `DISTANCE_ALERT:ON` | **NEW** true when distance ≤ threshold |
| **`ULTRASONIC:<READY\|NO_ECHO>`** | `ULTRASONIC:READY` | **NEW** sensor health |
| **`AVOID:<phase>`** | `AVOID:TURNING` | **NEW** avoidance phase |
| `BATTERY:<percent>` | `BATTERY:78` | Battery estimate |
| `BUZZER:<ON\|OFF>` | `BUZZER:OFF` | Buzzer state |
| `MODE:<mode>` | `MODE:MANUAL` | Active mode |
| `SPEED:<0-100>` | `SPEED:60` | Speed setting |
| `MOVE:<command>` | `MOVE:FORWARD` | Last movement command |
| `ALERT:<ON\|OFF>` | `ALERT:OFF` | Thermal alert active |
| `CONN:<OK\|LOST>` | `CONN:OK` | Arduino-side link health |
| `SENSOR:<READY\|MISSING>` | `SENSOR:READY` | AMG8833 status |
| `ACK:<command>` | `ACK:FORWARD` | Command accepted |
| `ERROR:<reason>` | `ERROR:MODE_IS_AUTO` | Command refused |
| `EVENT:<name>` | `EVENT:WHISKER_HIT` | Notable event |
| `DBG:<text>` | `DBG:SPEED SET` | Debug text; safe to ignore |

### Rejections the app must handle

| Error | Cause | App should |
|---|---|---|
| `ERROR:MODE_IS_AUTO` | Drive sent while in AUTO | Switch to MANUAL, or disable the button |
| `ERROR:OBSTACLE_BLOCKED` | Whisker is triggered | Show the obstacle banner |
| `ERROR:UNKNOWN:<cmd>` | Unrecognised command | A firmware/app version mismatch |
| `ERROR:LINE_TOO_LONG` | Over-long UART line | Should never happen; log it |

### Events

`EVENT:SELFTEST_START`, `EVENT:THERMAL_SENSOR_READY`,
`EVENT:THERMAL_SENSOR_MISSING`, `EVENT:WHISKER_HIT`,
`EVENT:OBSTACLE_AVOID`, `EVENT:ALERT_HEAT_DETECTED`, `EVENT:ALERT_CLEARED`,
`EVENT:BATTERY_LOW`, `EVENT:ESTOP_BUTTON`, `EVENT:FAILSAFE_LINK_LOST`,
`EVENT:FAILSAFE_CLEARED`

---

## 3. Safety rules baked into the protocol

1. **STOP always wins.** `CMD:STOP` is honoured in MANUAL and AUTO alike. In
   AUTO it also latches an override, suspending autonomy until cleared.
2. **Only one driver at a time.** Drive commands are rejected with
   `ERROR:MODE_IS_AUTO` while the firmware owns the motors.
3. **Never drive into an obstacle.** Drive commands are rejected with
   `ERROR:OBSTACLE_BLOCKED` when a whisker is triggered.
4. **Fail-safe link.** The Arduino stops itself if nothing arrives for
   `FAILSAFE_TIMEOUT_MS` (3000 ms). The ESP32-CAM pings every 500 ms, so if
   Wi-Fi, the phone or the ESP32 dies, the robot stops.
5. **No operator, no driving.** If the last HTTP client disconnects while in
   MANUAL, the ESP32-CAM commands a STOP.
6. **Whitelist, not passthrough.** The ESP32-CAM only forwards commands from a
   known list; nothing arbitrary from the network reaches the motors.

---

## 4. Language policy for thermal readings

The firmware **never** claims a person has been found.

| Term | Meaning |
|---|---|
| `NONE` | No significant heat above ambient |
| `HEAT_DETECTED` | A temperature rise of at least 3 °C above ambient |
| `POSSIBLE_SURVIVOR` | A rise of at least 6 °C above ambient |

An engine, an animal, a fire or a sunlit wall all produce the same readings.
The app should present these as **indications of heat**, never as a confirmed
detection, and should recommend that a trained rescue team confirms it.

---

## 5. Worked example

Operator taps FORWARD, then a whisker hits a wall:

```
app   -> POST /api/cmd {"command":"FORWARD"}
esp   -> CMD:FORWARD
ard   -> ACK:FORWARD
ard   -> MOVE:FORWARD
esp   -> {"robotStatus":"ONLINE","move":"FORWARD", ...}
app   -> renders "FORWARD"

                       (whisker struck)
ard   -> OBSTACLE:LEFT / OBSTACLE_LEFT:1
ard   -> ROBOT:STOPPED:OBSTACLE
ard   -> MOVE:STOP
esp   -> {"obstacleLeft":true,"robotStatus":"ONLINE", ...}
app   -> shows red banner "Obstacle detected (LEFT)"
```