/* ============================================================================
 *  MODULE [NEW] : OBSTACLE AVOIDANCE STATE MACHINE
 * ----------------------------------------------------------------------------
 *  Replaces the old blocking delay()-based manoeuvre with a NON-BLOCKING
 *  state machine. This is required for the ultrasonic integration, because the
 *  robot must stay responsive to an emergency STOP from the app throughout the
 *  manoeuvre. With the old blocking version, a reverse+turn held the CPU for
 *  ~900 ms during which a STOP command could not be read.
 *
 *  THE SEQUENCE
 *      AVOID_REACTING  -> obstacle detected; cut motor power immediately
 *      AVOID_STOPPED   -> full stop hold (AVOID_STOP_HOLD_MS)
 *      AVOID_REVERSING -> reverse straight back (AVOID_REVERSE_MS)
 *      AVOID_TURNING   -> pivot away from the obstacle (AVOID_TURN_MS)
 *      AVOID_RESUMING  -> pause, re-verify the path, then resume if clear
 *
 *  TRIGGERING
 *    Primary   : HC-SR04 at or below ULTRASONIC_THRESHOLD_CM (30 cm)
 *    Secondary : LEFT / RIGHT / BOTH whiskers
 *
 *  TURN DIRECTION
 *    LEFT  whisker  -> turn RIGHT  (steer away from the left contact)
 *    RIGHT whisker  -> turn LEFT   (steer away from the right contact)
 *    BOTH whiskers  -> escape manoeuvre: a longer, wider turn
 *    Ultrasonic only -> use the last known side, or the default in config, so
 *                      the robot is not left oscillating between turns.
 *
 *  GUARANTEES
 *    - An emergency STOP always wins (checked in avoidService and modeService)
 *    - No repeated manoeuvres: AVOID_COOLDOWN_MS gates every new trigger
 *    - Never reverse or turn when nothing is detected
 *    - Resumes only when the path is genuinely clear
 * ========================================================================== */
#ifndef BIOMOUSE_OBSTACLE_H
#define BIOMOUSE_OBSTACLE_H

#include "state.h"
#include "motor.h"
#include "safety.h"
#include "buzzer.h"
#include "ultrasonic.h"

// Abandon any manoeuvre and hand control back to the mode service.
inline void avoidCancel(const char *reason) {
  if (avoidState == AVOID_IDLE) return;
  motorsHardStop();
  avoidState = AVOID_IDLE;
  Serial.print(F("EVENT:OBSTACLE_AVOID_CANCELLED:"));
  Serial.println(reason);
}

// Is a manoeuvre currently running? The mode services use this to stand down.
inline bool avoidIsActive() { return avoidState != AVOID_IDLE; }

// Decide which way to turn away from whatever was detected.
inline void avoidPlanTurn() {
  // Both whiskers: the robot is boxed in, so back up and turn hard.
  if (obstacleLeft && obstacleRight) {
    avoidTurnDirection   = (avoidTurnDirection >= 0) ? 1 : -1;
    avoidEscapeManoeuver = true;
    return;
  }
  if (obstacleRight) { avoidTurnDirection = -1; avoidEscapeManoeuver = false; return; }  // turn left
  if (obstacleLeft)  { avoidTurnDirection =  1; avoidEscapeManoeuver = false; return; }  // turn right

  // Ultrasonic only: no side information, so keep the previous direction for
  // consistency. Defaulting to +1 means the robot turns right on first sight.
  avoidEscapeManoeuver = false;
}

/**
 * Start an avoidance manoeuvre, if the cooldown allows it.
 * Called automatically by avoidService(); exposed for manual triggering.
 */
inline void avoidTrigger() {
  if (avoidState != AVOID_IDLE) return;                      // already busy

  // Cooldown stops the robot reversing repeatedly against the same wall.
  if (!elapsed(avoidLastStartMs, AVOID_COOLDOWN_MS)) return;

  avoidPlanTurn();
  avoidState            = AVOID_REACTING;
  avoidStateStartMs     = nowMs();
  avoidLastStartMs      = nowMs();
  avoidTriggerCount++;

  // Remember whether we were driving forward, so we can resume sensibly.
  avoidResumeForward = (activeCommand == CMD_FORWARD);

  // Immediate stop. This is the "immediately STOP the motors" requirement.
  motorsHardStop();
  activeCommand = CMD_STOP;

  Serial.print(F("EVENT:OBSTACLE_DETECTED:"));
  if (obstacleLeft && obstacleRight) Serial.print(F("BOTH_WHISKER"));
  else if (obstacleLeft)              Serial.print(F("WHISKER_LEFT"));
  else if (obstacleRight)             Serial.print(F("WHISKER_RIGHT"));
  else if (obstacleRear)              Serial.print(F("WHISKER_REAR"));
  else {
    Serial.print(F("ULTRASONIC:"));
    Serial.print(ultrasonicSmoothedCm, 0);
    Serial.print(F("CM"));
  }
  Serial.println();

  dbg(F("avoidance started"));
  buzzerBeepOnce(BEEP_SHORT_MS);
}

/* ==========================================================================
 *  The state machine itself.
 * --------------------------------------------------------------------------
 *  Called every loop pass. Advances at most one phase per call, using
 *  millis() timing only -- there is no delay() anywhere in this file, so the
 *  UART listener and the failsafe watchdog keep running throughout.
 * ========================================================================== */
inline void avoidService() {
  // ---- 1. Emergency override: STOP and E-STOP always win ----------------
  if (failsafeTriggered) { avoidCancel("FAILSAFE"); return; }
  if (readEstopButton()) { avoidCancel("ESTOP"); return; }

  // ---- 2. Idle: watch for a new trigger ---------------------------------
  if (avoidState == AVOID_IDLE) {
    // Only react while actually moving forward. Sitting still against an
    // obstacle must not make the robot shuffle backwards forever.
    if (activeCommand != CMD_FORWARD &&
        !obstacleLeft && !obstacleRight && !obstacleRear) {
      return;
    }
    if (obstacleDetected()) avoidTrigger();
    return;
  }

  // ---- 3. Active: advance one phase -------------------------------------
  switch (avoidState) {

    // --- Phase 1: motors already stopped; begin the timed hold -----------
    case AVOID_REACTING:
      motorsHardStop();
      avoidState = AVOID_STOPPED;
      avoidStateStartMs = nowMs();
      break;

    // --- Phase 2: full stop, then start reversing ------------------------
    case AVOID_STOPPED:
      motorsHardStop();
      if (elapsed(avoidStateStartMs, AVOID_STOP_HOLD_MS)) {
        applyRampedSpeed(requestToPwm(-AVOID_REVERSE_SPEED),
                         requestToPwm(-AVOID_REVERSE_SPEED));
        avoidState = AVOID_REVERSING;
        avoidStateStartMs = nowMs();
      }
      break;

    // --- Phase 3: reverse straight back ----------------------------------
    case AVOID_REVERSING:
      if (elapsed(avoidStateStartMs, AVOID_REVERSE_MS)) {
        // Turn away. An escape manoeuvre turns harder, because both whiskers
        // firing means the robot is wedged.
        uint8_t speed = avoidEscapeManoeuver
                          ? (uint8_t)(AVOID_TURN_SPEED + 20)
                          : AVOID_TURN_SPEED;
        applyRampedSpeed(requestToPwm(-avoidTurnDirection * speed),
                         requestToPwm( avoidTurnDirection * speed));
        avoidState = AVOID_TURNING;
        avoidStateStartMs = nowMs();
      }
      break;

// --- Phase 4: steer away ---------------------------------------------
    case AVOID_TURNING:
      if (elapsed(avoidStateStartMs, AVOID_TURN_MS)) {
        motorsHardStop();                 // come to rest before re-checking
        avoidState = AVOID_RESUMING;
        avoidStateStartMs = nowMs();
      }
      break;

    // --- Phase 5: resume ONLY if the path is genuinely clear -------------
    case AVOID_RESUMING:
      motorsHardStop();
      if (!elapsed(avoidStateStartMs, AVOID_RESUME_DELAY_MS)) break;

      if (obstacleDetected()) {
        // Still blocked. Do NOT immediately loop into another manoeuvre -- that
        // is exactly the oscillation this design must avoid. Hold position and
        // wait for the operator, and restart the cooldown so the robot can act
        // again once the path clears.
        avoidState       = AVOID_IDLE;
        avoidLastStartMs = nowMs();
        motorsHardStop();
        Serial.println(F("EVENT:OBSTACLE_STILL_BLOCKED"));
        dbg(F("path still blocked - holding position"));
        break;
      }

      // Clear: hand control back to the mode service.
      avoidState = AVOID_IDLE;
      Serial.println(F("EVENT:OBSTACLE_AVOID_COMPLETE"));
      dbg(F("path clear, avoidance complete"));

      // Resume only what we were doing before, and only in AUTO mode. In
      // MANUAL the operator is in charge, so never restart driving unasked.
      if (robotMode == MODE_AUTO && avoidResumeForward) {
        activeCommand = CMD_FORWARD;
      } else {
        activeCommand = CMD_STOP;
      }
      break;

    default:
      avoidState = AVOID_IDLE;
      break;
  }
}

/* ==========================================================================
 *  Combined obstacle status for the ESP32-CAM and the mobile app
 * --------------------------------------------------------------------------
 *  Emits the OBSTACLE: line plus the whisker flags and the ultrasonic distance:
 *      OBSTACLE:CLEAR:85        path clear, 85 cm to the nearest surface
 *      OBSTACLE:DETECTED:25     ultrasonic sees something 25 cm ahead
 *      OBSTACLE:LEFT:12         left whisker struck, ultrasonic reads 12 cm
 *      OBSTACLE:BOTH:NA         both whiskers struck, ultrasonic not reporting
 *
 *  This is the alert source the app uses for its distance warning.
 * ========================================================================== */
inline void sendObstacleStatus() {
  // Whisker identity takes priority in the label, because that is what the
  // operator needs to interpret it; the ultrasonic distance is always appended.
  Serial.print(F("OBSTACLE:"));
  if      (obstacleLeft && obstacleRight) Serial.print(F("BOTH"));
  else if (obstacleLeft)                 Serial.print(F("LEFT"));
  else if (obstacleRight)                Serial.print(F("RIGHT"));
  else if (obstacleRear)                 Serial.print(F("REAR"));
  else if (ultrasonicObstacleDetected()) Serial.print(F("DETECTED"));
  else                                   Serial.print(F("CLEAR"));

  Serial.print(F(":"));
  if (ultrasonicSmoothedCm >= 0.0f) Serial.print((int)(ultrasonicSmoothedCm + 0.5f));
  else                              Serial.print(F("NA"));
  Serial.println();

  Serial.print(F("OBSTACLE_LEFT:"));  Serial.println(obstacleLeft  ? 1 : 0);
  Serial.print(F("OBSTACLE_RIGHT:")); Serial.println(obstacleRight ? 1 : 0);
  Serial.print(F("OBSTACLE_REAR:"));  Serial.println(obstacleRear  ? 1 : 0);

  // Dedicated lines for the app's distance readout and its alert banner.
  Serial.print(F("DISTANCE:")); Serial.print(ultrasonicDistanceCm, 1);
  Serial.println(F("CM"));
  Serial.print(F("DISTANCE_THRESHOLD:")); Serial.println((int)ULTRASONIC_THRESHOLD_CM);
  Serial.print(F("DISTANCE_ALERT:"));
  Serial.println(ultrasonicObstacleDetected() ? F("ON") : F("OFF"));
  Serial.print(F("ULTRASONIC:"));
  Serial.println(ultrasonicPresent ? F("READY") : F("NO_ECHO"));
}

#endif // BIOMOUSE_OBSTACLE_H