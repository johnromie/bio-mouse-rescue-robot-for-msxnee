/* ============================================================================
 *  MODULE [2] : AMG8833 THERMAL SENSING
 * ----------------------------------------------------------------------------
 *  Reads the AMG8833 8x8 usable array and reduces each frame to three sector
 *  averages (LEFT / CENTER / RIGHT).
 *
 *  AMBIENT TRACKING
 *  The AMG8833 reports absolute temperature, but in a rescue scenario the room
 *  itself is the reference, not 25 degrees. So "ambient" is tracked as a
 *  rolling minimum of the frame: if the coldest pixel falls, ambient drops
 *  immediately; if it rises, ambient creeps up slowly. Every decision is then
 *  a comparison of (pixel - ambient), which is what actually matters.
 *
 *  DIRECTION CONFIRMATION
 *  A single noisy frame must not be able to swing the robot the wrong way, so
 *  a new strongest sector has to win HEAT_CONFIRM_FRAMES times in a row before
 *  heatDirection is allowed to change.
 *
 *  LANGUAGE POLICY  --  PLEASE READ
 *  This code NEVER reports that a person has been found. It reports
 *  HEAT_DETECTED or POSSIBLE_SURVIVOR, which are statements about TEMPERATURE
 *  only. An engine, an animal, a fire or a heated wall would all produce the
 *  same reading. "POSSIBLE_SURVIVOR" is deliberately hedged, and the mobile
 *  app should present it the same way.
 * ========================================================================== */
#ifndef BIOMOUSE_THERMAL_H
#define BIOMOUSE_THERMAL_H

#include <Wire.h>
// The library that ships as "Adafruit AMG88xx Library" and provides the
// Adafruit_AMG88xx class. NOTE: there is NO separate "Adafruit AMG8833"
// library -- AMG8833 is a model of the same AMG88xx family, driven by this
// single driver.
#include <Adafruit_AMG88xx.h>
#include "state.h"
#include "motor.h"
#include "safety.h"
#include "buzzer.h"

// The library exposes a 64-element pixel array (8x8), not a readPixel(x,y)
// accessor, so the frame is read into a buffer and indexed manually.
static Adafruit_AMG88xx thermalSensor;
static float thermalPixels[AMG88xx_PIXEL_ARRAY_SIZE];   // 64 floats = 256 bytes

// Bring the sensor up. Returns false if it does not answer on the I2C bus.
inline bool thermalInit() {
  // begin(address) -- the address is passed as the first argument, and the
  // optional power-on flag is not used here.
  if (!thermalSensor.begin(AMG8833_I2C_ADDRESS)) {
    sensorReady = false;
    Serial.println(F("EVENT:THERMAL_SENSOR_MISSING"));
    dbg(F("AMG88xx not found - check I2C wiring"));
    return false;
  }
  sensorReady = true;
  Serial.println(F("EVENT:THERMAL_SENSOR_READY"));
  return true;
}

inline const char* sectorName(uint8_t idx) {
  return (idx == 0) ? "LEFT" : (idx == 1) ? "CENTER" : "RIGHT";
}

// Alert = hard stop + repeating buzzer + message to the app.
// This is the "strong AND persistent heat at close range -> STOP and alert"
// requirement from the specification.
inline void triggerThermalAlert() {
  alertActive = true;
  safetyStop("CLOSE_HEAT_ALERT");
  buzzerStartRepeating(ALERT_BEEP_MS, ALERT_BEEP_MS * 2);
  Serial.println(F("THERMAL:POSSIBLE_SURVIVOR"));
  Serial.println(F("EVENT:ALERT_HEAT_DETECTED"));
  dbg(F("ALERT - close heat, robot stopped"));
}

// Called when the heat goes away. Leaves the robot stopped; the operator has
// to drive it again, which is the safe behaviour.
inline void clearThermalAlert() {
  if (!alertActive) return;
  alertActive = false;
  buzzerOff();
  Serial.println(F("EVENT:ALERT_CLEARED"));
  dbg(F("thermal alert cleared"));
}

inline void thermalService() {
  if (!sensorReady) return;
  if (!elapsed(lastThermalReadMs, THERMAL_INTERVAL_MS)) return;
  lastThermalReadMs = nowMs();

  float frameMin = 1000.0f, frameMax = -1000.0f;
  float sums[3]     = { 0, 0, 0 };
  uint8_t counts[3] = { 0, 0, 0 };

  // ---- Read the grid ------------------------------------------------------
  // ---- Read the whole frame once into thermalPixels ----
  // readPixels() fills the 64-element buffer in row-major order:
  //   index = (row * GRID_COLS) + column
  thermalSensor.readPixels(thermalPixels, AMG88xx_PIXEL_ARRAY_SIZE);

  for (uint8_t y = 0; y < GRID_ROWS; y++) {
    for (uint8_t x = 0; x < GRID_COLS; x++) {
      float t = thermalPixels[(y * GRID_COLS) + x];

      // Reject implausible values (NaN, I2C noise, sensor self-heating).
      if (isnan(t) || t < -20.0f || t > MAX_VALID_TEMP) t = ambientTemp;

      frameMin = min(frameMin, t);
      frameMax = max(frameMax, t);

      uint8_t sector;
      if      (x >= LEFT_COL_START   && x <= LEFT_COL_END)   sector = 0;
      else if (x >= CENTER_COL_START && x <= CENTER_COL_END) sector = 1;
      else                                                    sector = 2;
      sums[sector]   += t;
      counts[sector] += 1;
    }
  }

  for (uint8_t s = 0; s < 3; s++)
    sectorTemp[s] = counts[s] ? (sums[s] / counts[s]) : frameMin;

  rawMaxTemp = frameMax;

  // ---- Ambient tracking ---------------------------------------------------
  // Snap down to a new minimum; creep up otherwise.
  if (frameMin < ambientTemp) {
    ambientTemp = frameMin;
  } else {
    ambientTemp += AMBIENT_TRACK_ALPHA * (frameMax - ambientTemp);
  }

  heatRise = frameMax - ambientTemp;

  // ---- Strongest sector, with confirmation --------------------------------
  uint8_t best = 0;
  for (uint8_t s = 1; s < 3; s++) if (sectorTemp[s] > sectorTemp[best]) best = s;

  if (best == confirmedDirection) {
    if (directionFrames < 255) directionFrames++;
  } else {
    confirmedDirection = best;
    directionFrames = 1;
  }
  if (directionFrames >= HEAT_CONFIRM_FRAMES) heatDirection = confirmedDirection;

  // ---- Classification. Heat terms only, never an identity claim. ----------
  if      (heatRise >= SURVIVOR_RISE_THRESHOLD) thermalStatus = "POSSIBLE_SURVIVOR";
  else if (heatRise >= HEAT_RISE_THRESHOLD)     thermalStatus = "HEAT_DETECTED";
  else                                           thermalStatus = "NONE";

  // ---- Close range: strong AND persistent ---------------------------------
  bool closeHeat = (frameMax >= CLOSE_RANGE_TEMP) &&
                   (heatRise >= HEAT_RISE_THRESHOLD);

  if (closeHeat) {
    if (!closeHeatPending) { closeHeatPending = true; closeHeatSinceMs = nowMs(); }
    if (!alertActive && elapsed(closeHeatSinceMs, CLOSE_HOLD_MS)) {
      triggerThermalAlert();
    }
  } else {
    closeHeatPending = false;
    if (alertActive) clearThermalAlert();
  }
}

#endif // BIOMOUSE_THERMAL_H