# BIO-MOUSE Pin Configuration & Wiring

> **No pin numbers were supplied with the project, so every assignment below is
> a clearly-marked, editable default chosen for a standard Arduino UNO R3 /
> Nano (ATmega328P). Change them in `config.h` to match YOUR wiring before the
> first power-up.**

Both firmware projects keep every pin in one place:
- `firmware/arduino/BioMouse_Arduino/config.h`
- `firmware/esp32cam/BioMouse_ESP32CAM/config.h`

---

## 1. Arduino pin assignments

### FINAL MAP (after adding the HC-SR04)

| Constant | Default | Device | Notes |
|---|---|---|---|
| `PIN_L298N_IN1` | `9` | L298N direction | Left motor A |
| `PIN_L298N_IN2` | **`10`** | L298N direction | **MOVED from 8** (HC-SR04 took D8) |
| `PIN_L298N_IN3` | **`A1`** | L298N direction | **MOVED from 7** (HC-SR04 took D7) |
| `PIN_L298N_IN4` | `6` | L298N direction | Right motor B |
| `PIN_L298N_ENA` | `5` | L298N speed (PWM) | **Must be a PWM pin** |
| `PIN_L298N_ENB` | `3` | L298N speed (PWM) | **Must be a PWM pin** |
| **`PIN_ULTRASONIC_TRIG`** | **`8`** | **HC-SR04** | **NEW — primary front detection** |
| **`PIN_ULTRASONIC_ECHO`** | **`7`** | **HC-SR04** | **NEW — primary front detection** |
| `PIN_WHISKER_LEFT` | `12` | Microswitch | `INPUT_PULLUP`, secondary detection |
| `PIN_WHISKER_RIGHT` | `4` | Microswitch | `INPUT_PULLUP`, secondary detection |
| `PIN_WHISKER_REAR` | `11` | Microswitch (optional) | Set `-1` if not fitted |
| `PIN_BUZZER` | `2` | 5V active buzzer | Pin sinks current |
| `PIN_BATTERY_SENSE` | `A0` | Voltage divider | 10-bit ADC |
| `PIN_I2C_SDA` | `A4` | AMG8833 | Fixed on UNO/Nano |
| `PIN_I2C_SCL` | `A5` | AMG8833 | Fixed on UNO/Nano |
| `PIN_ESTOP_BUTTON` | `13` | E-STOP button | Set `-1` if not fitted |
| `UART` | `D0` / `D1` | USB serial | Shared link to ESP32-CAM |

### ⚠️ PIN CONFLICT THAT WAS FOUND AND RESOLVED

The HC-SR04 was specified for **D8** and **D7**, but both were already in use:

| Pin | Was assigned to | Resolution |
|---|---|---|
| **D8** | `PIN_L298N_IN2` (left motor B) | Moved to **D10** |
| **D7** | `PIN_L298N_IN3` (right motor A) | Moved to **A1** |

**Why moving the L298N pins is safe:** `IN1..IN4` are *direction* inputs. They
do **not** need to be PWM pins, because speed is controlled separately through
`ENA`/`ENB`, which are still on `D5`/`D3` (both genuine PWM pins). `A1` is a
perfectly valid digital pin on the ATmega328P — analog pins are digital-capable.

**No other pin was changed.** The whiskers, buzzer, battery divider, AMG8833,
E-STOP and the UART link are all untouched.

Verified programmatically after the change:

```
PASS - no duplicate GPIO assignments
PIN_L298N_ENA = 5   PWM OK
PIN_L298N_ENB = 3   PWM OK
```

### Choosing different pins
- **`IN1..IN4`** — any 4 free digital pins; PWM not required.
- **`ENA`/`ENB`** — must be hardware PWM. On UNO/Nano: **3, 5, 6, 9, 10, 11**.
- **HC-SR04 TRIG/ECHO** — any 2 free digital pins.
- **Whiskers** — any free digital pin, `INPUT_PULLUP`.
- **Buzzer** — any free digital pin (2 also allows `tone()`).
- **Battery** — any free analog pin.

---

## 2. HC-SR04 ultrasonic (primary front detection)

```
HC-SR04 VCC  -> Arduino 5V
HC-SR04 GND  -> Arduino GND
HC-SR04 TRIG -> Arduino D8
HC-SR04 ECHO -> Arduino D7
```

**No level shifter needed.** The HC-SR04 drives ECHO to 5 V, and a 5 V UNO
recognises 5 V as a valid logic HIGH. (This would *not* be true on a 3.3 V
board, which is one reason this module belongs on the Arduino and not the
ESP32-CAM.)

**Mounting notes**

- Mount at chassis height, angled slightly **downward** so the beam does not
  hit the floor at a distance.
- Keep it **clear of the whiskers** by at least 5 cm, or the robot will
  reverse before the whiskers ever fire.
- The HC-SR04 has a ~2 cm blind zone and loses accuracy past ~400 cm.

---

## 3. L298N wiring

```
Arduino D9  -> IN1        Arduino D5 -> ENA
Arduino D10 -> IN2        Arduino D3 -> ENB
Arduino A1  -> IN3        L298N OUT1/OUT2 -> LEFT motors
Arduino D6  -> IN4        L298N OUT3/OUT4 -> RIGHT motors
Arduino GND -> GND
```

> **Changed for the HC-SR04.** IN2 moved from D8 to **D10**, and IN3 moved from
> D7 to **A1**. Re-check these two wires when rewiring the chassis. ENA/ENB are
> unchanged at D5/D3, so speed control still works.

**Critical L298N notes**

1. **Remove the ENA/ENB jumpers** on the module before connecting `D5`/`D3`.
   With the jumpers on, the module's own pull-ups fight your PWM pins and the
   robot will only run at full speed.
2. **5V regulator jumper:** on most modules, remove the `5V_EN` jumper when
   powering the motors from a separate battery, and feed the Arduino from the
   battery instead. Otherwise the L298N and the Arduino fight over the 5V rail.
3. **Common ground is mandatory.** Battery negative, L298N GND and Arduino GND
   must all be tied together.
4. **Motor direction.** If one side drives backwards, set
   `LEFT_MOTORS_REVERSED` / `RIGHT_MOTORS_REVERSED` in `config.h` rather than
   rewiring. Four DC motors in a 4WD chassis rarely all agree on polarity.

---

## 4. AMG8833 thermal sensor

```
AMG8833 VIN -> 3.3V
AMG8833 GND -> GND
AMG8833 SDA -> A4
AMG8833 SCL -> A5
```

> ### ⚠️ Important corrections to earlier notes in this project
>
> **1. The I2C address is `0x69`, not `0x33`.** The AMG8833 belongs to the
> AMG88xx family and shares that family's address. This is set by
> `AMG8833_I2C_ADDRESS` in `config.h`.
>
> **2. There is no library called "Adafruit AMG8833".** Searching the Arduino
> library index (44,541 libraries) returns only *Adafruit AMG88xx Library*.
> That single driver handles the whole AMG88xx family, including the AMG8833.
>
> **3. The class is `Adafruit_AMG88xx`, not `Adafruit_AMG8833`.**
>
> **4. There is no `readPixel(x, y)` method.** The driver exposes
> `readPixels(float *buf, size)`, which fills a 64-element (8x8) buffer in
> row-major order. `thermal.h` reads the frame once and then indexes it:
>
> ```cpp
> thermalSensor.readPixels(thermalPixels, AMG88xx_PIXEL_ARRAY_SIZE);
> float t = thermalPixels[(row * GRID_COLS) + col];
> ```
>
> Install **Adafruit AMG88xx Library** (which pulls in Adafruit BusIO).

### Verifying the sensor is detected
On boot the banner prints `Thermal sensor: READY`. If it says
`MISSING - CHECK I2C WIRING`, check SDA/SCL, the 3.3V supply, and that the
address in `config.h` is `0x69`.

---

## 5. Whiskers (secondary detection)

One leg to the pin, the other to **GND**. `INPUT_PULLUP` means an untouched
switch reads HIGH and a pressed switch reads LOW.

```
WHISKER_LEFT  (D12) ---- switch ---- GND
WHISKER_RIGHT (D4)  ---- switch ---- GND
```

If your switches are wired the other way round, set
`WHISKER_ACTIVE_LOW = false`.

---

## 6. Active buzzer

```
+5V ---- buzzer (+) ---- buzzer (-) ---- Arduino D2
```

The pin only **sinks** current. If your buzzer draws more than about 15 mA, use
a transistor driver; do not drive a sounder directly from a GPIO pin.

---

## 7. Battery divider

```
battery + ----[ R_TOP = 100k ]----+----[ R_BOTTOM = 27k ]---- GND
                                   |
                                   +----> A0
```

Ratio = 11.0:1, which keeps a 12.6 V pack inside the ADC's 5 V range.
Update `BATTERY_DIVIDER_RATIO` if you use different resistors:

```c
const float BATTERY_DIVIDER_RATIO = (R_TOP + R_BOTTOM) / R_BOTTOM;
```

Keep `R_BOTTOM >= 1 kOhm`, otherwise the ADC reading becomes too noisy.

---

## 8. ESP32-CAM pin assignments

| Constant | Default | Direction | Notes |
|---|---|---|---|
| `PIN_UART_TX` | `13` | ESP32 TX | -> Arduino D1 (RX) |
| `PIN_UART_RX` | `14` | ESP32 RX | <- Arduino D0 (TX) |

### GPIO map already taken on an AI-Thinker ESP32-CAM

| GPIOs | Used by |
|---|---|
| `0, 5, 18, 19, 21, 22, 23, 25, 26, 27, 32, 34, 35, 36, 39` | **Camera** |
| `15` | **Camera RESET** |
| `16` | **Module PSRAM** |
| `6`-`11` | **SPI flash** |
| `2, 4, 12, 13, 14` | **microSD slot** |

So `GPIO13`/`GPIO14` are free **only if you are not using the microSD card**.
If you are, move the UART to another free pair such as `GPIO4` / `GPIO12`.

### UART wiring

```
ESP32-CAM GPIO13 (TX)  -->  Arduino D1 (RX)
ESP32-CAM GPIO14 (RX)  <--  Arduino D0 (TX)
ESP32-CAM GND          ---  Arduino GND     <-- required
```

> **Voltage warning.** The ESP32 is **3.3 V logic**. A 5 V Arduino TX pin
> connected straight to the ESP32 RX pin can damage the ESP32. Use a level
> shifter, or power the Arduino's logic at 3.3 V.
>
> Arduino `D0`/`D1` are the USB serial port, so the USB port **cannot** be
> plugged into a PC at the same time as the ESP32-CAM: both would drive the
> same RX line. Use the Arduino serial monitor and the ESP32-CAM alternately,
> or wire the ESP32-CAM to `SoftwareSerial` pins instead.

---

## 9. Power budget (typical)

| Item | Current |
|---|---|
| 4x DC 6V motors (stall) | up to 4 x 1.2 A |
| ESP32-CAM (streaming) | ~0.25 A (peaks ~0.5 A) |
| Arduino UNO | ~0.05 A |
| AMG8833 | ~0.01 A |

Motor current dwarfs everything else. **Do not power the motors from the
Arduino's 5 V regulator** — use a battery pack with a separate L298N supply,
and add at least 1000 µF of bulk capacitance across the L298N to reduce
brownouts when the motors start.