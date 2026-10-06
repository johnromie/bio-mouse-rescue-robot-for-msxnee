/* ============================================================================
 *  MODULE [8] : SAFETY / EMERGENCY STOP
 * ----------------------------------------------------------------------------
 *  Three independent layers, each able to stop the robot on its own:
 *    1. App-issued STOP          -> handled in uart.h (works in BOTH modes)
 *    2. E-STOP button            -> this file, immediate and latched
 *    3. Failsafe watchdog        -> this file, stops when the link goes quiet
 *    4. Close-range thermal alert-> thermal.h, calls safetyStop()
 *
 *  Every stop goes through safetyStop() so that each one is logged on the
 *  serial console AND reported to the mobile app as ROBOT:STOPPED:<reason>.
 * ========================================================================== */
#ifndef BIOMOUSE_SAFETY_H
#define BIOMOUSE_SAFETY_H

#include "state.h"
#include "motor.h"

// Read the physical E-STOP button.
inline bool readEstopButton() {
  if (PIN_ESTOP_BUTTON < 0) return false;
  int raw = digitalRead(PIN_ESTOP_BUTTON);
  return ESTOP_ACTIVE_LOW ? (raw == LOW) : (raw == HIGH);
}

// Latched, logged stop. `reason` reaches the app as ROBOT:STOPPED:<reason>.
inline void safetyStop(const char *reason) {
  motorsHardStop();
  activeCommand = CMD_STOP;
  Serial.print(F("ROBOT:STOPPED:"));
  Serial.println(reason);
  dbg(F("SAFETY STOP"));
}

// Watchdog + E-STOP polling. Called every loop iteration.
//
// FAILSAFE: the ESP32-CAM pings this board regularly (see PING_INTERVAL_MS in
// uart.h). If nothing valid arrives for FAILSAFE_TIMEOUT_MS, the phone, the
// Wi-Fi link or the UART has failed, so the robot stops. This is the single
// most important safety behaviour in the whole system: without it, a dropped
// phone link would leave the robot driving unattended.
inline void safetyService() {
  static bool estopWasPressed = false;
  bool pressed = readEstopButton();

  if (pressed) {
    if (!estopWasPressed) {
      estopWasPressed = true;
      failsafeTriggered = true;          // latch until the link is restored
      Serial.println(F("EVENT:ESTOP_BUTTON"));
      safetyStop("ESTOP_BUTTON");
    }
    return;                              // stay stopped while it is held
  }
  estopWasPressed = false;

  if (!failsafeTriggered && elapsed(lastCommandRxMs, FAILSAFE_TIMEOUT_MS)) {
    failsafeTriggered = true;
    Serial.println(F("EVENT:FAILSAFE_LINK_LOST"));
    safetyStop("FAILSAFE_LINK_LOST");
  }
}

#endif // BIOMOUSE_SAFETY_H