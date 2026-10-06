/* ============================================================================
 *  MODULE [11 NEW] : HC-SR04 ULTRASONIC  (primary front obstacle detection)
 * ----------------------------------------------------------------------------
 *  WIRING (as specified)
 *    VCC  -> Arduino 5V
 *    GND  -> Arduino GND
 *    TRIG -> Arduino D8
 *    ECHO -> Arduino D7
 *
 *  HOW IT WORKS
 *    1. Hold TRIG LOW, then HIGH for 10 microseconds.
 *    2. The module emits an 8-cycle 40 kHz burst.
 *    3. ECHO goes high for a time proportional to the distance to whatever
 *       reflected the sound, then low.
 *    4. distance_cm = (echo_microseconds / 2) / 29.034
 *       The /2 is because sound makes a round trip.
 *
 *  NO-ECHO HANDLING  (important)
 *    pulseIn() is given a timeout so a missing echo cannot freeze the robot.
 *    If the echo times out the reading is INVALID -- it is NOT treated as an
 *    obstacle and NOT treated as "clear" either. The whiskers stay active, so a
 *    silent ultrasonic can never make the robot drive blind.
 *
 *  WHY THE WHISKERS STILL EXIST
 *    Sound reflects poorly off thin poles, angled glass, soft fabric and
 *    irregular rubble -- exactly the debris a rescue robot meets. The HC-SR04
 *    gives early warning at range; the whiskers are the physical backup.
 * ========================================================================== */
#ifndef BIOMOUSE_ULTRASONIC_H
#define BIOMOUSE_ULTRASONIC_H

#include "state.h"

/* ==========================================================================
 *  readUltrasonicDistance()
 * --------------------------------------------------------------------------
 *  Performs ONE complete HC-SR04 measurement.
 *
 *  @return distance in centimetres, or -1.0f if the reading was invalid
 *          (no echo within the timeout, or an implausible value).
 *
 *  NOTE ON BLOCKING: pulseIn() blocks for at most
 *  ULTRASONIC_ECHO_TIMEOUT_US (25 ms) and normally only ~1 ms. That is why it
 *  is rate-limited rather than called on every loop iteration.
 * ========================================================================== */
inline float readUltrasonicDistance() {
  // --- Step 1: make sure TRIG is low before the trigger pulse ---
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
  delayMicroseconds(4);

  // --- Step 2: 10 microsecond trigger pulse, per the datasheet ---
  digitalWrite(PIN_ULTRASONIC_TRIG, HIGH);
  delayMicroseconds(ULTRASONIC_TRIGGER_PULSE_US);
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);

  // --- Step 3: wait for the echo, but never longer than the timeout ---
  unsigned long echoUs = pulseIn(PIN_ULTRASONIC_ECHO, HIGH,
                                 ULTRASONIC_ECHO_TIMEOUT_US);

  // --- Step 4: no echo -> invalid, NOT an obstacle and NOT "clear" ---
  if (echoUs == 0) return -1.0f;

  // --- Step 5: convert round-trip microseconds to centimetres ---
  float cm = (float)echoUs / US_PER_CM;

  // --- Step 6: reject values the sensor cannot physically produce ---
  if (cm < ULTRASONIC_MIN_VALID_CM || cm > ULTRASONIC_MAX_VALID_CM) {
    return -1.0f;
  }
  return cm;
}

/* ==========================================================================
 *  ultrasonicService() -- non-blocking, rate-limited reader
 * --------------------------------------------------------------------------
 *  Called every loop pass. Takes a measurement at most every
 *  ULTRASONIC_INTERVAL_MS (75 ms), maintains the failure counter, and prints
 *  the distance to the serial monitor for debugging.
 *
 *  SMOOTHING: a single ultrasonic reading is noisy, so the value used for
 *  decisions is an exponentially-weighted average. Without this, one spurious
 *  25 cm reading could trigger a full avoidance manoeuvre.
 * ========================================================================== */
inline void ultrasonicService() {
  if (!elapsed(lastUltrasonicReadMs, ULTRASONIC_INTERVAL_MS)) return;
  lastUltrasonicReadMs = nowMs();

  float cm = readUltrasonicDistance();

  if (cm < 0.0f) {
    // ---- INVALID: no usable echo ----
    if (ultrasonicFailCount < 255) ultrasonicFailCount++;

    // Only warn after several consecutive failures. A single dropped echo is
    // completely normal and must not alarm the operator.
    if (ultrasonicFailCount == ULTRASONIC_FAIL_COUNT) {
      Serial.println(F("EVENT:ULTRASONIC_NO_ECHO"));
      dbg(F("HC-SR04 not echoing - whiskers remain active"));
    }
    if (ultrasonicPresent &&
        ultrasonicFailCount >= (ULTRASONIC_FAIL_COUNT * 2)) {
      ultrasonicPresent = false;   // flag it, but keep trying to recover
    }
    return;
  }

  // ---- VALID reading ----
  if (!ultrasonicPresent) {
    Serial.println(F("EVENT:ULTRASONIC_RESTORED"));
    dbg(F("HC-SR04 responding again"));
  }
  ultrasonicPresent    = true;
  ultrasonicFailCount  = 0;

  if (ultrasonicSmoothedCm < 0.0f) ultrasonicSmoothedCm = cm;
  else ultrasonicSmoothedCm = (ultrasonicSmoothedCm * 0.6f) + (cm * 0.4f);

  ultrasonicDistanceCm = cm;

  // Debug output for bench testing, throttled to twice a second.
  static uint32_t lastPrintMs = 0;
  if (elapsed(lastPrintMs, 500)) {
    lastPrintMs = nowMs();
    Serial.print(F("[ULTRA] "));
    Serial.print(cm, 1);
    Serial.print(F(" cm  (smoothed "));
    Serial.print(ultrasonicSmoothedCm, 1);
    Serial.println(F(" cm)"));
  }
}

/* ==========================================================================
 *  Helper predicates
 * ========================================================================== */

/**
 * Is the ultrasonic reporting a genuine obstacle right now?
 *
 * Requires a VALID reading at or below the threshold. An invalid reading
 * returns false, because "I cannot see" must never be treated as "I see an
 * obstacle" -- and equally must never be treated as "the path is clear".
 * The whiskers cover that gap.
 */
inline bool ultrasonicObstacleDetected() {
  if (!ultrasonicPresent)           return false;
  if (ultrasonicSmoothedCm < 0.0f)  return false;
  return (ultrasonicSmoothedCm <= ULTRASONIC_THRESHOLD_CM);
}

/** Is anything at all blocking us, from EITHER sensor? */
inline bool obstacleDetected() {
  return ultrasonicObstacleDetected() ||
         obstacleLeft || obstacleRight || obstacleRear;
}

#endif // BIOMOUSE_ULTRASONIC_H