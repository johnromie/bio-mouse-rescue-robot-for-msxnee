/* ============================================================================
 *  BIO-MOUSE RESCUE ROBOT  --  config.h
 * ----------------------------------------------------------------------------
 *  Every pin number and every tunable threshold in the firmware lives here.
 *  No other file hard-codes a pin or a magic number.
 *
 *  >>> VERIFY EVERY PIN AGAINST YOUR OWN WIRING BEFORE FIRST POWER-UP. <<<
 * ========================================================================== */
#ifndef BIOMOUSE_CONFIG_H
#define BIOMOUSE_CONFIG_H

/* ==========================================================================
 *  PIN CONFIGURATION  --  >>> ALL ASSIGNABLE, CHANGE AS NEEDED <<<
 * ==========================================================================
 *  Defaults match the prototype wiring on an Arduino UNO R3 / Nano (ATmega328P).
 *  A4/A5 are reserved for the thermal sensor I2C bus.
 * ------------------------------------------------------------------------
 *  HOW TO MOVE A PIN
 *    L298N_IN1..IN4  Any 4 free digital pins. PWM is NOT required here,
 *                    because speed is handled by the ENA/ENB pins.
 *    L298N_ENA/ENB    MUST be hardware-PWM pins, otherwise the robot can only
 *                    run at full speed. UNO/Nano PWM pins are 3, 5, 6, 9, 10, 11.
 *                    >>> On the L298N module REMOVE the ENA/ENB jumpers first,
 *                        otherwise the module's own pull-ups fight the pin.
 *    WHISKER_*        Any free digital pin. Used with INPUT_PULLUP.
 *    BUZZER           Any free digital pin (pin 2 also allows tone()).
 *    BATTERY_SENSE    Any free ANALOG pin (A0..A5).
 *    HC-SR04_TRIG/ECHO Any two free digital pins.
 *    E-STOP           Any free digital pin, or -1 if not fitted.
 * ==========================================================================
 *
 *  See docs/PIN_CONFIGURATION.md for the complete wiring map and UART notes.
 * ========================================================================== */

// ---- [1] L298N MOTOR DRIVER : DIRECTION PINS -------------------------------
const uint8_t PIN_L298N_IN1  = 9;    // left  motor A direction   (unchanged)
const uint8_t PIN_L298N_IN2  = 8;    // L298N IN2
const uint8_t PIN_L298N_IN3  = 7;    // L298N IN3
const uint8_t PIN_L298N_IN4  = 6;    // right motor B direction   (unchanged)

// ---- [NEW] HC-SR04 ULTRASONIC : PRIMARY FRONT OBSTACLE DETECTION ----------
// Primary front obstacle sensor. The whiskers remain as the secondary/backup
// detector for debris the ultrasonic cannot see (thin poles, soft fabric).
// Wiring: VCC -> 5V, GND -> GND, TRIG -> D4, ECHO -> D11
const uint8_t PIN_ULTRASONIC_TRIG = 4;
const uint8_t PIN_ULTRASONIC_ECHO = 11;
// The HC-SR04 drives ECHO to 5V. On a 5V UNO that is a valid logic level, so
// no level shifter or divider is required.
const bool ULTRASONIC_ECHO_5V_LOGIC = true;

// ---- [1] L298N : SPEED (PWM) PINS -----------------------------------------
// ENA drives BOTH left motors, ENB drives BOTH right motors.
const uint8_t PIN_L298N_ENA  = 10;   // PWM -> left motor pair   (MUST be PWM)
const uint8_t PIN_L298N_ENB  = 5;    // PWM -> right motor pair  (MUST be PWM)

// ---- [1] WIRING POLARITY --------------------------------------------------
// If a side drives the wrong way, flip only that side here instead of
// rewiring the motors.
const bool LEFT_MOTORS_REVERSED  = false;
const bool RIGHT_MOTORS_REVERSED = false;

// ---- [4] WHISKER / OBSTACLE MICROSWITCHES ---------------------------------
// One leg to the pin, the other to GND. With INPUT_PULLUP a pressed switch
// reads LOW when WHISKER_ACTIVE_LOW is true.
const uint8_t PIN_WHISKER_LEFT   = 2;   // left whisker
const uint8_t PIN_WHISKER_RIGHT  = 3;   // right whisker
const int8_t  PIN_WHISKER_REAR   = -1;  // no rear whisker in this wiring
const bool    WHISKER_ACTIVE_LOW = true;

// ---- [5] ACTIVE BUZZER ----------------------------------------------------
// 5V ACTIVE buzzer: +5V -> buzzer -> GPIO pin (the pin sinks current).
// Never wire a buzzer straight to a GPIO pin without a driver transistor.
const uint8_t PIN_BUZZER           = A3;
const uint16_t BUZZER_TEST_TONE_HZ = 2000;  // reserved for a tone() self-test

// ---- [6] BATTERY MONITORING ----------------------------------------------
const uint8_t PIN_BATTERY_SENSE = A0;
// Divider topology:  battery --[R_TOP]--+--[R_BOTTOM]-- GND
//                                      ^ sense node
// Keep R_BOTTOM >= 1 kOhm. Example R_TOP = 100k, R_BOTTOM = 27k -> 11.0:1.
const float BATTERY_DIVIDER_RATIO = (100000.0f + 27000.0f) / 27000.0f;
const float BATTERY_FULL_VOLTS    = 12.60f;  // 3S Li-ion fully charged
const float BATTERY_EMPTY_VOLTS   = 10.50f;  // 3S Li-ion considered empty
const float BATTERY_LOW_PERCENT   = 20.0f;   // warn below this

// ---- [2] AMG8833 THERMAL SENSOR (I2C) ------------------------------------
// The AMG8833 is part of the AMG88xx family and shares that driver's I2C
// address, 0x69. (It is NOT 0x33, which some older datasheets quote.)
const uint8_t PIN_I2C_SDA        = A4;    // fixed on UNO/Nano
const uint8_t PIN_I2C_SCL        = A5;    // fixed on UNO/Nano
const uint8_t AMG8833_I2C_ADDRESS = 0x69;

// ---- [8] EMERGENCY-STOP BUTTON -------------------------------------------
// Normally-OPEN button from the pin to GND (active LOW). Use -1 if not fitted.
const int8_t PIN_ESTOP_BUTTON = -1;    // no physical E-STOP fitted
const bool    ESTOP_ACTIVE_LOW = true;

// ---- [7] UART LINK TO THE ESP32-CAM ---------------------------------------
// The ESP32-CAM connects to this board's USB serial header (D0 = RX, D1 = TX).
// Debug text shares the same line and is prefixed with "DBG:" so the ESP32-CAM
// can ignore it without confusing the status protocol.
const uint32_t UART_BAUD = 115200;
const bool DEBUG_ENABLED = true;

/* ==========================================================================
 *  DRIVE CHARACTERISTICS
 * ========================================================================== */
const uint8_t  DEFAULT_SPEED   = 55;    // 0..100 until CMD:SPEED arrives
const uint8_t  MAX_SPEED       = 100;
const uint8_t  MIN_SPEED       = 15;    // below this, small DC motors stall
const float   TURN_FACTOR      = 0.65f; // 0 = pivot, 1 = full-speed turn
const uint16_t RAMP_STEP_MS    = 12;    // speed ramp interval
const uint8_t  RAMP_STEP       = 4;     // PWM delta per ramp step

/* ==========================================================================
 *  [2] THERMAL DECISION THRESHOLDS
 * ==========================================================================
 *  "Ambient" is the rolling minimum temperature the sensor has seen. A real
 *  heat source appears as a RISE above ambient, so every thermal decision is
 *  made against ambient rather than against a fixed 25 degrees.
 * ========================================================================== */
const float    HEAT_RISE_THRESHOLD     = 3.0f;  // degC above ambient -> HEAT
const float    SURVIVOR_RISE_THRESHOLD = 6.0f;  // degC -> POSSIBLE_SURVIVOR
const float    CLOSE_RANGE_TEMP        = 32.0f; // degC, very close heat
const float    AMBIENT_TRACK_ALPHA     = 0.02f; // ambient follow rate
const float    MAX_VALID_TEMP          = 80.0f; // reject sensor self-heating
const uint16_t CLOSE_HOLD_MS           = 1500;  // close heat must persist
const uint16_t HEAT_CONFIRM_FRAMES     = 3;     // frames to trust a direction

// Thermal grid geometry (AMG8833 usable 8x8 area).
const uint8_t GRID_COLS = 8;
const uint8_t GRID_ROWS = 8;
const uint8_t LEFT_COL_START   = 0, LEFT_COL_END   = 2;  // cols 0-2 = LEFT
const uint8_t CENTER_COL_START = 3, CENTER_COL_END = 5;  // cols 3-5 = CENTER
const uint8_t RIGHT_COL_START  = 6, RIGHT_COL_END  = 7;  // cols 6-7 = RIGHT

/* ==========================================================================
 *  [4] WHISKER RESPONSE  (secondary / backup obstacle detection)
 * ==========================================================================
 *  The whiskers are no longer the first line of defence: the HC-SR04 detects
 *  obstacles at range, and the whiskers catch what sound reflects off poorly
 *  (thin poles, soft fabric, angled glass). Both feed the same state machine.
 * ========================================================================== */
const uint16_t WHISKER_DEBOUNCE_MS   = 60;   // switch-bounce filter
const uint16_t WHISKER_COOLDOWN_MS  = 900;  // repeat-avoidance window
const uint16_t OBSTACLE_BACKUP_MS    = 450;  // reverse-away duration
const uint8_t  OBSTACLE_BACKUP_SPEED = 45;
const uint8_t  OBSTACLE_TURN_SPEED   = 50;

/* ==========================================================================
 *  [NEW] HC-SR04 ULTRASONIC : TUNING
 * ==========================================================================
 *  Primary front obstacle threshold. At or below this the path is treated as
 *  blocked. 30 cm gives a 4WD chassis room to stop before it hits something.
 * ========================================================================== */
const float    ULTRASONIC_THRESHOLD_CM  = 30.0f;

// Ignore readings the sensor cannot physically produce. The HC-SR04 has a
// blind zone of roughly 2 cm and loses accuracy past about 400 cm, so values
// outside this range are INVALID rather than "no obstacle".
const float    ULTRASONIC_MIN_VALID_CM = 2.0f;
const float    ULTRASONIC_MAX_VALID_CM = 400.0f;

// Speed-of-sound conversion: distance_cm = (echo_us / 2) / 29.034 us-per-cm.
const float    US_PER_CM = 58.0f / 2.0f;

// ECHO timeout. Sound travels to the target and back, so 25 ms covers roughly
// 430 cm. This is what stops the robot freezing when there is no echo at all
// (soft cloth, glass at an angle, or simply nothing in front).
const unsigned long ULTRASONIC_ECHO_TIMEOUT_US = 25000UL;

// 10 microsecond trigger pulse, per the HC-SR04 datasheet.
const unsigned long ULTRASONIC_TRIGGER_PULSE_US = 10UL;

// Pause between readings. The sensor needs ~60 ms to settle after a ping;
// reading faster returns the previous measurement.
const uint16_t ULTRASONIC_INTERVAL_MS = 75;

// Consecutive invalid readings before the ultrasonic is flagged as not
// reporting. One dropped echo is normal and must not be treated as a fault.
const uint8_t  ULTRASONIC_FAIL_COUNT = 5;

/* ==========================================================================
 *  [NEW] OBSTACLE AVOIDANCE : STATE-BASED SEQUENCE
 * ==========================================================================
 *  The manoeuvre is a NON-BLOCKING state machine (see obstacle.h), so the
 *  UART stays responsive to an emergency STOP during every phase:
 *
 *      REACTING -> STOPPED -> REVERSING -> TURNING -> RESUMING
 *
 *  Each phase is deliberately short, so the robot is never committed for long
 *  enough to miss an operator STOP or to drift somewhere unexpected.
 * ========================================================================== */
const uint16_t AVOID_STOP_HOLD_MS    = 250;  // full stop before reversing
const uint16_t AVOID_REVERSE_MS      = 400;  // controlled reverse
const uint16_t AVOID_TURN_MS         = 450;  // turn away from the obstacle
const uint16_t AVOID_RESUME_DELAY_MS = 300;  // re-verify the path is clear

// Cooldown between attempts. Without it, a robot facing a wall would reverse
// and turn over and over.
//
// IT MUST BE LONGER THAN THE WHOLE MANOEUVRE, or the robot can start a second
// manoeuvre immediately after finishing the first -- which is precisely the
// oscillation this design exists to prevent.
//   manoeuvre = AVOID_STOP_HOLD + AVOID_REVERSE + AVOID_TURN + AVOID_RESUME
//             = 250 + 400 + 450 + 300 = 1400 ms
//   cooldown  = 2500 ms  -> 1100 ms of margin
const uint16_t AVOID_COOLDOWN_MS     = 2500;

// Speeds used by the manoeuvre, independent of the operator's speed setting,
// so a very slow setting cannot make avoidance unsafe.
const uint8_t  AVOID_REVERSE_SPEED   = 45;
const uint8_t  AVOID_TURN_SPEED      = 50;

/* ==========================================================================
 *  [5] BUZZER PATTERN TIMINGS
 * ========================================================================== */
const uint16_t BEEP_SHORT_MS = 120;
const uint16_t BEEP_LONG_MS  = 700;
const uint16_t BEEP_GAP_MS   = 120;
const uint16_t ALERT_BEEP_MS = 400;   // repeating while an alert is active

/* ==========================================================================
 *  [7] TELEMETRY INTERVALS
 * ========================================================================== */
const uint16_t TELEMETRY_INTERVAL_MS = 250;   // normal status refresh
const uint16_t HEARTBEAT_INTERVAL_MS = 1000;  // ROBOT:ONLINE keep-alive
const uint16_t THERMAL_INTERVAL_MS   = 100;   // AMG8833 read cadence

/* ==========================================================================
 *  [8] FAILSAFE
 * ==========================================================================
 *  If NO valid command arrives from the ESP32-CAM within this window the
 *  robot stops. This is what makes the robot safe when Wi-Fi drops.
 * ========================================================================== */
const uint16_t FAILSAFE_TIMEOUT_MS = 3000;

#endif // BIOMOUSE_CONFIG_H
