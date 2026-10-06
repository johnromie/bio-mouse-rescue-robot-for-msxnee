/**
 * BIO-MOUSE  --  RobotClient
 * ============================================================================
 *  Drop this module into your Capacitor / web app. It is the ONLY place that
 *  needs to know the robot's HTTP address, so changing the IP never means
 *  hunting through UI code.
 *
 *  USAGE
 *    import { RobotClient } from './robot-client.js';
 *    const robot = new RobotClient('http://192.168.4.1');
 *    await robot.connect();
 *    robot.onUpdate(s => renderDashboard(s));
 *    await robot.forward();
 *
 *  Everything returns a Promise and rejects on failure, so you can show a
 *  "command failed" state rather than silently doing nothing.
 *
 *  NOTE ON https: if you later serve the app over HTTPS, browsers will block
 *  plain-HTTP requests to the robot. Either keep the app on HTTP in the field,
 *  or use the Capacitor HTTP plugin (which bypasses this restriction).
 * ========================================================================== */

export const DEFAULT_ROBOT_IP = '192.168.4.1';   // ESP32-CAM access-point mode

/** One robot status snapshot. Field names match /api/status exactly. */
export function emptyStatus() {
  return {
    robotStatus: 'UNKNOWN',       // ONLINE | OFFLINE | ALERT | FAILSAFE
    connection: 'DISCONNECTED',  // CONNECTED | DISCONNECTED | UNSTABLE | NO_WIFI
    robotOnline: false,
    linkLost: false,            // Arduino-side failsafe reported link loss
    mode: 'MANUAL',              // MANUAL | AUTO
    temperature: 0,              // TEMPERATURE, degrees C
    ambient: 0,
    thermalStatus: 'NONE',       // NONE | HEAT_DETECTED | POSSIBLE_SURVIVOR
    heatDirection: 'NONE',       // LEFT | CENTER | RIGHT | NONE
    obstacle: 'CLEAR',           // CLEAR | LEFT | RIGHT | BOTH | REAR
    obstacleLeft: false,         // OBSTACLE_LEFT
    obstacleRight: false,        // OBSTACLE_RIGHT
    obstacleRear: false,
    // ---- HC-SR04 front distance ----
    distanceCm: -1,              // -1 means no valid echo (NOT zero)
    distanceThreshold: 30,       // alert threshold in cm
    distanceAlert: false,        // true when distanceCm <= threshold
    distanceValid: false,        // false when the sensor gave no echo
    ultrasonic: 'READY',         // READY | NO_ECHO
    avoidState: 'IDLE',          // IDLE/REACTING/STOPPED/REVERSING/TURNING/RESUMING
    battery: 0,                  // BATTERY, percent
    buzzer: 'OFF',               // BUZZER_STATUS
    move: 'STOP',
    alert: false,
    sensor: 'MISSING',
    speed: 55,
    lastStopReason: '',
    commandsReceived: 0,
    uptimeMs: 0,
    clientCount: 0,
    wifiAddress: '',
  };
}

export class RobotClient {
  /**
   * @param {string} baseUrl  e.g. 'http://192.168.4.1'
   * @param {object} [opts]
   * @param {number} [opts.pollMs=700]   status polling interval
   * @param {number} [opts.timeoutMs=4000] per-request timeout
   */
  constructor(baseUrl = DEFAULT_ROBOT_IP, opts = {}) {
    this.baseUrl = baseUrl.replace(/\/+$/, '');
    this.pollMs = opts.pollMs ?? 700;
    this.timeoutMs = opts.timeoutMs ?? 4000;

    this.status = emptyStatus();
    this.connected = false;
    this.lastError = null;

    this._listeners = new Set();
    this._timer = null;
    this._inFlight = false;    // stops polls from overlapping
    this._abort = null;        // lets stopPolling() cancel a pending fetch
  }

  // ---- Events ------------------------------------------------------------
  /** Subscribe to status updates. Returns an unsubscribe function. */
  onUpdate(fn) {
    this._listeners.add(fn);
    return () => this._listeners.delete(fn);
  }

  _emit() {
    for (const fn of this._listeners) {
      try { fn(this.status); }
      catch (e) { console.error('[RobotClient] listener error', e); }
    }
  }

  // ---- Low-level request -------------------------------------------------
  async _fetchJson(path, options = {}) {
    this._abort = new AbortController();
    const timer = setTimeout(() => this._abort?.abort(), this.timeoutMs);

    try {
      const res = await fetch(this.baseUrl + path, {
        signal: this._abort.signal,
        cache: 'no-store',
        ...options,
      });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      return await res.json();
    } finally {
      clearTimeout(timer);
      this._abort = null;
    }
  }

  // ---- Connection --------------------------------------------------------
  /** Check the robot is reachable, then start polling. */
  async connect() {
    try {
      await this._fetchJson('/api/ping');
      this.connected = true;
      this.lastError = null;
      this.startPolling();
      return true;
    } catch (e) {
      this.connected = false;
      this.lastError = e.message;
      this.status = emptyStatus();
      this.status.connection = 'DISCONNECTED';
      this._emit();
      return false;
    }
  }

  disconnect() {
    this.stopPolling();
    this.connected = false;
  }

  startPolling() {
    this.stopPolling();
    this._timer = setInterval(() => this._pollOnce(), this.pollMs);
    this._pollOnce();
  }

  stopPolling() {
    if (this._timer) { clearInterval(this._timer); this._timer = null; }
    this._abort?.abort();
  }

  async _pollOnce() {
    if (this._inFlight) return;      // never overlap requests
    this._inFlight = true;
    try {
      const data = await this._fetchJson('/api/status');
      // Merge so a field the firmware later drops does not break the UI.
      this.status = { ...this.status, ...data };
      this.connected = true;
      this.lastError = null;
    } catch (e) {
      this.connected = false;
      this.lastError = e.message;
      this.status.connection = 'DISCONNECTED';
    } finally {
      this._inFlight = false;
      this._emit();
    }
  }

  /** Force an immediate status refresh (e.g. right after a command). */
  async refresh() { await this._pollOnce(); }

// ---- Commands ----------------------------------------------------------
  /**
   * Send any supported command. This is the single low-level path used by all
   * the convenience methods below.
   *
   * @param {string} command  FORWARD BACKWARD LEFT RIGHT STOP SCAN
   *                          SPEED MODE BUZZER ALERT SELFTEST
   * @param {string|number} [value]
   */
  async send(command, value) {
    const url = `/api/cmd?c=${encodeURIComponent(command)}` +
                (value === undefined ? '' : `&v=${encodeURIComponent(value)}`);
    try {
      const res = await this._fetchJson(url);
      await this._pollOnce();     // reflect the change immediately
      return res;
    } catch (e) {
      this.lastError = e.message;
      this.connected = false;
      this.status.connection = 'DISCONNECTED';
      this._emit();
      throw e;
    }
  }

  // ---- Movement ----------------------------------------------------------
  forward()  { return this.send('FORWARD'); }
  backward() { return this.send('BACKWARD'); }
  left()     { return this.send('LEFT'); }
  right()    { return this.send('RIGHT'); }
  stop()     { return this.send('STOP'); }
  scan()     { return this.send('SCAN'); }

  /** @param {number} percent 0-100 */
  setSpeed(percent) { return this.send('SPEED', Math.round(percent)); }

  // ---- Mode --------------------------------------------------------------
  setMode(mode) {
    const m = String(mode).toUpperCase();
    if (m !== 'MANUAL' && m !== 'AUTO') {
      return Promise.reject(new Error(`unknown mode: ${mode}`));
    }
    return this.send('MODE', m);
  }
  enableAutoMode()  { return this.setMode('AUTO'); }
  enableManualMode(){ return this.setMode('MANUAL'); }

  // ---- Buzzer / alert ----------------------------------------------------
  buzzerOn()  { return this.send('BUZZER', 'ON'); }
  buzzerOff() { return this.send('BUZZER', 'OFF'); }
  buzzerTest(){ return this.send('BUZZER', 'TEST'); }
  resetAlert(){ return this.send('ALERT', 'RESET'); }
  selfTest()  { return this.send('SELFTEST'); }

  // ---- Camera ------------------------------------------------------------
  /** Live MJPEG stream URL for an <img> tag. Close it when hidden, or the
   *  robot's failsafe may trip because streaming blocks the keep-alive ping. */
  cameraStreamUrl() { return `${this.baseUrl}/api/camera`; }

  /** Single still frame; append a cache-buster to force a refresh. */
  snapshotUrl() { return `${this.baseUrl}/api/snapshot?t=${Date.now()}`; }

  // ---- Derived helpers for the UI ---------------------------------------
  get isAlerting()     { return this.status.alert === true; }
  get isAutoMode()     { return this.status.mode === 'AUTO'; }
  get isObstructed()   { return this.status.obstacleLeft || this.status.obstacleRight; }
  get batteryLow()     { return this.status.battery <= 20; }
  get sensorMissing()  { return this.status.sensor === 'MISSING'; }

  // ---- HC-SR04 front distance -------------------------------------------

  /** True when the ultrasonic is actively avoiding. */
  get isAvoiding()     { return this.status.avoidState !== 'IDLE'; }

  /** True when the front distance is at or below the alert threshold. */
  get distanceAlert()  { return this.status.distanceAlert === true; }

  /**
   * Distance as display text. IMPORTANT: an invalid reading is shown as
   * "NO ECHO", never as "0 cm" -- 0 would look like the robot is touching a
   * wall, when in fact the sensor simply got no echo.
   */
  get distanceText() {
    const s = this.status;
    if (!s.distanceValid || s.distanceCm < 0) return 'NO ECHO';
    return `${s.distanceCm.toFixed(0)} cm`;
  }

  /**
   * True when the ultrasonic has stopped reporting. The whiskers are still
   * active in that case, so the robot is not blind -- but the operator should
   * know the primary sensor is down.
   */
  get ultrasonicFault() { return this.status.ultrasonic === 'NO_ECHO'; }

  /**
   * A single severity string for a status banner, or null when all is well.
   * Checked in priority order: safety-critical conditions first.
   */
  healthMessage() {
    const s = this.status;
    if (!s.robotOnline) return 'Robot offline - check power and UART link';
    if (s.alert) return `Heat alert: ${s.thermalStatus} at ${s.temperature} C`;
    if (s.linkLost) return 'Unstable link - robot may stop soon';
    if (this.distanceAlert)
      return `Obstacle ahead: ${this.distanceText} (limit ${s.distanceThreshold} cm)`;
    if (s.obstacleLeft || s.obstacleRight)
      return `Whisker contact (${s.obstacle})`;
    if (this.ultrasonicFault)
      return 'Ultrasonic not reporting - whiskers are the backup';
    if (s.sensor === 'MISSING') return 'Thermal sensor not detected';
    if (this.batteryLow) return `Battery low: ${s.battery}%`;
    return null;
  }
}

/* ============================================================================
 *  EXAMPLE UI WIRING
 * ----------------------------------------------------------------------------
 *  import { RobotClient } from './robot-client.js';
 *
 *  const robot = new RobotClient('http://192.168.4.1');
 *  const $ = (id) => document.getElementById(id);
 *
 *  robot.onUpdate((s) => {
 *    $('temp').textContent     = s.temperature.toFixed(1) + ' C';
 *    $('thermal').textContent  = s.thermalStatus;          // POSSIBLE_SURVIVOR etc
 *    $('battery').textContent  = s.battery + '%';
 *    $('battL').textContent    = s.obstacleLeft ? 'HIT' : 'CLEAR';
 *    $('battR').textContent    = s.obstacleRight ? 'HIT' : 'CLEAR';
 *    $('buzzer').textContent   = s.buzzer;
 *    $('conn').textContent     = s.connection;
 *    $('mode').textContent     = s.mode;
 *
 *    const msg = robot.healthMessage();
 *    $('banner').style.display = msg ? 'block' : 'none';
 *    $('banner').textContent   = msg || '';
 *  });
 *
 *  // Button handlers -- make them async and catch, so a dropped connection
 *  // shows an error instead of failing silently.
 *  $('btnFwd').onclick  = () => robot.forward().catch(reportError);
 *  $('btnLeft').onclick = () => robot.left().catch(reportError);
 *  $('btnStop').onclick = () => robot.stop().catch(reportError);
 *  $('btnAuto').onclick = () => robot.enableAutoMode().catch(reportError);
 *  $('speed').oninput   = (e) => $('speedVal').textContent = e.target.value;
 *  $('speed').onchange  = (e) => robot.setSpeed(e.target.value).catch(reportError);
 *
 *  // Reconnect when the phone comes back to the foreground.
 *  document.addEventListener('visibilitychange', () => {
 *    if (document.visibilityState === 'visible' && !robot.connected) robot.connect();
 *  });
 *
 *  robot.connect();
 * ========================================================================== */