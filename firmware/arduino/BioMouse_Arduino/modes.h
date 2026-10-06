/* ============================================================================
 *  MODULES [9] and [10] : MANUAL MODE  /  AUTOMATIC THERMAL-GUIDED MODE
 * ----------------------------------------------------------------------------
 *  Exactly one of these two functions drives the motors on each loop pass,
 *  selected by robotMode. This is the key conflict-prevention mechanism: the
 *  drive logic is split by MODE, so a manual command can never fight an
 *  autonomous decision.
 *
 *  [9]  MANUAL MODE
 *      The operator's last command is authoritative. An obstacle stops the
 *      robot and waits; a dead link stops it via the failsafe watchdog.
 *
 *  [10] AUTOMATIC THERMAL-GUIDED MODE
 *      This board decides where to go from the AMG8833. The app may still
 *      send STOP at any time: that sets manualOverrideLatch, which suspends
 *      autonomy until the operator clears it.
 * ========================================================================== */
#ifndef BIOMOUSE_MODES_H
#define BIOMOUSE_MODES_H

#include "state.h"
#include "motor.h"
#include "navigation.h"
#include "safety.h"
#include "obstacle.h"      // NEW: owns obstacle detection + the manoeuvre
#include "ultrasonic.h"

// ---- [9] MANUAL MODE ------------------------------------------------------
inline void manualModeService() {
  if (failsafeTriggered) { motorsHardStop(); return; }

  // An avoidance manoeuvre owns the motors while it runs. This is how the
  // ultrasonic and the whiskers take priority over the operator's drive
  // command WITHOUT the manoeuvre ever blocking the UART: avoidService()
  // advances the state machine, and this service simply stands down.
  if (avoidIsActive()) return;

  if (obstacleDetected()) {
    // Report and hold position. In MANUAL the operator is in charge, so we do
    // not manoeuvre automatically -- but we also will not drive into anything.
    motorsHardStop();
    if (activeCommand != CMD_STOP) {
      activeCommand = CMD_STOP;
      Serial.println(F("ROBOT:STOPPED:OBSTACLE"));
      dbg(F("manual: obstacle present, holding position"));
    }
    return;
  }

  int l, r;
  resolveCommandToWheelTargets(activeCommand, currentSpeed, l, r);
  applyRampedSpeed(requestToPwm(l), requestToPwm(r));
}

// ---- [10] AUTOMATIC THERMAL-GUIDED MODE -----------------------------------
inline void autoModeService() {
  // An alert or an obstacle always halts autonomy.
  if (alertActive) {
    motorsHardStop();
    activeCommand = CMD_STOP;
    return;
  }
  // Failsafe (link lost) or an operator STOP both suspend autonomy.
  if (failsafeTriggered || manualOverrideLatch) { motorsHardStop(); return; }
  // Never drive on a blind sensor.
  if (!sensorReady) { motorsHardStop(); return; }

  // The avoidance state machine owns the motors while it is running.
  if (avoidIsActive()) return;

  // Blocked and not manoeuvring (either not yet triggered, or the path stayed
  // blocked after a manoeuvre). Stay put rather than driving into it.
  if (obstacleDetected()) {
    motorsHardStop();
    activeCommand = CMD_STOP;
    return;
  }

  MoveCommand desired = thermalNavigationDecision();

  // Rate-limit SCAN so the robot pivots in short bursts with a pause between,
  // instead of spinning continuously at full speed.
  if (desired == CMD_SPIN_LEFT) {
    if (!elapsed(lastScanTurnMs, 1200)) return;   // resting between bursts
    lastScanTurnMs = nowMs();
  } else {
    lastScanTurnMs = 0;
  }

  activeCommand = desired;
  int l, r;
  resolveCommandToWheelTargets(desired, currentSpeed, l, r);
  applyRampedSpeed(requestToPwm(l), requestToPwm(r));
}

#endif // BIOMOUSE_MODES_H