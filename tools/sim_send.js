#!/usr/bin/env node
/**
 * BIO-MOUSE  --  simulator command sender
 * ============================================================================
 *  Connects to the simulator (tools/sim_robot.js), sends a command, prints
 *  the reply for a moment, then disconnects.
 *
 *  USAGE
 *    node tools/sim_send.js CMD:PING
 *    node tools/sim_send.js CMD:FORWARD
 *    node tools/sim_send.js CMD:SPEED:75
 *    node tools/sim_send.js CMD:MODE:AUTO
 *    node tools/sim_send.js --watch            # stay connected and listen
 *
 *  NOTE: this tool deliberately does NOT send CMD:PING automatically, so
 *  `sim_send.js CMD:FORWARD` will make the simulator's failsafe fire after
 *  3 seconds. That is useful for testing the failsafe path on purpose.
 * ============================================================================
 */
'use strict';

const net = require('net');

const argv = process.argv.slice(2);
const watch = argv.includes('--watch');
const portIdx = argv.indexOf('--port');
const PORT = portIdx >= 0 ? parseInt(argv[portIdx + 1], 10) : 7777;
const commands = argv.filter((a, i) =>
  a.startsWith('CMD:') && a !== '--port' && i !== portIdx + 1);

if (commands.length === 0 && !watch) {
  console.error('usage: node tools/sim_send.js CMD:FORWARD [--port 7777]');
  console.error('       node tools/sim_send.js --watch');
  process.exit(1);
}

const sock = net.connect(PORT, '127.0.0.1', () => {
  console.log(`-- connected to simulator on 127.0.0.1:${PORT}`);
  for (const c of commands) {
    console.log(`>> ${c}`);
    sock.write(c + '\n');
  }
  if (!watch) setTimeout(() => { sock.end(); }, 800);
});

sock.setEncoding('utf8');
let buf = '';
sock.on('data', (chunk) => {
  buf += chunk;
  let idx;
  while ((idx = buf.indexOf('\n')) >= 0) {
    const line = buf.slice(0, idx).replace(/\r$/, '');
    buf = buf.slice(idx + 1);
    if (line.trim()) console.log(`<< ${line}`);
  }
});

sock.on('error', (e) => {
  console.error(`-- error: ${e.message}`);
  console.error('   Is the simulator running?  node tools/sim_robot.js');
  process.exit(1);
});

sock.on('close', () => {
  if (watch) console.log('-- disconnected');
});