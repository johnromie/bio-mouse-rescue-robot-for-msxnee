/* ============================================================================
 *  MODULE [4] : WHISKER OBSTACLE DETECTION  (secondary / backup)
 * ----------------------------------------------------------------------------
 *  The whiskers are the PHYSICAL BACKUP detector. The HC-SR04 gives early
 *  warning of obstacles at range, but sound reflects poorly off thin poles,
 *  angled glass and soft fabric -- irregular debris, which is exactly what a
 *  rescue robot meets. The whiskers cannot miss that, because they physically
 *  touch it.
 *  The physical left and right whiskers are made from guitar strings.
 *
 *  THE MANOEUVRE MOVED. This module used to contain a blocking
 *  reverse-and-turn routine built from delay(). That has been replaced by the
 *  non-blocking state machine in obstacle.h, because a blocking manoeuvre held
 *  the CPU for ~900 ms and an emergency STOP could not be read during it.
 *  This module now only does detection and reporting; obstacle.h decides what
 *  the motors do.
 *
 *  DEBOUNCE: a microswitch bounces for several milliseconds. A new state is
 *  only accepted once it has held steady for WHISKER_DEBOUNCE_MS.
 * ========================================================================== */
#ifndef BIOMOUSE_WHISKER_H
#define BIOMOUSE_WHISKER_H

#include "state.h"

// Raw (undebounced) reading of one whisker.
inline bool readWhiskerRaw(uint8_t pin) {
  if (pin < 0) return false;
  int v = digitalRead(pin);
  return WHISKER_ACTIVE_LOW ? (v == LOW) : (v == HIGH);
}

/**
 * Debounced whisker reader.
 *
 * Updates obstacleLeft / obstacleRight / obstacleRear only once a change has
 * been stable long enough, then logs the event. It deliberately does NOT call
 * any avoidance code: obstacle.h owns the manoeuvre, so the ultrasonic and the
 * whiskers share one coherent state machine instead of two overlapping ones.
 */
inline void whiskerService() {
  bool rawLeft  = readWhiskerRaw(PIN_WHISKER_LEFT);
  bool rawRight = readWhiskerRaw(PIN_WHISKER_RIGHT);
  bool rawRear  = readWhiskerRaw(PIN_WHISKER_REAR);

  if (rawLeft == obstacleLeft &&
      rawRight == obstacleRight &&
      rawRear == obstacleRear) {
    return;                          // nothing changed
  }

  // A change is pending but not yet stable; require it to hold first.
  if (!elapsed(lastWhiskerChangeMs, WHISKER_DEBOUNCE_MS)) return;
  lastWhiskerChangeMs = nowMs();

  obstacleLeft  = rawLeft;
  obstacleRight = rawRight;
  obstacleRear  = rawRear;

  if (obstacleLeft || obstacleRight || obstacleRear) {
    Serial.print(F("EVENT:WHISKER_HIT:"));
    if (obstacleLeft)  Serial.print(F("LEFT "));
    if (obstacleRight) Serial.print(F("RIGHT "));
    if (obstacleRear)  Serial.print(F("REAR "));
    Serial.println();
    dbg(F("whisker state changed"));
  }

  // The OBSTACLE: line itself is emitted by sendObstacleStatus() from the
  // telemetry cycle, so it always carries the ultrasonic distance too.
}

#endif // BIOMOUSE_WHISKER_H
