/* ============================================================================
 *  MODULE [6] : BATTERY MONITORING
 * ----------------------------------------------------------------------------
 *  Reads a resistive divider on PIN_BATTERY_SENSE and converts it to volts and
 *  then to a percentage of a 3S Li-ion pack.
 *
 *  The reading is averaged over 10 samples and then smoothed again, because a
 *  single ADC read on a motorised robot is very noisy (the motors' back-EMF
 *  pulls the supply around) and a jittering battery bar looks broken.
 *
 *  Below BATTERY_LOW_PERCENT the buzzer starts a slow repeating pattern and an
 *  EVENT:BATTERY_LOW is reported. Hysteresis stops it buzzing on and off when
 *  the pack sits exactly at the threshold.
 * ========================================================================== */
#ifndef BIOMOUSE_BATTERY_H
#define BIOMOUSE_BATTERY_H

#include "state.h"
#include "buzzer.h"

inline void batteryService() {
  static uint32_t lastReadMs = 0;
  if (!elapsed(lastReadMs, 1000)) return;      // once per second is plenty
  lastReadMs = nowMs();

  // Average 10 rapid reads to reject ADC noise.
  long acc = 0;
  for (uint8_t i = 0; i < 10; i++) acc += analogRead(PIN_BATTERY_SENSE);

  // Arduino ADC is 10-bit over the 5V reference.
  float volts = (acc / 10.0f) * (5.0f / 1023.0f) * BATTERY_DIVIDER_RATIO;

  // Smooth so the app's battery bar is stable.
  batteryVolts = (batteryVolts * 0.7f) + (volts * 0.3f);

  // Map volts onto 0-100%, clamped so a detached sensor cannot report nonsense.
  batteryPercent = constrain(
      ((batteryVolts - BATTERY_EMPTY_VOLTS) /
       (BATTERY_FULL_VOLTS - BATTERY_EMPTY_VOLTS)) * 100.0f, 0.0f, 100.0f);

  if (batteryPercent <= BATTERY_LOW_PERCENT && !batteryLowAnnounced) {
    batteryLowAnnounced = true;
    Serial.println(F("EVENT:BATTERY_LOW"));
    buzzerStartRepeating(BEEP_LONG_MS, 3000);
  } else if (batteryPercent > (BATTERY_LOW_PERCENT + 5.0f)) {
    // 5% of hysteresis, otherwise it re-triggers constantly near the limit.
    batteryLowAnnounced = false;
  }
}

#endif // BIOMOUSE_BATTERY_H