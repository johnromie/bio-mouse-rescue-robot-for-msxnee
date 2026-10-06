/* ============================================================================
 *  BIO-MOUSE RESCUE ROBOT  --  ESP32-CAM MAIN SKETCH  (v1.0.0)
 * ----------------------------------------------------------------------------
 *  WHAT THIS BOARD IS RESPONSIBLE FOR
 *    Wi-Fi, the camera, and the bridge between the mobile app and the
 *    Arduino. It does NOT drive the motors. All motor decisions, thermal
 *    navigation and obstacle avoidance happen on the Arduino, which keeps
 *    the safety-critical logic independent of Wi-Fi reliability.
 *
 *  FILES
 *    config.h        every pin, Wi-Fi setting and timeout -- EDIT THIS FIRST
 *    wifi_manager.h  AP / station setup
 *    uart_bridge.h   UART link to the Arduino, parses telemetry into state
 *    camera.h        OV2640 initialisation
 *    http_api.h      HTTP endpoints that the mobile app calls
 *
 *  HOW A COMMAND TRAVELS FROM THE APP TO THE MOTORS
 *    phone -> Wi-Fi HTTP GET /api/cmd?c=FORWARD
 *          -> http_api.h  translates + whitelists it
 *          -> uart_bridge.h writes "CMD:FORWARD\n" to UART2
 *          -> Arduino uart.h parses it
 *          -> motor.h sets IN1..IN4 + ENA/ENB
 *          -> L298N turns the motors
 *
 *  REQUIREMENTS
 *    Arduino core "esp32" 2.0.x or later (ESP32 by Espressif Systems)
 *    Board package: esp32
 *    Upload speed: 115200 or higher. This board has little flash.
 *
 *  SAFETY DISCLAIMER
 *    Academic / prototype rescue robot. It assists a search; it does not
 *    replace a trained rescue team. The firmware reports HEAT, never a
 *    confirmed person.
 * ========================================================================= */

#include "config.h"
#include "wifi_manager.h"
#include "uart_bridge.h"
#include "camera.h"
#include "http_api.h"

/* ==========================================================================
 *  SETUP
 * ========================================================================== */
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println(F("============================================"));
  Serial.println(F("  BIO-MOUSE RESCUE ROBOT - ESP32-CAM v1.0.0"));
  Serial.println(F("============================================"));

  // ---- Wi-Fi first: the app can only connect once this is up -------------
  wifiBegin();

  // ---- Camera. Not fatal: the robot still works as a thermal rover
  //      without video, so a camera failure must not stop the firmware. ----
  if (!cameraBegin()) {
    Serial.println(F("[CAMERA] unavailable - video disabled, control still OK"));
  }

  // ---- UART to the Arduino ----------------------------------------------
  uartBridgeBegin();
  Serial.print(F("[UART] TX=GPIO"));
  Serial.print(PIN_UART_TX);
  Serial.print(F(" RX=GPIO"));
  Serial.print(PIN_UART_RX);
  Serial.print(F(" @ "));
  Serial.print(UART_BAUD);
  Serial.println(F(" baud"));

  // ---- HTTP API (this is what the app talks to) --------------------------
  httpBegin();

  Serial.println(F("[READY] Open the address shown above in your browser,"));
  Serial.println(F("        or point the BIO-MOUSE app at it."));
  Serial.println(F("============================================"));
  Serial.println();
}

/* ==========================================================================
 *  LOOP
 * ==========================================================================
 *  Order matters:
 *    1. Ping the Arduino FIRST, every single pass. This is what keeps the
 *       Arduino's failsafe watchdog fed, and therefore what keeps the robot
 *       alive. It must not be conditional on anything else.
 *    2. Drain the UART so telemetry stays current.
 *    3. Service HTTP.
 *
 *  Extra safety: if the phone disconnects while the robot is under MANUAL
 *  drive, command a STOP. Without this the robot would keep driving with
 *  nobody at the controls, which is exactly the accident this project exists
 *  to prevent.
 * ========================================================================== */
void loop() {
  // [1] Keep-alive to the Arduino. Highest priority.
  uartBridgePingService();

  // [2] Drain telemetry from the Arduino.
  uartBridgeService();

  // [3] Serve the app.
  server.handleClient();

  // ---- Safety: no operator connected -> stop driving ---------------------
  static bool     wasConnected = true;
  static uint32_t lastClientSeenMs = 0;

  // ESP32 core 3.x removed WebServer::hasClient(). server.client() returns the
  // NetworkClient, and WiFiClient::connected() reports whether it is still there.
  if (server.client().connected()) {
    lastClientSeenMs = millis();
    wasConnected = true;
  } else if (wasConnected &&
             (uint32_t)(millis() - lastClientSeenMs) >= APP_DISCONNECT_STOP_MS) {
    // Only stop if the robot was actually under operator control. Leaving
    // AUTO mode running is fine, because the Arduino's own safety rules still
    // apply and the operator can switch back to MANUAL.
    if (!strcmp(robot.mode, "MANUAL")) {
      Serial.println(F("[SAFETY] operator disconnected -> STOP"));
      cmdStop();
    }
    wasConnected = false;
  }
}