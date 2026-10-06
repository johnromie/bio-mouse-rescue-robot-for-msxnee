/* ============================================================================
 *  SHARED STATE + SMALL UTILITIES
 * ----------------------------------------------------------------------------
 *  Declared once, included by every module. Keeping the state here (rather
 *  than inside each module) is what lets the modules cooperate without any
 *  of them reaching into each other's internals.
 * ========================================================================== */
#ifndef BIOMOUSE_STATE_H
#define BIOMOUSE_STATE_H

#include <Arduino.h>
#include "config.h"

/* ---- Operating modes ----------------------------------------------------- */
enum RobotMode : uint8_t { MODE_MANUAL = 0, MODE_AUTO = 1 };

/* ---- Movement commands ---------------------------------------------------- */
enum MoveCommand : uint8_t {
  CMD_NONE = 0, CMD_FORWARD, CMD_BACKWARD, CMD_LEFT, CMD_RIGHT,
  CMD_SPIN_LEFT, CMD_SPIN_RIGHT, CMD_STOP
};

/* ---- Robot-level state --------------------------------------------------- */
RobotMode   robotMode           = MODE_MANUAL;
MoveCommand activeCommand       = CMD_STOP;
uint8_t     currentSpeed        = DEFAULT_SPEED;
// Set when the app issues STOP while AUTO is running. Suspends autonomy
// until cleared, so the operator always has the final say.
bool        manualOverrideLatch = false;

/* ---- [5] buzzer state ---------------------------------------------------- */
bool     buzzerActive       = false;
bool     buzzerRepeating    = false;
bool     buzzerNextIsOn     = false;
uint32_t buzzerNextToggleMs = 0;
uint16_t buzzerOnMs         = BEEP_SHORT_MS;
uint16_t buzzerPeriodMs     = BEEP_SHORT_MS * 4;

/* ---- [6] battery state --------------------------------------------------- */
float batteryPercent     = 100.0f;
float batteryVolts       = BATTERY_FULL_VOLTS;
bool  batteryLowAnnounced = false;

/* ---- [2] thermal state --------------------------------------------------- */
bool     sensorReady        = false;
float    ambientTemp        = 25.0f;
float    rawMaxTemp         = 25.0f;
float    sectorTemp[3]      = { 25.0f, 25.0f, 25.0f };  // LEFT, CENTER, RIGHT
uint8_t  heatDirection      = 1;    // 0 = LEFT, 1 = CENTER, 2 = RIGHT
float    heatRise           = 0.0f;
uint8_t  confirmedDirection = 1;    // candidate direction being confirmed
uint8_t  directionFrames    = 0;    // frames the candidate has held
const char* thermalStatus   = "NONE";
bool     alertActive        = false;
bool     closeHeatPending   = false;
uint32_t closeHeatSinceMs   = 0;

/* ---- [4] obstacle state -------------------------------------------------- */
bool     obstacleLeft  = false;
bool     obstacleRight = false;
bool     obstacleRear  = false;
uint32_t lastWhiskerChangeMs = 0;   // debounce timing
uint32_t lastWhiskerEventMs  = 0;   // response cooldown timing

/* ---- [NEW] HC-SR04 ultrasonic state --------------------------------------- */
float    ultrasonicDistanceCm  = -1.0f;  // last valid reading, -1 = invalid
float    ultrasonicSmoothedCm  = -1.0f;  // filtered value used for decisions
uint8_t  ultrasonicFailCount  = 0;       // consecutive invalid readings
bool     ultrasonicPresent    = true;    // false if it stopped reporting
uint32_t lastUltrasonicReadMs  = 0;
uint32_t lastObstacleReportMs = 0;       // rate-limit the OBSTACLE: line

/* ---- [NEW] obstacle avoidance state machine ------------------------------- */
enum AvoidState : uint8_t {
  AVOID_IDLE = 0,     // nothing to do
  AVOID_REACTING,     // obstacle seen, about to stop
  AVOID_STOPPED,      // full stop hold
  AVOID_REVERSING,    // backing away
  AVOID_TURNING,      // steering away from the obstacle
  AVOID_RESUMING      // pause, then check the path before driving on
};

AvoidState avoidState       = AVOID_IDLE;
uint32_t   avoidStateStartMs = 0;
uint32_t   avoidLastStartMs  = 0;   // cooldown anchor
bool       avoidResumeForward = false;   // command to restore afterwards
bool       avoidEscapeManoeuver = false; // larger turn when both whiskers fire
int        avoidTurnDirection = 1;   // +1 turn right, -1 turn left
uint32_t   avoidTriggerCount  = 0;   // how many manoeuvres have run

// Human-readable avoidance phase, reported to the app as AVOID:<name>.
inline const char* avoidStateName(AvoidState s) {
  switch (s) {
    case AVOID_REACTING:  return "REACTING";
    case AVOID_STOPPED:   return "STOPPED";
    case AVOID_REVERSING: return "REVERSING";
    case AVOID_TURNING:   return "TURNING";
    case AVOID_RESUMING:  return "RESUMING";
    default:              return "IDLE";
  }
}

/* ---- [1] speed ramp state ------------------------------------------------ */
int      rampLeft  = 0;
int      rampRight = 0;

/* ---- [8] failsafe state -------------------------------------------------- */
uint32_t lastCommandRxMs   = 0;
bool     failsafeTriggered = false;

/* ---- scan rate limiting (AUTO mode) -------------------------------------- */
uint32_t lastScanTurnMs = 0;

/* ---- timing + statistics ------------------------------------------------- */
uint32_t cmdCount         = 0;
uint32_t rxLineCount      = 0;
uint32_t lastThermalReadMs = 0;
uint32_t lastTelemetryMs   = 0;
uint32_t lastHeartbeatMs   = 0;

/* ==========================================================================
 *  UTILITIES
 * ========================================================================== */
inline uint32_t nowMs() { return millis(); }

// True once `ms` has elapsed since `start`. Written with unsigned arithmetic
// so it stays correct across the millis() rollover (~49 days).
inline bool elapsed(uint32_t start, uint16_t ms) {
  return (uint32_t)(nowMs() - start) >= (uint32_t)ms;
}

// True once a stored deadline has been reached. Use this for schedules that
// are stored as an absolute time; elapsed() is for durations. Signed
// subtraction keeps this correct across the millis() rollover.
inline bool isDue(uint32_t deadlineMs) {
  return (int32_t)(nowMs() - deadlineMs) >= 0;
}

// Debug output. The "DBG:" prefix lets the ESP32-CAM discard these without
// confusing the status protocol.
inline void dbg(const char *msg) {
  if (!DEBUG_ENABLED) return;
  Serial.print(F("DBG:"));
  Serial.println(msg);
}

inline void dbg(const String &msg) {
  if (!DEBUG_ENABLED) return;
  Serial.print(F("DBG:"));
  Serial.println(msg);
}

#endif // BIOMOUSE_STATE_H