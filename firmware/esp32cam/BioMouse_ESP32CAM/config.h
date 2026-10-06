/* ============================================================================
 *  BIO-MOUSE RESCUE ROBOT  --  ESP32-CAM config.h
 * ----------------------------------------------------------------------------
 *  Every pin, Wi-Fi setting and timeout for the ESP32-CAM board.
 *  >>> VERIFY EVERY PIN AGAINST YOUR OWN WIRING BEFORE FIRST POWER-UP. <<<
 * ========================================================================== */
#ifndef BIOMOUSE_ESP_CONFIG_H
#define BIOMOUSE_ESP_CONFIG_H

#include <Arduino.h>

// The CAMERA_FRAME_SIZE constant below is declared as a framesize_t, which comes
// from the camera driver. Including it here lets config.h stay self-contained
// instead of relying on camera.h being included first.
#include <esp_camera.h>

/* ==========================================================================
 *  BOARD SELECTION
 * ==========================================================================
 *  Uncomment EXACTLY ONE line for your board.
 *    "cam_esp32c"  the common AI-Thinker ESP32-CAM (OV2640, 4MB flash + PSRAM)
 * ========================================================================== */
// #define BOARD_MODEL "cam_esp32c"
#define BOARD_MODEL "cam_esp32c"

/* ==========================================================================
 *  UART : ESP32-CAM  <-->  ARDUINO
 * ==========================================================================
 *  >>> READ THIS CAREFULLY, IT IS THE #1 SOURCE OF "IT DOESN'T WORK" <<<
 *
 *  WHICH UART
 *    UART0 (Serial) is the USB / programmer port -- do NOT use it for Arduino.
 *    UART2 is used here. The RX / TX pins are configurable below.
 *
 *  PIN CHOICE ON THE AI-THINKER ESP32-CAM
 *    Most GPIOs are already taken:
 *      GPIO0,5,18,19,21,22,23,25,26,27,32,34,35,36,39  -> camera
 *      GPIO15                                             -> camera RESET
 *      GPIO16                                             -> module PSRAM
 *      GPIO6..GPIO11                                      -> SPI flash
 *      GPIO2,4,12,13,14                                   -> microSD slot
 *    That leaves GPIO1 and GPIO3 (the UART0 programmer port) and the SD-card
 *    pins. GPIO13 / GPIO14 are used below, which is ONLY correct if you are
 *    not using the microSD card. If you DO use microSD, move them to
 *    another free pair such as GPIO4 / GPIO12.
 *
 *  WIRING (crossed over, and share a common ground!)
 *    ESP32-CAM GPIO13 (TX)  -->  Arduino D1  (RX)
 *    ESP32-CAM GPIO14 (RX)  <--  Arduino D0  (TX)
 *    ESP32-CAM GND          ---  Arduino GND
 *
 *  IMPORTANT: the ESP32 is 3.3V logic. Most Arduino UNO / Nano boards are 5V
 *  logic. Connecting the ESP32 RX pin directly to a 5V Arduino TX pin can
 *  damage the ESP32. Use a level shifter, or power the Arduino's logic at 3.3V.
 * ========================================================================== */
const uint8_t PIN_UART_TX = 13;   // ESP32-CAM TX  -> Arduino RX
const uint8_t PIN_UART_RX = 14;   // ESP32-CAM RX  <- Arduino TX

const uint32_t UART_BAUD            = 115200;
#define ARDUINO_UART Serial2        // UART2

/* How often to ping the Arduino. MUST be comfortably faster than the
 * Arduino's FAILSAFE_TIMEOUT_MS (3000 ms) or the robot will stop itself.
 * This ping is what keeps the Arduino's watchdog fed while the robot runs. */
const uint32_t PING_INTERVAL_MS = 500;

/* If the Arduino stops sending telemetry for this long, report the robot as
 * offline in the app. This is the app's view of CONNECTION_STATUS. */
const uint32_t ROBOT_OFFLINE_TIMEOUT_MS = 3000;

/* Grace period after the last phone disconnects, before the ESP32-CAM
 * commands a STOP on the operator's behalf. This prevents the robot from
 * continuing to drive in MANUAL mode with nobody at the controls.
 * Set to 0 to stop the instant the last client goes away. */
const uint32_t APP_DISCONNECT_STOP_MS = 0;

/* ==========================================================================
 *  WI-FI
 * ==========================================================================
 *  DEFAULT MODE IS ACCESS POINT (AP).
 *  The robot creates its own network, so it works with no router, no
 *  internet and no PC -- which is what you want in a debris field.
 *
 *  The phone joins "BIO-MOUSE-XXXX" and reaches the robot at 192.168.4.1.
 *
 *  To use an existing router instead, set USE_STATION_MODE = true and fill in
 *  the SSID / PASSWORD below.
 * ========================================================================== */
const bool USE_STATION_MODE = false;

// ---- Access Point (used when USE_STATION_MODE == false) -------------------
const char* AP_SSID_PREFIX = "BIO-MOUSE";    // final name is BIO-MOUSE-XXXX
const char* AP_PASSWORD    = "biomouse123";  // >= 8 chars, or "" for open
const uint8_t AP_CHANNEL   = 1;

// ---- Station mode (used when USE_STATION_MODE == true) ------------------
const char* STATION_SSID         = "YOUR_WIFI_NAME";
const char* STATION_PASSWORD     = "YOUR_WIFI_PASSWORD";
const uint8_t STATION_MAX_RETRIES = 20;

/* If the router cannot be reached after STATION_MAX_RETRIES, bring the AP up
 * anyway so the robot stays reachable instead of disappearing. */
const char* STATION_FALLBACK_SSID     = "BIO-MOUSE-SETUP";
const char* STATION_FALLBACK_PASSWORD = "biomouse123";

/* ==========================================================================
 *  HTTP SERVER
 * ==========================================================================
 *  Endpoints served to the mobile app:
 *    GET  /              control-panel web page (lets you test from a browser)
 *    GET  /api/status    JSON snapshot of every robot status value
 *    GET  /api/cmd?c=..  send a command (e.g. /api/cmd?c=FORWARD)
 *    POST /api/cmd       send a command as JSON {"command":"FORWARD","value":60}
 *    GET  /api/camera    live MJPEG video stream
 *    GET  /api/snapshot  single JPEG still frame
 *    GET  /api/ping      connectivity check
 * ========================================================================== */
const uint16_t HTTP_PORT = 80;

/* ==========================================================================
 *  CAMERA
 * ========================================================================== */
// Small default frame size. Raise it for better thermal-guided alignment,
// lower it to QVGA if the stream is unstable on a long link.
const framesize_t CAMERA_FRAME_SIZE = FRAMESIZE_VGA;   // 640x480
const int         CAMERA_QUALITY    = 12;              // 10 = best, 63 = worst

/* ==========================================================================
 *  ROBOT STATUS  --  the EXACT fields the mobile app reads
 * ==========================================================================
 *  See docs/PROTOCOL.md for the full specification.
 * ========================================================================== */
struct RobotStatus {
  bool     robotOnline       = false;   // Arduino heartbeat seen
  bool     linkLost          = true;    // Arduino reported link loss
  char     mode[8]           = "MANUAL";
  float    temperature       = 0.0f;    // TEMPERATURE
  float    ambient           = 0.0f;    // ambient reference
  char     thermalStatus[24] = "NONE";  // THERMAL_STATUS
  char     heatDirection[8]  = "NONE";  // strongest-heat sector
  char     obstacle[8]       = "CLEAR"; // OBSTACLE summary
  bool     obstacleLeft      = false;   // OBSTACLE_LEFT
  bool     obstacleRight     = false;   // OBSTACLE_RIGHT
  bool     obstacleRear      = false;
  // ---- NEW: HC-SR04 ultrasonic front distance ----
  float    distanceCm        = -1.0f;   // DISTANCE, -1 when no valid echo
  int      distanceThreshold = 30;      // DISTANCE_THRESHOLD, in cm
  bool     distanceAlert     = false;   // DISTANCE_ALERT: true when <= threshold
  char     ultrasonic[8]     = "READY"; // ULTRASONIC: READY | NO_ECHO
  char     avoidState[12]    = "IDLE";  // AVOID:IDLE/REACTING/STOPPED/...
  int      battery           = 0;       // BATTERY percent
  char     buzzer[4]         = "OFF";   // BUZZER_STATUS
  char     move[12]          = "STOP";  // last movement command
  bool     alert             = false;   // thermal alert active
  char     sensor[8]         = "MISSING";
  uint8_t  speed             = 55;      // SPEED setting
  char     lastStopReason[24] = "";
  uint32_t cmdsReceived      = 0;
  uint32_t lastRxMs          = 0;
};

#endif // BIOMOUSE_ESP_CONFIG_H