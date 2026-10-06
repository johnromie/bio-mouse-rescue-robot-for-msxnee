/* ============================================================================
 *  MODULE [3] : THERMAL-GUIDED NAVIGATION
 * ----------------------------------------------------------------------------
 *  Converts the current thermal frame into exactly ONE movement command.
 *  This is the whole autonomous decision, kept in one function so the
 *  behaviour can be read and tuned at a glance.
 *
 *    strongest heat LEFT   -> steer LEFT
 *    strongest heat CENTER -> move FORWARD
 *    strongest heat RIGHT  -> steer RIGHT
 *    no heat at all        -> SCAN (pivot to sweep the array and search)
 *    close-range heat      -> STOP (an alert is raised in thermal.h)
 *    sensor missing        -> STOP (never drive blind)
 * ========================================================================== */
#ifndef BIOMOUSE_NAVIGATION_H
#define BIOMOUSE_NAVIGATION_H

#include "state.h"

inline MoveCommand thermalNavigationDecision() {
  // A missing sensor or an active alert both mean: do not move.
  if (!sensorReady || alertActive) return CMD_STOP;

  // Nothing warm in view. Pivot slowly to sweep the array rather than
  // driving forward into an unknown space.
  if (heatRise < HEAT_RISE_THRESHOLD) return CMD_SPIN_LEFT;

  if (heatDirection == 0) return CMD_LEFT;
  if (heatDirection == 2) return CMD_RIGHT;
  return CMD_FORWARD;
}

#endif // BIOMOUSE_NAVIGATION_H