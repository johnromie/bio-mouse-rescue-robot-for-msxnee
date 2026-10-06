/* ============================================================================
 *  MODULE [5] : ACTIVE BUZZER
 * ----------------------------------------------------------------------------
 *  Wiring: +5V -> buzzer (+) and buzzer (-) -> GPIO pin. The pin only sinks
 *  current. If your buzzer draws more than ~15 mA (a sounder, not a small
 *  active buzzer), put a transistor driver between the pin and the buzzer.
 *
 *  Fully NON-BLOCKING: patterns are driven by buzzerService() using millis()
 *  timing rather than delay(). This matters because a blocking delay() in the
 *  buzzer would stop the UART listener from processing an emergency STOP.
 * ========================================================================== */
#ifndef BIOMOUSE_BUZZER_H
#define BIOMOUSE_BUZZER_H

#include "state.h"

inline void buzzerWrite(bool on) {
  digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
  buzzerActive = on;
}

// Silence everything and tell the app.
inline void buzzerOff() {
  buzzerWrite(false);
  buzzerRepeating = false;
  Serial.println(F("BUZZER:OFF"));
}

// One short beep (used by the self-test and by SCAN).
inline void buzzerBeepOnce(uint16_t durationMs) {
  buzzerRepeating    = false;
  buzzerOnMs         = durationMs;
  buzzerWrite(true);
  buzzerNextToggleMs = nowMs() + durationMs;
}

// Repeating pattern until buzzerOff() is called (alerts, manual ON).
inline void buzzerStartRepeating(uint16_t onMs, uint16_t periodMs) {
  buzzerRepeating    = true;
  buzzerOnMs         = onMs;
  buzzerPeriodMs     = max(onMs + BEEP_GAP_MS, periodMs);
  buzzerNextIsOn     = false;
  buzzerWrite(true);
  buzzerNextToggleMs = nowMs() + onMs;
}

// Advance the pattern. Call every loop iteration.
inline void buzzerService() {
  if (!buzzerActive) return;
  if (!isDue(buzzerNextToggleMs)) return;      // next toggle not due yet

  if (!buzzerRepeating) {
    buzzerWrite(false);                          // single beep finished
    return;
  }
  // Repeating: alternate ON and OFF every half period.
  buzzerNextIsOn = !buzzerNextIsOn;
  buzzerWrite(buzzerNextIsOn);
  buzzerNextToggleMs = nowMs() + (buzzerNextIsOn ? buzzerOnMs
                                                 : (buzzerPeriodMs - buzzerOnMs));
}

#endif // BIOMOUSE_BUZZER_H