/* ============================================================================
 *  MODULE [1] : MOTOR CONTROL   (L298N, 4WD differential drive)
 * ----------------------------------------------------------------------------
 *  Owns every write to the L298N. No other module touches a motor pin, which
 *  is what guarantees that exactly one thing is in control of the motors at
 *  any moment.
 *
 *  Differential drive mixing:
 *    FORWARD   both sides same direction, same speed
 *    BACKWARD  both sides reversed
 *    LEFT      left side slowed/reversed, right side full -> arc left
 *    RIGHT     mirrored arc right
 *    SCAN      left reversed, right full -> pivot on the spot
 *
 *  ENA carries the LEFT motor pair, ENB carries the RIGHT motor pair.
 * ========================================================================== */
#ifndef BIOMOUSE_MOTOR_H
#define BIOMOUSE_MOTOR_H

#include "state.h"
#include "config.h"

// Signed -100..100 speed request -> signed 0..255 PWM duty.
inline int requestToPwm(int request) {
  int r = constrain(request, -(int)MAX_SPEED, (int)MAX_SPEED);
  int duty = (int)((abs(r) * 255L) / MAX_SPEED);
  if (abs(r) > 0 && duty < 30) duty = 30;   // beat static friction on small motors
  return (r < 0) ? -duty : duty;
}

// Ramp toward the targets, then drive the direction and speed pins.
// The ramp exists so the robot never slams 0 -> 100, which would brown out
// the regulator, trip the L298N's current limiting, or stall the motors.
inline void applyRampedSpeed(int targetLeft, int targetRight) {
  targetLeft  = constrain(targetLeft,  -255, 255);
  targetRight = constrain(targetRight, -255, 255);

  static uint32_t lastRampStepMs = 0;
  if (elapsed(lastRampStepMs, RAMP_STEP_MS)) {
    lastRampStepMs = nowMs();
    rampLeft  = (rampLeft  < targetLeft)  ? min(rampLeft  + RAMP_STEP, targetLeft)
                                           : max(rampLeft  - RAMP_STEP, targetLeft);
    rampRight = (rampRight < targetRight) ? min(rampRight + RAMP_STEP, targetRight)
                                           : max(rampRight - RAMP_STEP, targetRight);
  }

  bool leftFwd = (rampLeft > 0),  leftRev  = (rampLeft < 0);
  bool rightFwd = (rampRight > 0), rightRev = (rampRight < 0);
  if (LEFT_MOTORS_REVERSED)  { bool t = leftFwd;  leftFwd  = leftRev;  leftRev  = t; }
  if (RIGHT_MOTORS_REVERSED) { bool t = rightFwd; rightFwd = rightRev; rightRev = t; }

  digitalWrite(PIN_L298N_IN1, leftFwd  ? HIGH : LOW);
  digitalWrite(PIN_L298N_IN2, leftRev  ? HIGH : LOW);
  digitalWrite(PIN_L298N_IN3, rightFwd ? HIGH : LOW);
  digitalWrite(PIN_L298N_IN4, rightRev ? HIGH : LOW);

  analogWrite(PIN_L298N_ENA, abs(rampLeft));
  analogWrite(PIN_L298N_ENB, abs(rampRight));
}

// Instant full stop, no ramp. This is the emergency path used by the E-STOP,
// the failsafe watchdog and the close-range thermal alert.
inline void motorsHardStop() {
  rampLeft = rampRight = 0;
  digitalWrite(PIN_L298N_IN1, LOW);
  digitalWrite(PIN_L298N_IN2, LOW);
  digitalWrite(PIN_L298N_IN3, LOW);
  digitalWrite(PIN_L298N_IN4, LOW);
  analogWrite(PIN_L298N_ENA, 0);
  analogWrite(PIN_L298N_ENB, 0);
}

// Human-readable name of the current command (used in debug + telemetry).
inline const char* commandName(MoveCommand c) {
  switch (c) {
    case CMD_FORWARD:    return "FORWARD";
    case CMD_BACKWARD:   return "BACKWARD";
    case CMD_LEFT:       return "LEFT";
    case CMD_RIGHT:      return "RIGHT";
    case CMD_SPIN_LEFT:  return "SCAN";
    case CMD_SPIN_RIGHT: return "SPIN_RIGHT";
    case CMD_STOP:       return "STOP";
    default:             return "NONE";
  }
}

// Turn an abstract command into signed left/right speed requests.
inline void resolveCommandToWheelTargets(MoveCommand c, int spd,
                                         int &outLeft, int &outRight) {
  switch (c) {
    case CMD_FORWARD:
      outLeft = spd; outRight = spd; break;
    case CMD_BACKWARD:
      outLeft = -spd; outRight = -spd; break;
    case CMD_LEFT:        // forward arc left
      outLeft = -(int)(spd * TURN_FACTOR); outRight = spd; break;
    case CMD_RIGHT:       // forward arc right
      outLeft = spd; outRight = -(int)(spd * TURN_FACTOR); break;
    case CMD_SPIN_LEFT:   // pivot on the spot
      outLeft = -spd; outRight = spd; break;
    case CMD_SPIN_RIGHT:
      outLeft = spd; outRight = -spd; break;
    case CMD_STOP:
    default:
      outLeft = 0; outRight = 0; break;
  }
}

#endif // BIOMOUSE_MOTOR_H