#!/usr/bin/env node
/**
 * BIO-MOUSE  --  Arduino simulator
 * ============================================================================
 *  Implements the Arduino end of the protocol on a TCP port, so you can test
 *  the ESP32-CAM firmware and the mobile app BEFORE any hardware exists.
 *
 *  It reproduces the real firmware's behaviour, including the rules that
 *  matter for testing:
 *    - STOP always works
 *    - drive commands are REJECTED in AUTO mode  (ERROR:MODE_IS_AUTO)
 *    - drive commands are REJECTED when an obstacle is present
 *    - unknown commands produce ERROR:UNKNOWN
 *    - it stops itself if no CMD:PING arrives within FAILSAFE_TIMEOUT_MS
 *      (this is how you verify the failsafe path)
 *    - a simulated heat source can be pushed into the LEFT / CENTER / RIGHT
 *      sector, which drives the thermal-guided steering
 *
 *  USAGE
 *    node sim_robot.js                 # listen on 7777
 *    node sim_robot.js --port 7777
 * ============================================================================
 */
'use strict';

const net = require('net');

const argv = process.argv.slice(2);
const argOf = (name, fallback) => {
  const i = argv.indexOf(name);
  return i >= 0 && argv[i + 1] ? argv[i + 1] : fallback;
};

const PORT   = parseInt(argOf('--port', '7777'), 10);
const BAUD   = 115200;
const FAILSAFE_TIMEOUT_MS = 3000;   // must match config.h on the Arduino
const TELEMETRY_INTERVAL_MS = 250;
const HEARTBEAT_INTERVAL_MS = 1000;

// ---- Simulated robot state -----------------------------------------------
const state = {
  mode: 'MANUAL',
  move: 'STOP',
  speed: 55,
  buzzer: false,
  alert: false,
  obstacleLeft: false,
  obstacleRight: false,
  obstacleRear: false,
  sensorReady: true,
  // ---- NEW: simulated HC-SR04 ----
  distanceCm: 120,              // simulated front distance, cm
  ultrasonicOk: true,           // false = sensor returning no echo
  avoidState: 'IDLE',
  battery: 87,
  cmds: 0,
  rx: 0,
  lastRx: Date.now(),
};

const ULTRASONIC_THRESHOLD_CM = 30;
// Must exceed the whole manoeuvre (120+250+400+450+300 = 1520 ms), matching
// AVOID_COOLDOWN_MS in the Arduino config.h.
const AVOID_COOLDOWN_MS = 2500;
const AVOID_PHASES = [
  { name: 'REACTING',  ms: 120 },
  { name: 'STOPPED',   ms: 250 },
  { name: 'REVERSING', ms: 400 },
  { name: 'TURNING',   ms: 450 },
  { name: 'RESUMING',  ms: 300 },
];
let avoidIndex = -1;          // -1 = idle
let avoidStartedMs = 0;

// Mirror the firmware: capture the command BEFORE the manoeuvre overwrites it.
// Comparing state.move at completion time would always see STOP, and the robot
// would never resume -- which is exactly the bug this field prevents.
let avoidResumeForward = false;

// True when the ultrasonic reports something at or below the threshold.
function ultrasonicBlocked() {
  return state.ultrasonicOk && state.distanceCm <= ULTRASONIC_THRESHOLD_CM;
}

// Advance the simulated avoidance state machine, mirroring obstacle.h.
function avoidanceService() {
  if (avoidIndex < 0) { state.avoidState = 'IDLE'; return; }

  const phase = AVOID_PHASES[avoidIndex];
  if (Date.now() - avoidStartedMs < phase.ms) return;

  avoidIndex++;
  avoidStartedMs = Date.now();

  if (avoidIndex >= AVOID_PHASES.length) {
    avoidIndex = -1;
    state.avoidState = 'IDLE';
    // Resume only if the path actually cleared, exactly like the firmware.
    if (!ultrasonicBlocked() && !state.obstacleLeft && !state.obstacleRight) {
      if (state.mode === 'AUTO' && avoidResumeForward) state.move = 'FORWARD';
      log(`obstacle avoid complete (resumeForward=${avoidResumeForward}, mode=${state.mode})`);
    } else {
      state.move = 'STOP';
      log('path still blocked - holding position');
    }
    return;
  }
  state.avoidState = AVOID_PHASES[avoidIndex].name;
  if (state.avoidState === 'REVERSING')      state.move = 'BACKWARD';
  else if (state.avoidState === 'TURNING')   state.move = 'SCAN';
  else                                       state.move = 'STOP';
}

// Start a manoeuvre if something is in the way and the cooldown has expired.
function avoidanceTriggerCheck() {
  if (avoidIndex >= 0) return;                       // already busy
  if (Date.now() - state.lastAvoidStart < AVOID_COOLDOWN_MS) return;

  const blocked = ultrasonicBlocked() || state.obstacleLeft || state.obstacleRight;
  if (!blocked) return;

  // Mirrors avoidTrigger(): stops immediately, remembers the command.
  // Capture the resume flag FIRST, before overwriting state.move.
  avoidResumeForward = (state.move === 'FORWARD');
  avoidIndex = 0;
  avoidStartedMs = Date.now();
  state.lastAvoidStart = Date.now();
  state.move = 'STOP';
  state.avoidState = AVOID_PHASES[0].name;
  log(`OBSTACLE_DETECTED ultra=${state.distanceCm}cm ` +
      `whiskerL=${state.obstacleLeft} whiskerR=${state.obstacleRight}`);
}

// ---- Simulated thermal scene --------------------------------------------
// null means "nothing warm in that sector".
const scene = { left: null, center: null, right: null };
const AMBIENT = 26.0;

function sectorTemps() {
  const t = [AMBIENT, AMBIENT, AMBIENT];
  if (scene.left   != null) t[0] = scene.left;
  if (scene.center != null) t[1] = scene.center;
  if (scene.right  != null) t[2] = scene.right;
  return t;
}

function thermalReport() {
  const t = sectorTemps();
  const max = Math.max(...t);
  let best = 0;
  for (let i = 1; i < 3; i++) if (t[i] > t[best]) best = i;
  const rise = max - AMBIENT;

  let status = 'NONE';
  if (rise >= 6) status = 'POSSIBLE_SURVIVOR';
  else if (rise >= 3) status = 'HEAT_DETECTED';

  const dir = rise < 3 ? 'NONE' : ['LEFT', 'CENTER', 'RIGHT'][best];

  // Close-range alert: strong AND above 32 C.
  if (max >= 32 && rise >= 3) state.alert = true;

  return { max, ambient: AMBIENT, status, dir };
}

function obstacleWord() {
  if (state.obstacleLeft && state.obstacleRight) return 'BOTH';
  if (state.obstacleLeft)  return 'LEFT';
  if (state.obstacleRight) return 'RIGHT';
  return 'CLEAR';
}

function log(msg) {
  const t = new Date().toISOString().slice(11, 23);
  console.log(`[${t}] ${msg}`);
}

const clients = new Set();

function send(sock, line) {
  if (!sock || sock.destroyed) return;
  sock.write(line + '\n');
}
function sendAll(lines) {
  for (const c of clients) lines.forEach((l) => send(c, l));
}

// ---- Command handling: mirrors uart.h in the Arduino firmware -------------
function handleCommand(sock, line) {
  state.cmds++;
  state.rx++;
  state.lastRx = Date.now();
  log(`RX  ${line}`);

  if (line === 'CMD:PING') return send(sock, 'ACK:PING');

  if (line === 'CMD:MODE:MANUAL') {
    state.mode = 'MANUAL';
    state.move = 'STOP';
    send(sock, 'MODE:MANUAL');
    return send(sock, 'ACK:MODE:MANUAL');
  }
  if (line === 'CMD:MODE:AUTO') {
    state.mode = 'AUTO';
    send(sock, 'MODE:AUTO');
    return send(sock, 'ACK:MODE:AUTO');
  }
  if (line.startsWith('CMD:SPEED:')) {
    state.speed = Math.max(0, Math.min(100, parseInt(line.slice(10), 10) || 0));
    send(sock, `SPEED:${state.speed}`);
    return send(sock, 'ACK:SPEED');
  }
  if (line === 'CMD:STOP') {
    // Mirrors uart.h: an operator STOP cancels any manoeuvre in flight.
    if (avoidIndex >= 0) {
      avoidIndex = -1;
      state.avoidState = 'IDLE';
      log('avoidance cancelled by operator STOP');
    }
    avoidResumeForward = false;
    state.move = 'STOP';
    send(sock, 'ROBOT:STOPPED:CMD');
    return send(sock, 'ACK:STOP');
  }
  if (['CMD:FORWARD', 'CMD:BACKWARD', 'CMD:LEFT', 'CMD:RIGHT'].includes(line)) {
    // Conflict rule 1: AUTO owns the motors.
    if (state.mode === 'AUTO') {
      log('     -> rejected: AUTO mode owns the motors');
      return send(sock, 'ERROR:MODE_IS_AUTO');
    }
    // Conflict rule 2: never drive into an obstacle -- now including the
    // ultrasonic, matching uart.h in the real firmware.
    if (ultrasonicBlocked() || state.obstacleLeft || state.obstacleRight) {
      log('     -> rejected: obstacle present');
      return send(sock, 'ERROR:OBSTACLE_BLOCKED');
    }
    if (avoidIndex >= 0) {
      log('     -> rejected: avoidance in progress');
      return send(sock, 'ERROR:AVOID_IN_PROGRESS');
    }
    state.move = line.slice(4);
    return send(sock, `ACK:${state.move}`);
  }
  if (line === 'CMD:SCAN') {
    if (state.obstacleLeft || state.obstacleRight)
      return send(sock, 'ERROR:OBSTACLE_BLOCKED');
    state.mode = 'MANUAL';
    state.move = 'SCAN';
    send(sock, 'MODE:MANUAL');
    return send(sock, 'ACK:SCAN');
  }
  if (line === 'CMD:BUZZER:ON')   { state.buzzer = true;  send(sock, 'BUZZER:ON');  return send(sock, 'ACK:BUZZER:ON'); }
  if (line === 'CMD:BUZZER:OFF')  { state.buzzer = false; send(sock, 'BUZZER:OFF'); return send(sock, 'ACK:BUZZER:OFF'); }
  if (line === 'CMD:BUZZER:TEST') { return send(sock, 'ACK:BUZZER:TEST'); }
  if (line === 'CMD:BUZZER:ALERT'){ return send(sock, 'ACK:BUZZER:ALERT'); }
  if (line === 'CMD:ALERT:RESET') { state.alert = false; return send(sock, 'ACK:ALERT:RESET'); }
  if (line === 'CMD:SELFTEST') {
    send(sock, 'EVENT:SELFTEST_START');
    send(sock, `SENSOR:${state.sensorReady ? 'READY' : 'MISSING'}`);
    send(sock, `BATTERY:${state.battery}`);
    return send(sock, 'ACK:SELFTEST');
  }

  log('     -> unknown command');
  send(sock, `ERROR:UNKNOWN:${line}`);
}

// ---- TCP server ----------------------------------------------------------
const server = net.createServer((sock) => {
  clients.add(sock);
  log(`client connected (${clients.size} total)`);
  sock.write('ROBOT:ONLINE\nROBOT:ONLINE:CMDS=0:RX=0:UPTIME=0s\n');

  sock.setEncoding('utf8');
  sock.on('data', (chunk) => {
    let buf = (sock._rxbuf || '') + chunk;
    let idx;
    while ((idx = buf.indexOf('\n')) >= 0) {
      const line = buf.slice(0, idx).replace(/\r$/, '');
      buf = buf.slice(idx + 1);
      if (!line.trim()) continue;

      // SIM: lines are test hooks, not robot commands. Handling them here is
      // what lets automated tests drive the scene over the same socket the
      // app would use.
      if (line.toUpperCase().startsWith('SIM:')) {
        applySimCommand(line);
      } else {
        handleCommand(sock, line);
      }
    }
    sock._rxbuf = buf;
  });

  sock.on('close', () => { clients.delete(sock); log('client disconnected'); });
  sock.on('error', () => clients.delete(sock));
});

server.listen(PORT, '127.0.0.1', () => {
  console.log('');
  console.log('============================================');
  console.log('  BIO-MOUSE Arduino simulator');
  console.log(`  Listening on 127.0.0.1:${PORT} at ${BAUD} baud (emulated)`);
  console.log('============================================');
  console.log('');
  console.log('Drive it from another terminal, e.g.:');
  console.log(`  node tools/sim_send.js CMD:PING`);
  console.log(`  node tools/sim_send.js CMD:FORWARD`);
  console.log('');
  console.log('Or type directly into this window:');
  console.log('  CMD:PING                    -> ACK:PING');
  console.log('  CMD:FORWARD                 -> ACK:FORWARD');
  console.log('  CMD:MODE:AUTO               -> MODE:AUTO');
  console.log('  CMD:FORWARD                 -> ERROR:MODE_IS_AUTO  (conflict rule)');
  console.log('');
  console.log('Simulation hooks (work here AND over the socket):');
  console.log('  SIM:HEAT:LEFT:36            -> 36 C source in the left sector');
  console.log('  SIM:HEAT:LEFT:NONE          -> clear that sector');
  console.log('  SIM:OBSTACLE:LEFT:1         -> left whisker hit (0 to clear)');
  console.log('  SIM:BATTERY:15              -> force battery percentage');
  console.log('  SIM:SENSOR:0                -> simulate a missing AMG8833');
  console.log('  SIM:SCENE                   -> print the current scene');
  console.log('');
  console.log('To verify the failsafe, stay connected and stop sending CMD:PING');
  console.log('for 3 seconds: the robot must report ROBOT:STOPPED:FAILSAFE_LINK_LOST.');
  console.log('');
});

// ---- Periodic telemetry (same cadence as the real firmware) --------------
setInterval(() => {
  if (clients.size === 0) return;

  // Failsafe: no traffic for 3s -> stop and report it, exactly like the real
  // firmware. Pause the ESP32-CAM (or kill it) to watch this fire.
  const quiet = Date.now() - state.lastRx;
  if (quiet > FAILSAFE_TIMEOUT_MS && state.move !== 'STOP') {
    state.move = 'STOP';
    log('FAILSAFE: no command within timeout -> STOP');
    sendAll(['EVENT:FAILSAFE_LINK_LOST', 'ROBOT:STOPPED:FAILSAFE_LINK_LOST']);
  }

  // ---- NEW: avoidance state machine + ultrasonic awareness ------------------
  avoidanceService();
  avoidanceTriggerCheck();

  const t = thermalReport();
  const blockedByUltra = ultrasonicBlocked();
  const obstacleLabel = (state.obstacleLeft && state.obstacleRight) ? 'BOTH'
                      : state.obstacleLeft  ? 'LEFT'
                      : state.obstacleRight ? 'RIGHT'
                      : state.obstacleRear  ? 'REAR'
                      : blockedByUltra      ? 'DETECTED'
                      : 'CLEAR';

  sendAll([
    `TEMP:${t.max.toFixed(1)}`,
    `AMBIENT:${t.ambient.toFixed(1)}`,
    `THERMAL:${t.status}`,
    `HEAT:${t.dir}`,
    `BATTERY:${state.battery}`,
    `BUZZER:${state.buzzer ? 'ON' : 'OFF'}`,
    `OBSTACLE_LEFT:${state.obstacleLeft ? 1 : 0}`,
    `OBSTACLE_RIGHT:${state.obstacleRight ? 1 : 0}`,
    `OBSTACLE_REAR:${state.obstacleRear ? 1 : 0}`,
    `OBSTACLE:${obstacleLabel}:${state.ultrasonicOk ? Math.round(state.distanceCm) : 'NA'}`,
    `DISTANCE:${state.ultrasonicOk ? state.distanceCm.toFixed(1) : '-1.0'}CM`,
    `DISTANCE_THRESHOLD:${ULTRASONIC_THRESHOLD_CM}`,
    `DISTANCE_ALERT:${blockedByUltra ? 'ON' : 'OFF'}`,
    `ULTRASONIC:${state.ultrasonicOk ? 'READY' : 'NO_ECHO'}`,
    `AVOID:${state.avoidState}`,
    `SENSOR:${state.sensorReady ? 'READY' : 'MISSING'}`,
    `MODE:${state.mode}`,
    `SPEED:${state.speed}`,
    `MOVE:${state.move}`,
    `ALERT:${state.alert ? 'ON' : 'OFF'}`,
    `CONN:${quiet > FAILSAFE_TIMEOUT_MS ? 'LOST' : 'OK'}`,
  ]);
}, TELEMETRY_INTERVAL_MS);

setInterval(() => {
  if (clients.size === 0) return;
  sendAll([`ROBOT:ONLINE:CMDS=${state.cmds}:RX=${state.rx}:UPTIME=${Math.floor(process.uptime())}s`]);
}, HEARTBEAT_INTERVAL_MS);

// ---- Simulation control hooks -------------------------------------------
// Reachable two ways, so the simulator is testable headlessly:
//   1. typed into this window  (interactive use)
//   2. sent over the socket with a SIM: prefix (automated tests / CI)
//
// Accepted forms, case-insensitive:
//   SIM:HEAT:LEFT:36          put a 36 C source in the left sector
//   SIM:HEAT:LEFT:NONE        clear it
//   SIM:OBSTACLE:LEFT:1       left whisker hit   (0 to clear)
//   SIM:BATTERY:15            force a battery percentage
//   SIM:SENSOR:0|1            mark the AMG8833 missing / present
//   SIM:SCENE                 dump the current scene
function applySimCommand(raw) {
  const cmd = raw.trim();
  if (!cmd) return false;

  const body = cmd.toUpperCase().startsWith('SIM:') ? cmd.slice(4) : cmd;
  const parts = body.split(':');
  const key = parts[0].toUpperCase();

  if (key === 'HEAT' && parts.length >= 3) {
    const sector = parts[1].toLowerCase();
    const v = parts[2].toUpperCase() === 'NONE' ? null : parseFloat(parts[2]);
    if (['left', 'center', 'right'].includes(sector)) {
      scene[sector] = Number.isNaN(v) ? null : v;
      log(`scene: left=${scene.left} center=${scene.center} right=${scene.right}`);
      return true;
    }
  }
  if (key === 'OBSTACLE' && parts.length >= 3) {
    const side = parts[1].toLowerCase();
    const on = parts[2] === '1' || parts[2].toUpperCase() === 'HIT';
    if (side === 'left')  state.obstacleLeft  = on;
    if (side === 'right') state.obstacleRight = on;
    log(`obstacle: left=${state.obstacleLeft} right=${state.obstacleRight}`);
    return true;
  }
  if (key === 'BATTERY') {
    state.battery = Math.max(0, Math.min(100, parseInt(parts[1], 10) || 0));
    log(`battery=${state.battery}%`);
    return true;
  }
  if (key === 'SENSOR') {
    state.sensorReady = (parts[1] === '1');
    log(`AMG8833 marked ${state.sensorReady ? 'READY' : 'MISSING'}`);
    return true;
  }
  // ---- NEW: simulated HC-SR04 front distance ----
  //   SIM:DIST:<cm>   set the front distance (use NONE to simulate no echo)
  //   SIM:ULTRA:1|0   ultrasonic responding / stopped responding
  if (key === 'DIST' && parts.length >= 2) {
    if (parts[1].toUpperCase() === 'NONE') {
      state.ultrasonicOk = false;
      return log('ultrasonic -> NO ECHO (invalid reading, not "clear")');
    }
    state.ultrasonicOk = true;
    state.distanceCm = parseFloat(parts[1]);
    if (Number.isNaN(state.distanceCm)) return false;
    return log(`front distance = ${state.distanceCm} cm`);
  }
  if (key === 'ULTRA') {
    state.ultrasonicOk = (parts[1] === '1');
    log(`ultrasonic ${state.ultrasonicOk ? 'READY' : 'NO ECHO'}`);
    return true;
  }
  if (key === 'SCENE') {
    log(`scene: left=${scene.left} center=${scene.center} right=${scene.right} ` +
        `| dist=${state.ultrasonicOk ? state.distanceCm + 'cm' : 'NO_ECHO'} ` +
        `| obst L=${state.obstacleLeft} R=${state.obstacleRight} ` +
        `| battery=${state.battery} sensor=${state.sensorReady}`);
    return true;
  }
  return false;
}

const stdin = process.stdin;
stdin.setEncoding('utf8');
stdin.on('data', (chunk) => {
  chunk.split('\n').forEach((raw) => {
    const cmd = raw.trim();
    if (!cmd) return;
    // A SIM: hook is handled locally; anything else is a robot command.
    if (!applySimCommand(cmd)) {
      for (const c of clients) handleCommand(c, cmd);
    }
  });
});
stdin.on('end', () => { /* headless: stdin closes, TCP control still works */ });