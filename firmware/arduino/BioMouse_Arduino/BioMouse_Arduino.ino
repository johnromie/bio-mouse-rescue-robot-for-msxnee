/* ============================================================================
 *  BIO-MOUSE RESCUE ROBOT  --  ARDUINO MAIN SKETCH  (v1.0.0)
 * ----------------------------------------------------------------------------
 *  WHAT THIS BOARD IS RESPONSIBLE FOR
 *    This is the safety-critical half of the robot. It owns the motors, the
 *    AMG8833 thermal array, the HC-SR04 ultrasonic, the whisker switches, the
 *    buzzer and the battery monitor. It knows nothing about Wi-Fi or the app.
 *
 *    The ESP32-CAM owns Wi-Fi and the camera. It receives commands from the
 *    mobile app, forwards them down the UART to this board, and forwards this
 *    board's telemetry back up to the app.
 *
 *  MODULE MAP (each in its own header, all included below)
 *    [1] motor.h        Motor control (L298N, 4WD differential drive)
 *    [2] thermal.h      AMG8833 thermal sensing
 *    [3] navigation.h   Thermal-guided navigation
 *    [4] whisker.h      Whisker obstacle detection (secondary/backup)
 *    [5] buzzer.h       5V active buzzer
 *    [6] battery.h      Battery monitoring
 *    [7] uart.h         UART communication (command parser + telemetry)
 *    [8] safety.h       Safety / emergency stop
 *    [9] modes.h        Manual mode
 *    [10] modes.h       Automatic thermal-guided mode
 *    [11] ultrasonic.h  HC-SR04 PRIMARY front obstacle detection
 *         obstacle.h    Obstacle avoidance state machine
 *
 *  REQUIRED LIBRARIES  (Arduino IDE -> Library Manager)
 *    "Adafruit AMG88xx Library"  by Adafruit   <-- provides Adafruit_AMG88xx
 *    "Adafruit BusIO"            by Adafruit   (dependency)
 *
 *    NOTE: there is NO library called "Adafruit AMG8833". The AMG8833 is a
 *    model of the AMG88xx family and is driven by the single AMG88xx driver.
 *    Its I2C address is 0x69 (see config.h), not 0x33.
 *
 *    Verified with arduino-cli 1.1.1 + arduino:avr:uno 1.8.6:
 *      Sketch uses 19678 bytes (61%) of 32256; 1301 bytes (63%) of RAM.
 *
 *  BOARD
 *    Arduino UNO R3 / Nano, or any ATmega328P board.
 *
 *  SAFETY DISCLAIMER
 *    Academic / prototype rescue robot. It assists a search; it does not
 *    replace a trained rescue team. Nothing here claims a person has been
 *    found. It reports that HEAT was detected.
 * ========================================================================= */

#include "state.h"
#include "motor.h"
#include "safety.h"
#include "buzzer.h"
#include "battery.h"
#include "ultrasonic.h"      // [11] HC-SR04 primary front detection
#include "whisker.h"         // [4]  secondary / backup detection
#include "obstacle.h"        //      avoidance state machine
#include "thermal.h"
#include "navigation.h"
#include "modes.h"
#include "uart.h"

/* ==========================================================================
 *  SETUP
 * ========================================================================== */
void setup() {
  Serial.begin(UART_BAUD);

  // ---- Pin directions ----------------------------------------------------
  pinMode(PIN_L298N_IN1, OUTPUT);
  pinMode(PIN_L298N_IN2, OUTPUT);
  pinMode(PIN_L298N_IN3, OUTPUT);
  pinMode(PIN_L298N_IN4, OUTPUT);

  pinMode(PIN_BUZZER, OUTPUT);

  // Whiskers use the internal pull-ups: a microswitch ties the pin to GND
  // when pressed, so no external resistors are needed.
  pinMode(PIN_WHISKER_LEFT,  INPUT_PULLUP);
  pinMode(PIN_WHISKER_RIGHT, INPUT_PULLUP);
  if (PIN_WHISKER_REAR >= 0) pinMode(PIN_WHISKER_REAR, INPUT_PULLUP);

  // HC-SR04 ultrasonic. TRIG is driven by the firmware and must be LOW while
  // idle, otherwise the module fires continuously. ECHO is an input.
  pinMode(PIN_ULTRASONIC_TRIG, OUTPUT);
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
  pinMode(PIN_ULTRASONIC_ECHO, INPUT);

  // Emergency-stop button.
  if (PIN_ESTOP_BUTTON >= 0) pinMode(PIN_ESTOP_BUTTON, INPUT_PULLUP);

  // ENA/ENB must be PWM pins, otherwise the robot only ever runs at full
  // speed and CMD:SPEED has no visible effect.
  analogWrite(PIN_L298N_ENA, 0);
  analogWrite(PIN_L298N_ENB, 0);

  motorsHardStop();          // never boot with the motors energised

  // ---- I2C / thermal sensor ----------------------------------------------
  Wire.begin();
  Wire.setClock(400000);     // the AMG8833 supports I2C Fast Mode
  thermalInit();

  // Prime the battery reading so the first telemetry is already sensible.
  batteryService();

  // Timing baselines. lastCommandRxMs matters: the robot must not trip the
  // failsafe watchdog while the ESP32-CAM is still booting.
  lastCommandRxMs   = nowMs();
  lastTelemetryMs   = nowMs();
  lastHeartbeatMs   = nowMs();
  lastThermalReadMs = nowMs();

  // ---- Startup banner ----------------------------------------------------
  Serial.println();
  Serial.println(F("========================================="));
  Serial.println(F(" BIO-MOUSE RESCUE ROBOT - ARDUINO v1.0.0"));
  Serial.print(F(" UART @ ")); Serial.print(UART_BAUD); Serial.println(F(" baud"));
  Serial.print(F(" Thermal sensor: "));
  Serial.println(sensorReady ? F("READY") : F("MISSING - CHECK I2C WIRING"));
  Serial.print(F(" Battery: "));
  Serial.print((int)(batteryPercent + 0.5f));
  Serial.print(F("% ("));
  Serial.print(batteryVolts, 1);
  Serial.println(F("V)"));
  Serial.print(F(" Speed: ")); Serial.println((int)currentSpeed);

  // Probe the ultrasonic once at boot, so a wiring fault is obvious immediately
  // rather than discovered later in a rescue scenario.
  float probe = readUltrasonicDistance();
  Serial.print(F(" Ultrasonic: "));
  if (probe >= 0.0f) {
    Serial.print(probe, 0);
    Serial.println(F(" cm"));
  } else {
    Serial.println(F("NO ECHO (whiskers are the backup)"));
  }

  Serial.println(F(" Waiting for commands from ESP32-CAM..."));
  Serial.println(F("========================================="));
  Serial.println(F("ROBOT:ONLINE"));
}

/* ==========================================================================
 *  LOOP  --  non-blocking scheduler
 * ==========================================================================
 *  Nothing here uses delay(), so an emergency STOP from the app is honoured on
 *  every single pass, including in the middle of an avoidance manoeuvre.
 *
 *    1. uartService() runs FIRST, so a STOP takes effect before anything else.
 *    2. safetyService() next, so the E-STOP and failsafe are always checked.
 *    3. Sensors refresh.
 *    4. The avoidance state machine advances at most one phase.
 *    5. EXACTLY ONE mode service runs, and is the only other motor driver.
 * ========================================================================== */
void loop() {
  // [7] Commands from the app (highest priority).
  uartService();

  // [8] E-STOP button + failsafe watchdog.
  safetyService();

  // [5] Buzzer pattern, non-blocking.
  buzzerService();

  // [11] HC-SR04: rate-limited, non-blocking distance reading.
  ultrasonicService();

  // [4] Whiskers: debounced secondary/backup detection (reporting only).
  whiskerService();

  // Obstacle avoidance state machine. Owns the motors while a manoeuvre is in
  // flight. Non-blocking, so the UART keeps running throughout.
  avoidService();

  // [6] Battery level.
  batteryService();

  // [2] AMG8833 frame + classification + close-range alert.
  thermalService();

  // [9] / [10] Exactly one of these owns the motors this pass.
  if (robotMode == MODE_AUTO) autoModeService();     // [10]
  else                         manualModeService();   // [9]

  // ---- Periodic telemetry ------------------------------------------------
  if (elapsed(lastTelemetryMs, TELEMETRY_INTERVAL_MS)) {
    lastTelemetryMs = nowMs();
    sendTelemetry();
  }

  // ---- Keep-alive so the ESP32-CAM knows this board is alive -------------
  if (elapsed(lastHeartbeatMs, HEARTBEAT_INTERVAL_MS)) {
    lastHeartbeatMs = nowMs();
    Serial.print(F("ROBOT:ONLINE:CMDS="));
    Serial.print(cmdCount);
    Serial.print(F(":RX="));
    Serial.print(rxLineCount);
    Serial.print(F(":UPTIME="));
    Serial.print(millis() / 1000);
    Serial.println(F("s"));
  }
}

/* ==========================================================================
 *  END OF FILE
 * ========================================================================== */