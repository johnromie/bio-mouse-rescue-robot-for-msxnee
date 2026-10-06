/* ============================================================================
 *  HTTP API  --  THE INTERFACE YOUR MOBILE APP CALLS
 * ----------------------------------------------------------------------------
 *  ENDPOINTS
 *    GET  /                 control-panel web page (test from a phone browser)
 *    GET  /api/status       JSON: every robot status value
 *    GET  /api/cmd?c=CMD    send a command (e.g. /api/cmd?c=FORWARD)
 *    POST /api/cmd          send a command as JSON {"command":"SPEED","value":60}
 *    GET  /api/camera       live MJPEG video stream
 *    GET  /api/snapshot     single JPEG still frame
 *    GET  /api/ping         connectivity check
 *
 *  THE THREE STATUS VALUES THE APP NEEDS MAP TO THESE JSON KEYS:
 *    TEMPERATURE      -> "temperature"
 *    THERMAL_STATUS   -> "thermalStatus"
 *    OBSTACLE_LEFT    -> "obstacleLeft"
 *    OBSTACLE_RIGHT   -> "obstacleRight"
 *    BATTERY          -> "battery"
 *    ROBOT_STATUS     -> "robotOnline" / "robotStatus"
 *    BUZZER_STATUS    -> "buzzer"
 *    CONNECTION_STATUS-> "connection"
 *
 *  WHY GET AND POST BOTH EXIST FOR /api/cmd
 *    A GET is trivially testable in any browser or with curl. A POST with a
 *    JSON body is cleaner for the real app. Both are supported so you can
 *    test from a browser first and wire up the app later without changing
 *    the firmware.
 * ========================================================================== */
#ifndef BIOMOUSE_ESP_HTTP_H
#define BIOMOUSE_ESP_HTTP_H

// Self-contained on purpose: this header uses the camera framebuffer API and
// the Wi-Fi helpers, so it includes them itself rather than relying on the
// .ino including them in the right order first.
#include "config.h"
#include "uart_bridge.h"
#include "wifi_manager.h"
#include "camera.h"
#include <WebServer.h>
#include <WiFiClient.h>

WebServer server(HTTP_PORT);

/* ==========================================================================
 *  Small JSON helpers
 * ========================================================================== */
inline String boolJson(bool v) { return v ? "true" : "false"; }

// Extract "key" from a JSON body without a full parser. Good enough for the
// two shapes we accept, and avoids pulling in ArduinoJson for 2 fields.
inline String jsonField(const String &body, const char *key) {
  String needle = String("\"") + key + "\"";
  int k = body.indexOf(needle);
  if (k < 0) return "";
  int colon = body.indexOf(':', k + needle.length());
  if (colon < 0) return "";
  colon++;

  while (colon < body.length() && body[colon] == ' ') colon++;

  if (colon >= body.length()) return "";

  if (body[colon] == '"') {                       // string value
    int end = body.indexOf('"', colon + 1);
    if (end < 0) return "";
    return body.substring(colon + 1, end);
  }
  // numeric / boolean value: read until a comma or closing brace
  int end = colon;
  while (end < body.length() && body[end] != ',' && body[end] != '}' &&
         body[end] != ' ') end++;
  return body.substring(colon, end);
}

/* ==========================================================================
 *  GET /api/status
 * ========================================================================== */
inline void handleStatus() {
  const char* connection = !robot.robotOnline   ? "DISCONNECTED"
                           : robot.linkLost      ? "UNSTABLE"
                           : wifiIsUp()          ? "CONNECTED"
                                                  : "NO_WIFI";
  const char* robotStatus = !robot.robotOnline ? "OFFLINE"
                            : robot.alert       ? "ALERT"
                            : robot.linkLost    ? "FAILSAFE"
                                                : "ONLINE";

  String json = "{";
  json += "\"robotStatus\":\""    + String(robotStatus) + "\",";
  json += "\"connection\":\""     + String(connection) + "\",";
  json += "\"robotOnline\":"      + boolJson(robot.robotOnline) + ",";
  json += "\"linkLost\":"         + boolJson(robot.linkLost) + ",";
  json += "\"mode\":\""           + String(robot.mode) + "\",";
  json += "\"temperature\":"      + String(robot.temperature, 1) + ",";
  json += "\"ambient\":"          + String(robot.ambient, 1) + ",";
  json += "\"thermalStatus\":\""  + String(robot.thermalStatus) + "\",";
  json += "\"heatDirection\":\""  + String(robot.heatDirection) + "\",";
  json += "\"obstacle\":\""       + String(robot.obstacle) + "\",";
  json += "\"obstacleLeft\":"     + boolJson(robot.obstacleLeft) + ",";
  json += "\"obstacleRight\":"    + boolJson(robot.obstacleRight) + ",";
  json += "\"obstacleRear\":"     + boolJson(robot.obstacleRear) + ",";

  // ---- NEW: HC-SR04 front distance -------------------------------------
  // distanceCm is -1 when the sensor returned no valid echo. The app must show
  // that as "no reading" rather than as 0 cm, which would read as "touching".
  json += "\"distanceCm\":"        + String(robot.distanceCm, 1) + ",";
  json += "\"distanceThreshold\":" + String(robot.distanceThreshold) + ",";
  json += "\"distanceAlert\":"     + boolJson(robot.distanceAlert) + ",";
  json += "\"distanceValid\":"     + boolJson(robot.distanceCm >= 0.0f) + ",";
  json += "\"ultrasonic\":\""      + String(robot.ultrasonic) + "\",";
  json += "\"avoidState\":\""      + String(robot.avoidState) + "\",";
  json += "\"battery\":"          + String(robot.battery) + ",";
  json += "\"buzzer\":\""         + String(robot.buzzer) + "\",";
  json += "\"move\":\""           + String(robot.move) + "\",";
  json += "\"alert\":"            + boolJson(robot.alert) + ",";
  json += "\"sensor\":\""         + String(robot.sensor) + "\",";
  json += "\"speed\":"            + String(robot.speed) + ",";
  json += "\"lastStopReason\":\"" + String(robot.lastStopReason) + "\",";
  json += "\"commandsReceived\":" + String(robot.cmdsReceived) + ",";
  json += "\"uptimeMs\":"         + String(millis()) + ",";
  // ESP32 core 3.x has no WebServer::hasClient() and no clientConnected().
// WiFiClient::connected() is the supported way to test the current client.
// server.client() returns NetworkClient, which derives from WiFiClient.
json += "\"clientCount\":"      + String(server.client().connected() ? 1 : 0) + ",";
  json += "\"wifiAddress\":\""    + wifiAddress() + "\"";
  json += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

/* ==========================================================================
 *  Command translation -- shared by GET and POST
 * ==========================================================================
 *  Returns true if the command was recognised. `out` receives the exact
 *  protocol line that will be sent to the Arduino.
 *
 *  WHITELIST ONLY. An arbitrary string from the network is NEVER forwarded to
 *  the motor controller; it must match one of these known commands. This is
 *  the ESP32-side half of the safety argument.
 * ========================================================================== */
inline bool translateCommand(const String &name, const String &value, String &out) {
  String c = name;
  c.toUpperCase();
  c.trim();

  if (c == "FORWARD")  { out = "CMD:FORWARD";  return true; }
  if (c == "BACKWARD") { out = "CMD:BACKWARD"; return true; }
  if (c == "LEFT")     { out = "CMD:LEFT";     return true; }
  if (c == "RIGHT")    { out = "CMD:RIGHT";    return true; }
  if (c == "STOP")     { out = "CMD:STOP";     return true; }
  if (c == "SCAN")     { out = "CMD:SCAN";     return true; }

  if (c == "SPEED") {
    int v = value.toInt();
    if (value.length() == 0 || v < 0 || v > 100) return false;   // validate range
    out = "CMD:SPEED:" + String(v);
    return true;
  }
  if (c == "MODE" || c == "AUTO") {
    String v = value.length() ? value : name;   // MODE:AUTO or AUTO
    v.toUpperCase();
    if (v == "AUTO")                { out = "CMD:MODE:AUTO";   return true; }
    if (v == "MANUAL")              { out = "CMD:MODE:MANUAL"; return true; }
    return false;
  }
  if (c == "BUZZER" || c == "BUZZ") {
    String v = value.length() ? value : name;
    v.toUpperCase();
    if (v == "ON" || v == "TRUE" || v == "1")  { out = "CMD:BUZZER:ON";  return true; }
    if (v == "OFF" || v == "FALSE" || v == "0") { out = "CMD:BUZZER:OFF"; return true; }
    if (v == "TEST")  { out = "CMD:BUZZER:TEST";  return true; }
    if (v == "ALERT") { out = "CMD:BUZZER:ALERT"; return true; }
    return false;
  }
  if (c == "ALERT")     { out = "CMD:ALERT:RESET"; return true; }
  if (c == "SELFTEST") { out = "CMD:SELFTEST";    return true; }
  if (c == "PING")      { out = "CMD:PING";        return true; }

  return false;   // unknown command -> rejected
}

/* ==========================================================================
 *  GET /api/cmd?c=FORWARD      and      POST /api/cmd
 * ========================================================================== */
inline void handleCmdGet() {
  if (!server.hasArg("c")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing c\"}");
    return;
  }
  String name  = server.arg("c");
  String value = server.arg("v");       // optional value, e.g. v=60
  String line;

  if (!translateCommand(name, value, line)) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"unknown command\"}");
    return;
  }

  sendToArduino(line.c_str());
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json",
              String("{\"ok\":true,\"sent\":\"") + line + "\"}");
}

inline void handleCmdPost() {
  String body  = server.arg("plain");
  String name  = jsonField(body, "command");
  String value = jsonField(body, "value");

  if (name.length() == 0) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"missing command\"}");
    return;
  }

  String line;
  if (!translateCommand(name, value, line)) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"unknown command\"}");
    return;
  }

  sendToArduino(line.c_str());
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json",
              String("{\"ok\":true,\"sent\":\"") + line + "\"}");
}

/* ==========================================================================
 *  GET /api/ping   and   GET /api/snapshot
 * ========================================================================== */
inline void handlePing() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json",
              String("{\"pong\":true,\"robotOnline\":") + boolJson(robot.robotOnline) + "}");
}

inline void handleSnapshot() {
  // ESP32 core 3.x: esp_camera_fb_get() takes NO arguments and returns the
  // frame buffer pointer directly (NULL on failure). The old two-argument
  // form returning esp_err_t no longer exists.
  camera_fb_t *fb = esp_camera_fb_get();

  if (!fb) {
    server.send(500, "text/plain", "camera capture failed");
    return;
  }

  // JPEG frame buffers are not always tightly packed, so derive the length
  // from the reported size rather than assuming width*height*3/2.
  size_t len = fb->width * fb->height * 3 / 2;
  if (fb->len && fb->len < len) len = fb->len;

  server.setContentLength(len);
  server.send(200, "image/jpeg", "");
  server.sendContent((const char *)fb->buf, len);
  esp_camera_fb_return(fb);
}

/* ==========================================================================
 *  GET /api/camera  --  live MJPEG stream
 * ==========================================================================
 *  The classic multipart/x-mixed-replace technique: the browser keeps one
 *  connection open and each JPEG boundary refreshes the image. This lets the
 *  app show live video without needing WebSockets.
 *
 *  IMPORTANT CAVEAT: this handler runs until the client disconnects, so the
 *  rest of the firmware (including the CMD:PING keep-alive) does NOT run
 *  while a stream is open. The Arduino therefore hits its 3 s failsafe and
 *  stops the robot.
 *
 *  >>> So: keep ONE client on the stream, and only open the video while the
 *  camera panel is actually visible. Close the stream when the operator
 *  switches to the control view, or the robot will stop itself.
 *  >>> If you need video and control at the same time, lower CAMERA_FRAME_SIZE
 *  to FRAMESIZE_QVGA and/or shorten the loop delay at the bottom of this
 *  function, or move the ping onto a hardware timer callback. <<<
 * ========================================================================== */
inline void handleStream() {
  WiFiClient client = server.client();

  client.printf("HTTP/1.1 200 OK\r\n"
                "Content-Type: multipart/x-mixed-replace;boundary=frame\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n");

  const char *boundary   = "frame";
  const char *contentType = "image/jpeg";

  while (client.connected()) {
    // ESP32 core 3.x: esp_camera_fb_get() takes NO arguments and returns the
    // buffer pointer directly, or NULL on failure.
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println(F("[CAMERA] fb_get failed during stream"));
      delay(100);
      continue;
    }

    client.printf("--%s\r\n"
                  "Content-Type: %s\r\n"
                  "Content-Length: %u\r\n"
                  "\r\n",
                  boundary, contentType, fb->len);
    client.write(fb->buf, fb->len);
    client.printf("\r\n");
    esp_camera_fb_return(fb);      // must return it or the buffer leaks

    delay(30);                     // let the TCP stack and radio breathe
  }
}

/* ==========================================================================
 *  GET /  --  built-in control panel
 * ==========================================================================
 *  A small self-contained HTML page. It is NOT the mobile app, but it lets you
 *  test every command and every status value from a phone browser before
 *  writing any app code, which makes hardware bring-up much easier.
 * ========================================================================== */
inline const char* INDEX_HTML = R"HTML(
<!DOCTYPE html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BIO-MOUSE Control Panel</title>
<style>
 body{font-family:Arial,sans-serif;background:#111;color:#eee;margin:0;padding:10px}
 h1{font-size:18px;text-align:center;color:#4CAF50}
 .grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;max-width:420px;margin:auto}
 button{padding:16px;font-size:15px;border:0;border-radius:8px;background:#333;color:#fff}
 .stop{background:#c62828}.scan{background:#1565c0}.auto{background:#2e7d32}
 .row{max-width:420px;margin:10px auto;background:#1c1c1c;padding:8px;border-radius:8px}
 .row div{display:flex;justify-content:space-between;font-size:13px;padding:2px}
 .k{color:#888}.v{color:#4CAF50;font-weight:bold}
 #cam{width:100%;max-width:420px;display:block;margin:10px auto;border-radius:8px}
 #alert{background:#c62828;padding:10px;text-align:center;border-radius:8px;
        margin:8px auto;max-width:420px;display:none}
 input[type=range]{width:100%}
</style></head><body>
<h1>BIO-MOUSE RESCUE ROBOT</h1>
<div id="alert">HEAT ALERT - POSSIBLE SURVIVOR</div>
<img id="cam" src="/api/snapshot">
<div class="grid">
  <div></div><button onclick="cmd('LEFT')">LEFT</button><div></div>
  <button onclick="cmd('BACKWARD')">BACK</button>
  <button class="stop" onclick="cmd('STOP')">STOP</button>
  <button onclick="cmd('FORWARD')">FWD</button>
  <div></div><button onclick="cmd('RIGHT')">RIGHT</button><div></div>
  <button class="scan" onclick="cmd('SCAN')">SCAN</button>
  <button class="auto" onclick="cmd('MODE','AUTO')">AUTO</button>
  <button onclick="cmd('MODE','MANUAL')">MANUAL</button>
</div>
<div class="row">
  <div><span class="k">Speed</span><span class="v" id="speedV">55</span></div>
  <input type="range" min="0" max="100" value="55" id="sp">
  <button onclick="cmd('SPEED',document.getElementById('sp').value)">SET SPEED</button>
  <button onclick="cmd('BUZZER','TEST')">BUZZER TEST</button>
</div>
<div class="row">
  <div><span class="k">Robot</span><span class="v" id="robotStatus">-</span></div>
  <div><span class="k">Connection</span><span class="v" id="connection">-</span></div>
  <div><span class="k">Mode</span><span class="v" id="mode">-</span></div>
  <div><span class="k">Temp</span><span class="v" id="temperature">-</span></div>
  <div><span class="k">Thermal</span><span class="v" id="thermalStatus">-</span></div>
  <div><span class="k">Heat dir</span><span class="v" id="heatDirection">-</span></div>
  <div><span class="k">Obstacle L/R</span><span class="v" id="obst">-</span></div>
  <div><span class="k">Front distance</span><span class="v" id="dist">-</span></div>
  <div><span class="k">Distance alert</span><span class="v" id="distAlert">-</span></div>
  <div><span class="k">Avoid phase</span><span class="v" id="avoidState">-</span></div>
  <div><span class="k">Ultrasonic</span><span class="v" id="ultrasonic">-</span></div>
  <div><span class="k">Battery</span><span class="v" id="battery">-</span></div>
  <div><span class="k">Buzzer</span><span class="v" id="buzzer">-</span></div>
</div>
<script>
function cmd(c,v){
  var u='/api/cmd?c='+encodeURIComponent(c)+
        (v!==undefined?'&v='+encodeURIComponent(v):'');
  fetch(u).then(function(r){return r.json()}).then(function(d){
    if(!d.ok) alert('REJECTED: '+d.error);});
}
// Poll the camera still-frame on a timer instead of holding an MJPEG stream
// open, because an open stream blocks the ping and trips the failsafe.
setInterval(function(){
  if(document.visibilityState==='visible')
    document.getElementById('cam').src='/api/snapshot?t='+Date.now();
},1000);
setInterval(function(){
  fetch('/api/status').then(function(r){return r.json()}).then(function(s){
    ['robotStatus','connection','mode','temperature','thermalStatus',
     'heatDirection','battery','buzzer','ultrasonic','avoidState',
     'obstacle'].forEach(function(k){
      var e=document.getElementById(k);
      if(e && s[k]!==undefined) e.textContent=s[k];
    });
    document.getElementById('obst').textContent=
      (s.obstacleLeft?'LEFT:CLEAR ':'LEFT:HIT ')+
      (s.obstacleRight?'/ RIGHT:CLEAR':'/ RIGHT:HIT');

    // Front distance. Show "NO ECHO" for an invalid reading rather than 0,
    // which would look like the robot is touching something.
    document.getElementById('dist').textContent =
      s.distanceValid ? s.distanceCm.toFixed(0)+' cm (alert <='+s.distanceThreshold+' cm)'
                       : 'NO ECHO';
    document.getElementById('distAlert').textContent =
      s.distanceAlert ? 'YES - OBSTACLE' : 'no';

    var d = document.getElementById('distAlert');
    d.style.color = s.distanceAlert ? '#ff5252' : '#4CAF50';

    document.getElementById('alert').style.display=s.alert?'block':'none';
  }).catch(function(){
    document.getElementById('connection').textContent='DISCONNECTED';});
},700);
</script></body></html>
)HTML";

/* ==========================================================================
 *  Route registration
 * ========================================================================== */
inline void httpBegin() {
  server.on("/",            HTTP_GET,  []() { server.send(200, "text/html", INDEX_HTML); });
  server.on("/api/status",  HTTP_GET,  handleStatus);
  server.on("/api/cmd",     HTTP_GET,  handleCmdGet);
  server.on("/api/cmd",     HTTP_POST, handleCmdPost);
  server.on("/api/camera",  HTTP_GET,  handleStream);
  server.on("/api/snapshot",HTTP_GET,  handleSnapshot);
  server.on("/api/ping",    HTTP_GET,  handlePing);

  server.onNotFound([]() {
    server.send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
  });

  server.begin(HTTP_PORT);
  Serial.print(F("[HTTP] server started on port "));
  Serial.println(HTTP_PORT);
}

#endif // BIOMOUSE_ESP_HTTP_H