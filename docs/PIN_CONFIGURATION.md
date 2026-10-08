# BIO-MOUSE Pin Configuration & Wiring

The Arduino pin assignments below match the requested prototype wiring and
are defined in `firmware/arduino/BioMouse_Arduino/config.h`.

## Arduino UNO / Nano pin map

### Sensors and their pins

| Sensor / input | Signal | Arduino pin | Power / connection |
|---|---|---:|---|
| AMG8833 thermal array | SDA | A4 | I2C data |
| AMG8833 thermal array | SCL | A5 | I2C clock |
| HC-SR04 ultrasonic | TRIG | D4 | Sensor trigger |
| HC-SR04 ultrasonic | ECHO | D11 | Echo output |
| Left whisker switch | signal | D2 | Other switch contact to GND |
| Right whisker switch | signal | D3 | Other switch contact to GND |
| Battery voltage monitor | sense | A0 | Through resistor divider only |

The whiskers use `INPUT_PULLUP`; an activated switch pulls its pin to GND.
There is no physical E-STOP button in this prototype, so its input is disabled
in firmware. The rear whisker is also not fitted and is disabled.

Wiring picture: [Whisker microswitch wiring diagram](WHISKER_MICROSWITCH_WIRING.svg).

Full prototype connection map: [Full system wiring diagram](FULL_SYSTEM_WIRING.svg).

Component reference image from the prototype: [Original wiring image](docsprototype-wiring-original.png).
For the reviewed power and signal connections, use the [Prototype wiring guide](PROTOTYPE_WIRING_REVIEW.svg); the colored crossings in the original image are hard to trace and are not the authoritative pin map.

### Other connections

| Device | Signal | Arduino pin |
|---|---|---:|
| Active buzzer | signal | A3 |
| L298N | ENA (PWM) | D10 |
| L298N | IN1 | D9 |
| L298N | IN2 | D8 |
| L298N | IN3 | D7 |
| L298N | IN4 | D6 |
| L298N | ENB (PWM) | D5 |
| Battery divider | sense | A0 |
| Physical E-STOP | button | A2 |
| Rear whisker | not fitted | disabled |

`ENA` D10 and `ENB` D5 are both UNO PWM pins. Remove the L298N ENA/ENB
jumpers before connecting these pins so motor speed control works.

### Sensor and switch wiring

```text
AMG8833 SDA -> A4       HC-SR04 TRIG -> D4
AMG8833 SCL -> A5       HC-SR04 ECHO -> D11
Buzzer signal -> A3     Left whisker -> D2 -> switch -> GND
                        Right whisker -> D3 -> switch -> GND
```

The whisker inputs use `INPUT_PULLUP`: connect each switch between its pin and
GND. The buzzer output should drive a transistor if the buzzer draws more than
about 15 mA. The battery sense input requires the configured resistor divider;
never connect the battery directly to A0.

### L298N wiring

```text
Arduino D9  -> IN1       Arduino D10 -> ENA
Arduino D8  -> IN2       Arduino D5  -> ENB
Arduino D7  -> IN3       L298N OUT1/OUT2 -> left motors
Arduino D6  -> IN4       L298N OUT3/OUT4 -> right motors
Arduino GND ------------ L298N GND and battery negative
```

Power the motors from the battery through the L298N, not from the Arduino 5 V
regulator. Join battery negative, L298N GND, Arduino GND, and ESP32-CAM GND.
Lift the wheels for the first motor test.

## ESP32-CAM to Arduino serial link

The firmware currently uses ESP32-CAM UART2 and the Arduino hardware UART on
D0/D1. Its configured pins are ESP32-CAM GPIO13 (TX) and GPIO14 (RX), connected
crosswise to Arduino D0 (RX) and D1 (TX). The requested GPIO12/13-to-D12/13
connection is not applied yet because it conflicts with the current UART
implementation and the Arduino D12 assignment, and GPIO12 is an ESP32 boot
strapping pin. Confirm the intended link before wiring or changing that code.

ESP32-CAM is 3.3 V logic. Never connect a 5 V Arduino TX signal directly to an
ESP32-CAM RX pin; use a level shifter or suitable divider. Keep microSD unused
when UART pins overlap its GPIO assignments.

## Power and thermal sensor notes

- AMG8833 I2C address is `0x69`; install **Adafruit AMG88xx Library**.
- Verify the board's supply requirements before powering the sensor module.
- Use a suitable external motor battery and common ground; add bulk capacitance
  across the motor supply to reduce brownouts.

See `docs/TESTING.md` for staged startup and motor tests.
