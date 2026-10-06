/* ============================================================================
 *  WI-FI SETUP
 * ----------------------------------------------------------------------------
 *  TWO MODES, chosen in config.h:
 *
 *  ACCESS POINT (default, recommended for a rescue robot)
 *    The ESP32-CAM creates its own network, typically "BIO-MOUSE-A1B2".
 *    The phone joins that network and talks to the robot directly at
 *    http://192.168.4.1 -- no router, no internet, no PC required.
 *    This is the right default because a rescue site often has no working
 *    router or internet connection at all.
 *
 *  STATION
 *    The ESP32-CAM joins your existing router. Use this when developing, or
 *    when the phone is already on your network. If the router cannot be
 *    reached, the code falls back to bringing up the AP so the robot is
 *    still reachable rather than becoming a brick.
 * ========================================================================== */
#ifndef BIOMOUSE_ESP_WIFI_H
#define BIOMOUSE_ESP_WIFI_H

#include "config.h"
#include <WiFi.h>

// Last four hex digits of the chip MAC, so several robots can be told apart.
inline String uniqueApSuffix() {
  uint64_t chipId = ESP.getEfuseMac();
  char buf[8];
  snprintf(buf, sizeof(buf), "%04X", (uint16_t)(chipId & 0xFFFF));
  return String(buf);
}

/* ---- Access Point ------------------------------------------------------- */
inline void startAccessPoint(const char *fallbackName = nullptr) {
  String ssid = String(AP_SSID_PREFIX) + "-" + uniqueApSuffix();
  if (fallbackName) ssid = String(fallbackName) + "-" + uniqueApSuffix();

  const char *pass = fallbackName ? STATION_FALLBACK_PASSWORD : AP_PASSWORD;

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1),
                    IPAddress(255,255,255,0));
  bool ok = (strlen(pass) >= 8)
              ? WiFi.softAP(ssid.c_str(), pass)
              : WiFi.softAP(ssid.c_str());
  if (!ok) {
    Serial.println(F("[WIFI] softAP FAILED"));
    return;
  }

  Serial.println(F(""));
  Serial.println(F("============================================"));
  Serial.print  (F("  BIO-MOUSE access point is up\n"));
  Serial.print  (F("  SSID      : ")); Serial.println(ssid);
  Serial.print  (F("  Password  : ")); Serial.println(pass);
  Serial.print  (F("  Connect the phone to that SSID, then open:\n"));
  Serial.println(F("      http://192.168.4.1"));
  Serial.println(F("============================================"));
  Serial.println(F(""));
}

/* ---- Station mode ------------------------------------------------------- */
inline bool startStation() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(STATION_SSID, STATION_PASSWORD);

  Serial.print(F("[WIFI] joining '"));
  Serial.print(STATION_SSID);
  Serial.println(F("' ..."));

  uint8_t attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < STATION_MAX_RETRIES) {
    delay(500);
    attempts++;
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("[WIFI] connected, IP = "));
    Serial.println(WiFi.localIP());
    return true;
  }

  // Fall back to the AP so the robot stays reachable.
  Serial.println(F("[WIFI] station failed, falling back to AP mode"));
  WiFi.disconnect(true);
  startAccessPoint(STATION_FALLBACK_SSID);
  return false;
}

/* ---- Single entry point ------------------------------------------------- */
inline void wifiBegin() {
  if (USE_STATION_MODE) startStation();
  else                   startAccessPoint();
}

/* True while the Wi-Fi interface has an IP the app can reach. */
inline bool wifiIsUp() {
  return (USE_STATION_MODE ? (WiFi.status() == WL_CONNECTED)
                           : (WiFi.softAPgetStationNum() >= 0));
}

/* The address the phone should use. */
inline String wifiAddress() {
  if (USE_STATION_MODE && WiFi.status() == WL_CONNECTED)
    return WiFi.localIP().toString();
  return String("192.168.4.1");
}

#endif // BIOMOUSE_ESP_WIFI_H