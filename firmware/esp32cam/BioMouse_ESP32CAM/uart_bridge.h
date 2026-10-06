/* ============================================================================
 *  MODULE [7b] : UART BRIDGE  (ESP32-CAM <--> ARDUINO)
 * ----------------------------------------------------------------------------
 *  This is the ARDUINO END of the protocol. Two jobs:
 *
 *    1. TX  -- forward an app command to the Arduino as one text line.
 *    2. RX  -- parse the Arduino's status lines into the RobotStatus struct,
 *              so the HTTP layer can serve the app as JSON.
 *
 *  PROTOCOL SUMMARY (full spec in docs/PROTOCOL.md)
 *    Down: CMD:FORWARD / CMD:SPEED:60 / CMD:MODE:AUTO ...
 *    Up:   TEMP:36.4 / THERMAL:POSSIBLE_SURVIVOR / OBSTACLE:LEFT / ...
 * ========================================================================== */
#ifndef BIOMOUSE_ESP_UART_H
#define BIOMOUSE_ESP_UART_H

#include "config.h"

static RobotStatus robot;
static char         rxBuf[80];
static uint8_t      rxLen = 0;
static bool         espDebugEnabled = true;

/* ---- Initialise UART2 on the configured pins --------------------------- */
inline void uartBridgeBegin() {
  ARDUINO_UART.begin(UART_BAUD, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
}

/* ==========================================================================
 *  TX : app -> Arduino
 * ==========================================================================
 *  One command per line, "\n" terminated. Every command is echoed to the USB
 *  serial console so you can watch what the app does, EXCEPT PING, which
 *  would flood the console twice a second.
 * ========================================================================== */
inline void sendToArduino(const char *line) {
  ARDUINO_UART.print(line);
  ARDUINO_UART.print("\n");
  robot.cmdsReceived++;

  if (strcmp(line, "CMD:PING") != 0) {
    Serial.print(F("[APP->ROBOT] "));
    Serial.println(line);
  }
}

// Convenience wrappers matching the app's command set.
inline void cmdMovement(const char *cmd)  { sendToArduino(cmd); }   // CMD:FORWARD ...
inline void cmdSpeed(int percent) {
  char buf[24];
  snprintf(buf, sizeof(buf), "CMD:SPEED:%d", constrain(percent, 0, 100));
  sendToArduino(buf);
}
inline void cmdMode(bool autoMode) { sendToArduino(autoMode ? "CMD:MODE:AUTO" : "CMD:MODE:MANUAL"); }
inline void cmdStop()              { sendToArduino("CMD:STOP"); }
inline void cmdScan()              { sendToArduino("CMD:SCAN"); }
inline void cmdBuzzer(bool on)     { sendToArduino(on ? "CMD:BUZZER:ON" : "CMD:BUZZER:OFF"); }
inline void cmdBuzzerTest()        { sendToArduino("CMD:BUZZER:TEST"); }
inline void cmdAlertReset()        { sendToArduino("CMD:ALERT:RESET"); }
inline void cmdSelfTest()          { sendToArduino("CMD:SELFTEST"); }

/* ==========================================================================
 *  RX : Arduino -> this board -> app
 * ==========================================================================
 *  Apply one "KEY:VALUE" line to the RobotStatus struct.
 * ========================================================================== */
inline void copyField(char *dst, size_t size, const char *src) {
  strncpy(dst, src, size - 1);
  dst[size - 1] = '\0';
}

inline void handleStatusLine(char *line) {
  // Split "KEY:VALUE" at the first colon only.
  char *colon = strchr(line, ':');
  if (!colon) return;

  *colon = '\0';
  const char *key   = line;
  const char *value = colon + 1;

  // ---- Liveness ---------------------------------------------------------
  if (!strcmp(key, "ROBOT")) {
    robot.robotOnline = true;
    robot.linkLost = false;
    // ROBOT:ONLINE:CMDS=..:RX=..:UPTIME=..   OR   ROBOT:STOPPED:<reason>
    if (!strncmp(value, "STOPPED", 7)) {
      const char *reason = strchr(value, ':');
      copyField(robot.lastStopReason, sizeof(robot.lastStopReason),
                reason ? reason + 1 : "STOPPED");
      Serial.print(F("[STOP] "));
      Serial.println(robot.lastStopReason);
    }
  }
  else if (!strcmp(key, "EVENT")) {
    // Important events, echoed for bench testing.
    Serial.print(F("[ROBOT->APP] EVENT: "));
    Serial.println(value);

    if      (!strcmp(value, "FAILSAFE_LINK_LOST"))   { robot.linkLost = true;  }
    else if (!strcmp(value, "FAILSAFE_CLEARED"))     { robot.linkLost = false; }
    else if (!strcmp(value, "ALERT_HEAT_DETECTED"))  { robot.alert = true;    }
    else if (!strcmp(value, "ALERT_CLEARED"))        { robot.alert = false;   }
  }

  // ---- Mode / movement --------------------------------------------------
  else if (!strcmp(key, "MODE"))  { copyField(robot.mode, sizeof(robot.mode), value); }
  else if (!strcmp(key, "SPEED")) { robot.speed = (uint8_t)constrain(atoi(value), 0, 100); }
  else if (!strcmp(key, "MOVE"))  { copyField(robot.move, sizeof(robot.move), value); }

  // ---- Thermal ----------------------------------------------------------
  else if (!strcmp(key, "TEMP"))    { robot.temperature = atof(value); }
  else if (!strcmp(key, "AMBIENT")) { robot.ambient     = atof(value); }
  else if (!strcmp(key, "THERMAL")) { copyField(robot.thermalStatus, sizeof(robot.thermalStatus), value); }
  else if (!strcmp(key, "HEAT"))    { copyField(robot.heatDirection, sizeof(robot.heatDirection), value); }

  // ---- Obstacles --------------------------------------------------------
  else if (!strcmp(key, "OBSTACLE")) {
    // The value is now "LABEL:DISTANCE", e.g. DETECTED:25 or CLEAR:85.
    // Split on the first colon: the label is the obstacle state, the number is
    // the ultrasonic reading. This keeps the original OBSTACLE:<label> format
    // intact for existing app code while adding the distance.
    char label[16] = {0};
    const char *dist = strchr(value, ':');
    if (dist) {
      size_t n = (size_t)(dist - value);
      if (n >= sizeof(label)) n = sizeof(label) - 1;
      strncpy(label, value, n);
      label[n] = '\0';
      robot.distanceCm = atof(dist + 1);
    } else {
      strncpy(label, value, sizeof(label) - 1);
    }
    copyField(robot.obstacle, sizeof(robot.obstacle), label);
  }
  else if (!strcmp(key, "OBSTACLE_LEFT"))  { robot.obstacleLeft  = (atoi(value) != 0); }
  else if (!strcmp(key, "OBSTACLE_RIGHT")) { robot.obstacleRight = (atoi(value) != 0); }
  else if (!strcmp(key, "OBSTACLE_REAR"))  { robot.obstacleRear  = (atoi(value) != 0); }

  // ---- NEW: HC-SR04 ultrasonic ------------------------------------------
  // DISTANCE:25.4CM  -> strip the "CM" suffix and store the number.
  else if (!strcmp(key, "DISTANCE")) {
    char buf[16] = {0};
    size_t n = 0;
    while (value[n] && value[n] != 'C' && value[n] != 'c' && n < sizeof(buf) - 1) {
      buf[n] = value[n];
      n++;
    }
    buf[n] = '\0';
    robot.distanceCm = atof(buf);
  }
  else if (!strcmp(key, "DISTANCE_THRESHOLD")) { robot.distanceThreshold = atoi(value); }
  else if (!strcmp(key, "DISTANCE_ALERT"))    { robot.distanceAlert = (strcmp(value, "ON") == 0); }
  else if (!strcmp(key, "ULTRASONIC"))        { copyField(robot.ultrasonic, sizeof(robot.ultrasonic), value); }
  else if (!strcmp(key, "AVOID"))             { copyField(robot.avoidState, sizeof(robot.avoidState), value); }

  // ---- Power / audio / health -------------------------------------------
  else if (!strcmp(key, "BATTERY")) { robot.battery = constrain(atoi(value), 0, 100); }
  else if (!strcmp(key, "BUZZER"))  { copyField(robot.buzzer, sizeof(robot.buzzer), value); }
  else if (!strcmp(key, "ALERT"))   { robot.alert = (strcmp(value, "ON") == 0); }
  else if (!strcmp(key, "SENSOR"))  { copyField(robot.sensor, sizeof(robot.sensor), value); }
  else if (!strcmp(key, "CONN"))    { robot.linkLost = (strcmp(value, "LOST") == 0); }

  // Debug lines are for humans, not for state.
  else if (!strcmp(key, "DBG") && espDebugEnabled) {
    Serial.print(F("[DBG] "));
    Serial.println(value);
  }
}

/* ==========================================================================
 *  UART service loop -- read whatever has arrived, one line at a time
 * ========================================================================== */
inline void uartBridgeService() {
  while (ARDUINO_UART.available()) {
    char c = (char)ARDUINO_UART.read();

    if (c == '\r') continue;                       // tolerate CRLF
    if (c == '\n') {
      rxBuf[rxLen] = '\0';
      if (rxLen > 0) {
        robot.lastRxMs = millis();
        handleStatusLine(rxBuf);
      }
      rxLen = 0;
      continue;
    }

    if (rxLen < sizeof(rxBuf) - 1) rxBuf[rxLen++] = c;
    else                            rxLen = 0;      // drop an over-long line
  }

  // If the Arduino has gone quiet, report the robot offline so the app shows
  // a clear DISCONNECTED state instead of stale numbers.
  if (robot.robotOnline &&
      (uint32_t)(millis() - robot.lastRxMs) > ROBOT_OFFLINE_TIMEOUT_MS) {
    robot.robotOnline = false;
    robot.linkLost    = true;
  }
}

/* ==========================================================================
 *  Keep the Arduino's failsafe watchdog fed
 * ==========================================================================
 *  CRITICAL SAFETY FUNCTION. The Arduino stops itself if it does not hear from
 *  this board for FAILSAFE_TIMEOUT_MS (3000 ms). This ping runs every 500 ms,
 *  independently of whether the app is connected, so the robot stays alive and
 *  responsive while it is simply waiting for the operator.
 *
 *  Deliberate asymmetry: if this ESP32 loses power or crashes, the pings stop
 *  and the Arduino stops the robot. That is the desired outcome.
 * ========================================================================== */
inline void uartBridgePingService() {
  static uint32_t lastPingMs = 0;
  if ((uint32_t)(millis() - lastPingMs) >= PING_INTERVAL_MS) {
    lastPingMs = millis();
    ARDUINO_UART.print("CMD:PING\n");     // not logged: too chatty
  }
}

#endif // BIOMOUSE_ESP_UART_H