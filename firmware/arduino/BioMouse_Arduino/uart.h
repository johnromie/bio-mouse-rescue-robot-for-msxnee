/* ============================================================================
 *  MODULE [7] : UART COMMUNICATION
 * ----------------------------------------------------------------------------
 *  The Arduino side of the command protocol. See docs/PROTOCOL.md for the
 *  full specification.
 *
 *  IN  (ESP32-CAM -> Arduino), one command per line, "\n" terminated:
 *    CMD:FORWARD | CMD:BACKWARD | CMD:LEFT | CMD:RIGHT | CMD:STOP | CMD:SCAN
 *    CMD:SPEED:<0-100>
 *    CMD:MODE:MANUAL | CMD:MODE:AUTO
 *    CMD:BUZZER:ON | CMD:BUZZER:OFF | CMD:BUZZER:TEST | CMD:BUZZER:ALERT
 *    CMD:ALERT:RESET
 *    CMD:PING                (keep-alive; also resets the failsafe watchdog)
 *    CMD:SELFTEST
 *
 *  OUT (Arduino -> ESP32-CAM), one key:value per line:
 *    ROBOT:ONLINE:... / ROBOT:STOPPED:<reason> / EVENT:<...>
 *    MODE:MANUAL|AUTO / SPEED:<n> / MOVE:<name> / CONN:OK|LOST
 *    TEMP:<c> / AMBIENT:<c> / THERMAL:<status> / HEAT:<LEFT|CENTER|RIGHT>
 *    OBSTACLE:LEFT|RIGHT|BOTH|REAR|CLEAR
 *    OBSTACLE_LEFT:<0|1> / OBSTACLE_RIGHT:<0|1> / OBSTACLE_REAR:<0|1>
 *    BATTERY:<percent> / BUZZER:ON|OFF / ALERT:ON|OFF / SENSOR:READY|MISSING
 *    ACK:<command> / ERROR:<reason>
 *
 *  DEBUG LINES are prefixed "DBG:" and are ignored by the ESP32-CAM parser.
 *
 *  NOTE: this UART is also the debug console. Opening the Arduino serial
 *  monitor is fine for bench testing, but a PC cannot be connected at the
 *  same time as the ESP32-CAM, since both would drive the same RX line.
 * ========================================================================== */
#ifndef BIOMOUSE_UART_H
#define BIOMOUSE_UART_H

#include "state.h"
#include "motor.h"
#include "safety.h"
#include "buzzer.h"
#include "thermal.h"
#include "ultrasonic.h"
#include "obstacle.h"

// Single entry point for every app command. All conflict resolution is
// concentrated here, so no other function can bypass the safety rules.
inline void handleCommandLine(char *line) {
  cmdCount++;
  lastCommandRxMs = nowMs();            // ANY valid traffic resets the watchdog

  if (failsafeTriggered) {
    failsafeTriggered = false;
    Serial.println(F("EVENT:FAILSAFE_CLEARED"));
    dbg(F("link restored, failsafe cleared"));
  }

  dbg(line);

  // ---- PING (keep-alive) --------------------------------------------------
  if (strcmp(line, "CMD:PING") == 0) { Serial.println(F("ACK:PING")); return; }

  // ---- MODE ---------------------------------------------------------------
  if (strcmp(line, "CMD:MODE:MANUAL") == 0) {
    robotMode = MODE_MANUAL;
    motorsHardStop();
    activeCommand = CMD_STOP;
    manualOverrideLatch = false;
    Serial.println(F("MODE:MANUAL"));
    Serial.println(F("ACK:MODE:MANUAL"));
    return;
  }
  if (strcmp(line, "CMD:MODE:AUTO") == 0) {
    robotMode = MODE_AUTO;
    manualOverrideLatch = false;
    Serial.println(F("MODE:AUTO"));
    Serial.println(F("ACK:MODE:AUTO"));
    return;
  }

  // ---- SPEED --------------------------------------------------------------
  if (strncmp(line, "CMD:SPEED:", 10) == 0) {
    int v = constrain(atoi(line + 10), 0, MAX_SPEED);
    currentSpeed = (uint8_t)v;
    Serial.print(F("SPEED:")); Serial.println(currentSpeed);
    Serial.println(F("ACK:SPEED"));
    return;
  }

  // ---- BUZZER -------------------------------------------------------------
  if (strcmp(line, "CMD:BUZZER:ON") == 0) {
    buzzerStartRepeating(BEEP_SHORT_MS, BEEP_SHORT_MS * 4);
    Serial.println(F("BUZZER:ON"));
    Serial.println(F("ACK:BUZZER:ON"));
    return;
  }
  if (strcmp(line, "CMD:BUZZER:OFF") == 0) {
    buzzerOff();
    Serial.println(F("ACK:BUZZER:OFF"));
    return;
  }
  if (strcmp(line, "CMD:BUZZER:TEST") == 0) {
    buzzerBeepOnce(BEEP_SHORT_MS);
    Serial.println(F("ACK:BUZZER:TEST"));
    return;
  }
  if (strcmp(line, "CMD:BUZZER:ALERT") == 0) {
    buzzerStartRepeating(ALERT_BEEP_MS, ALERT_BEEP_MS * 2);
    Serial.println(F("ACK:BUZZER:ALERT"));
    return;
  }

  // ---- ALERT --------------------------------------------------------------
  if (strcmp(line, "CMD:ALERT:RESET") == 0) {
    clearThermalAlert();
    manualOverrideLatch = false;
    Serial.println(F("ACK:ALERT:RESET"));
    return;
  }

  // ---- SELF TEST ----------------------------------------------------------
  if (strcmp(line, "CMD:SELFTEST") == 0) {
    Serial.println(F("EVENT:SELFTEST_START"));
    Serial.print(F("SENSOR:"));  Serial.println(sensorReady ? F("READY") : F("MISSING"));
    Serial.print(F("BATTERY:")); Serial.println((int)(batteryPercent + 0.5f));
    Serial.println(F("ACK:SELFTEST"));
    return;
  }

// ---- STOP : highest priority, valid in BOTH modes -----------------------
  if (strcmp(line, "CMD:STOP") == 0) {
    // An emergency STOP overrides everything, including any avoidance
    // manoeuvre that is mid-flight. This is the single most important rule in
    // the firmware: nothing the robot decides for itself outranks the operator.
    if (avoidState != AVOID_IDLE) avoidCancel("OPERATOR_STOP");
    motorsHardStop();
    activeCommand = CMD_STOP;
    // In AUTO, an app STOP must also suspend autonomy until cleared.
    manualOverrideLatch = (robotMode == MODE_AUTO);
    Serial.println(F("ROBOT:STOPPED:CMD"));
    Serial.println(F("ACK:STOP"));
    return;
  }

  // ---- DRIVE COMMANDS -----------------------------------------------------
  bool isDrive = (strcmp(line, "CMD:FORWARD")  == 0) ||
                 (strcmp(line, "CMD:BACKWARD") == 0) ||
                 (strcmp(line, "CMD:LEFT")     == 0) ||
                 (strcmp(line, "CMD:RIGHT")    == 0);

  if (isDrive) {
    // Conflict rule 1: in AUTO the firmware owns the motors, so operator drive
    // commands are rejected rather than silently fighting the algorithm.
    if (robotMode == MODE_AUTO) {
      Serial.println(F("ERROR:MODE_IS_AUTO"));
      dbg(F("drive rejected - AUTO owns the motors"));
      return;
    }
    // Conflict rule 2: never drive into an obstacle. This now covers the
    // ultrasonic as well as the whiskers.
    if (obstacleDetected()) {
      Serial.println(F("ERROR:OBSTACLE_BLOCKED"));
      dbg(F("drive rejected - obstacle present"));
      return;
    }
    // Conflict rule 3: never issue a new drive command while a manoeuvre is
    // running, or the operator would stomp on the avoidance sequence.
    if (avoidIsActive()) {
      Serial.println(F("ERROR:AVOID_IN_PROGRESS"));
      dbg(F("drive rejected - avoidance in progress"));
      return;
    }

    manualOverrideLatch = false;
    if      (strcmp(line, "CMD:FORWARD")  == 0) activeCommand = CMD_FORWARD;
    else if (strcmp(line, "CMD:BACKWARD") == 0) activeCommand = CMD_BACKWARD;
    else if (strcmp(line, "CMD:LEFT")     == 0) activeCommand = CMD_LEFT;
    else                                       activeCommand = CMD_RIGHT;

    Serial.print(F("ACK:")); Serial.print(commandName(activeCommand)); Serial.println();
    return;
  }

  // ---- SCAN : operator action, pivots to sweep the thermal array ----------
  if (strcmp(line, "CMD:SCAN") == 0) {
    if (obstacleDetected()) {
      Serial.println(F("ERROR:OBSTACLE_BLOCKED"));
      return;
    }
    robotMode = MODE_MANUAL;             // SCAN implies operator control
    manualOverrideLatch = false;
    activeCommand = CMD_SPIN_LEFT;
    Serial.println(F("MODE:MANUAL"));
    Serial.println(F("ACK:SCAN"));
    buzzerBeepOnce(BEEP_SHORT_MS);
    return;
  }

  // ---- Unknown ------------------------------------------------------------
  Serial.print(F("ERROR:UNKNOWN:"));
  Serial.println(line);
}

// Non-blocking line reader. Reads at most what is already buffered, so motor
// control is never stalled waiting for serial data.
inline void uartService() {
  static char   rxLine[48];
  static uint8_t rxLen = 0;

  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    if (c == '\r') continue;                       // tolerate CRLF from PC

    if (c == '\n') {
      rxLine[rxLen] = '\0';
      if (rxLen > 0) {
        rxLineCount++;
        handleCommandLine(rxLine);
      }
      rxLen = 0;
      continue;
    }

    if (rxLen < sizeof(rxLine) - 1) {
      rxLine[rxLen++] = c;
    } else {
      // Overflow: discard the WHOLE line rather than risk executing a
      // truncated command such as "CMD:FORWA".
      rxLen = 0;
      Serial.println(F("ERROR:LINE_TOO_LONG"));
    }
  }
}

// Full status snapshot: exactly the fields the mobile app displays.
inline void sendTelemetry() {
  Serial.print(F("TEMP:"));      Serial.println(rawMaxTemp, 1);
  Serial.print(F("AMBIENT:"));   Serial.println(ambientTemp, 1);
  Serial.print(F("THERMAL:"));   Serial.println(thermalStatus);
  Serial.print(F("HEAT:"));      Serial.println(sectorName(heatDirection));
  Serial.print(F("BATTERY:"));   Serial.println((int)(batteryPercent + 0.5f));
  Serial.print(F("BUZZER:"));    Serial.println(buzzerActive ? F("ON") : F("OFF"));

  // Obstacles + ultrasonic. sendObstacleStatus() emits OBSTACLE:,
  // OBSTACLE_LEFT/RIGHT/REAR, DISTANCE:, DISTANCE_ALERT: and ULTRASONIC:, so
  // the label and the distance always arrive in one consistent snapshot.
  sendObstacleStatus();

  Serial.print(F("AVOID:"));    Serial.println(avoidStateName(avoidState));
  Serial.print(F("SENSOR:"));   Serial.println(sensorReady ? F("READY") : F("MISSING"));
  Serial.print(F("MODE:"));      Serial.println(robotMode == MODE_AUTO ? F("AUTO") : F("MANUAL"));
  Serial.print(F("SPEED:"));     Serial.println(currentSpeed);
  Serial.print(F("MOVE:"));      Serial.println(commandName(activeCommand));
  Serial.print(F("ALERT:"));     Serial.println(alertActive ? F("ON") : F("OFF"));
  Serial.print(F("CONN:"));      Serial.println(failsafeTriggered ? F("LOST") : F("OK"));
}

#endif // BIOMOUSE_UART_H