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

inline void logApClientEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
    Serial.println(F("[WIFI] phone associated; waiting for DHCP lease"));
  } else if (event == ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED) {
    Serial.print(F("[WIFI] DHCP lease assigned: "));
    Serial.println(IPAddress(info.wifi_ap_staipassigned.ip.addr));
  }
}

/* ---- Access Point ------------------------------------------------------- */
inline void startAccessPoint(const char *fallbackName = nullptr) {
  String ssid = String(AP_SSID_PREFIX) + "-" + uniqueApSuffix();
  if (fallbackName) ssid = String(fallbackName) + "-" + uniqueApSuffix();

  const char *pass = fallbackName ? STATION_FALLBACK_PASSWORD : AP_PASSWORD;

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  const IPAddress apIp(192, 168, 4, 1);
  const IPAddress subnet(255, 255, 255, 0);
  // Configure the AP interface before starting its DHCP server. Let the
  // Arduino core choose the default lease pool instead of overriding it.
  if (!WiFi.softAPConfig(apIp, apIp, subnet)) {
    Serial.println(F("[WIFI] AP IP configuration FAILED"));
    return;
  }

  const bool ok = (strlen(pass) >= 8)
                    ? WiFi.softAP(ssid.c_str(), pass, 1, 0, 4)
                    : WiFi.softAP(ssid.c_str(), nullptr, 1, 0, 4);
  if (!ok) {
    Serial.println(F("[WIFI] softAP FAILED"));
    return;
  }

  Serial.println(F(""));
  Serial.println(F("============================================"));
  Serial.print  (F("  BIO-MOUSE access point is up\n"));
  Serial.print  (F("  SSID      : ")); Serial.println(ssid);
  Serial.print  (F("  Password  : ")); Serial.println(pass);
  Serial.print  (F("  AP IP     : ")); Serial.println(WiFi.softAPIP());
  Serial.println(F("  DHCP      : automatic"));
  Serial.print  (F("  Connect the phone to that SSID, then open:\n"));
  Serial.println(F("      http://192.168.4.1"));
  Serial.println(F("============================================"));
  Serial.println(F(""));
}

/* ---- Station mode ------------------------------------------------------- */
inline bool startStation() {
  WiFi.mode(WIFI_STA);
  // Keep the radio awake during association. This can help with phone
  // hotspots that otherwise take longer to respond than a home router.
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  Serial.print(F("[WIFI] checking for hotspot '"));
  Serial.print(STATION_SSID);
  Serial.println(F("' ..."));
  int networkCount = WiFi.scanNetworks();
  bool targetFound = false;
  for (int i = 0; i < networkCount; ++i) {
    if (WiFi.SSID(i) == STATION_SSID) {
      targetFound = true;
      Serial.print(F("[WIFI] hotspot visible, channel="));
      Serial.print(WiFi.channel(i));
      Serial.print(F(" RSSI="));
      Serial.println(WiFi.RSSI(i));
      break;
    }
  }
  if (!targetFound) {
    Serial.println(F("[WIFI] hotspot not found; check its name, 2.4 GHz, and that hotspot is on"));
  }
  WiFi.scanDelete();

  WiFi.begin(STATION_SSID, STATION_PASSWORD);

  Serial.print(F("[WIFI] joining '"));
  Serial.print(STATION_SSID);
  Serial.println(F("' ..."));

  uint16_t attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < STATION_MAX_RETRIES) {
    delay(500);
    attempts++;
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("[WIFI] connected, IP = "));
    Serial.println(WiFi.localIP());
    Serial.print(F("[WIFI] gateway = "));
    Serial.println(WiFi.gatewayIP());
    return true;
  }

  // Fall back to the AP so the robot stays reachable.
  Serial.print(F("[WIFI] station failed; status="));
  Serial.print((int)WiFi.status());
  Serial.println(F(" (1=no SSID, 4=auth/connect failed, 6=disconnected); falling back to AP mode"));
  WiFi.disconnect(true);
  startAccessPoint(STATION_FALLBACK_SSID);
  return false;
}

/* ---- Single entry point ------------------------------------------------- */
inline void wifiBegin() {
  WiFi.onEvent(logApClientEvent, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
  WiFi.onEvent(logApClientEvent, ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED);
  if (USE_STATION_MODE) startStation();
  else                   startAccessPoint();
}

/* True while the Wi-Fi interface has an IP the app can reach. */
inline bool wifiIsUp() {
  wifi_mode_t mode = WiFi.getMode();
  if ((mode & WIFI_STA) && WiFi.status() == WL_CONNECTED) return true;
  // Station mode may fall back to its setup AP. Check the active interface,
  // rather than the compile-time preference, so the fallback is reported up.
  if (mode & WIFI_AP) return WiFi.softAPIP() != IPAddress(0, 0, 0, 0);
  return false;
}

/* The address the phone should use. */
inline String wifiAddress() {
  wifi_mode_t mode = WiFi.getMode();
  if ((mode & WIFI_STA) && WiFi.status() == WL_CONNECTED)
    return WiFi.localIP().toString();
  if (mode & WIFI_AP) return WiFi.softAPIP().toString();
  return String("0.0.0.0");
}

#endif // BIOMOUSE_ESP_WIFI_H
