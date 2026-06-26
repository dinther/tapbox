"""
cdj_sim_web.py  —  Pioneer Pro DJ Link simulator with browser GUI

Opens a control page at http://localhost:8080 and broadcasts genuine
0x28 beat packets on UDP port 50001, exactly as a CDJ-2000 would.

Usage:
    python tools/cdj_sim_web.py [bpm] [player]
"""

import http.server
import json
import socket
import struct
import sys
import threading
import time
import webbrowser

HTTP_PORT     = 8080
CDJ_PORT      = 50001
BROADCAST     = '255.255.255.255'
SEND_INTERVAL = 0.030        # 30 ms — real CDJ cadence
NEUTRAL_PITCH = 0x00100000   # 1 048 576, no pitch adjustment
MAGIC         = bytes([0x51, 0x73, 0x70, 0x74, 0x31, 0x57, 0x6d, 0x4a, 0x4f, 0x4c])

state = {'bpm': 120.0, 'player': 1, 'running': True, 'downbeat': False}
state_lock = threading.Lock()

# ── UDP broadcaster ────────────────────────────────────────────────────────────

def build_beat_packet(bpm, beat_in_bar, player):
    raw_bpm = max(0, min(0xFFFF, int(round(bpm * 100))))
    beat_ms = int(60000.0 / bpm)
    bar_ms  = beat_ms * 4
    pkt = bytearray(96)
    pkt[0x00:0x0a] = MAGIC
    pkt[0x0a] = 0x28
    name = b'CDJ-sim\x00'
    pkt[0x0b:0x0b + len(name)] = name
    pkt[0x20] = 0x00
    pkt[0x21] = player
    struct.pack_into('>H', pkt, 0x22, 0x003C)
    struct.pack_into('>I', pkt, 0x24, beat_ms)
    struct.pack_into('>I', pkt, 0x28, beat_ms * 2)
    struct.pack_into('>I', pkt, 0x2c, bar_ms)
    struct.pack_into('>I', pkt, 0x30, beat_ms * 4)
    struct.pack_into('>I', pkt, 0x34, bar_ms * 2)
    struct.pack_into('>I', pkt, 0x38, beat_ms * 8)
    for i in range(0x3c, 0x54):
        pkt[i] = 0xFF
    struct.pack_into('>I', pkt, 0x54, NEUTRAL_PITCH)
    struct.pack_into('>H', pkt, 0x5a, raw_bpm)
    pkt[0x5c] = beat_in_bar
    pkt[0x5f] = player
    return bytes(pkt)

def udp_loop():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.bind(('', 0))
    beat_idx  = 0
    next_send = time.monotonic()
    while True:
        with state_lock:
            running = state['running']
            bpm     = state['bpm']
            player  = state['player']
        if running:
            now = time.monotonic()
            if now >= next_send:
                with state_lock:
                    if state['downbeat']:
                        beat_idx = 0
                        state['downbeat'] = False
                pkt = build_beat_packet(bpm, beat_idx + 1, player)
                sock.sendto(pkt, (BROADCAST, CDJ_PORT))
                beat_idx  = (beat_idx + 1) % 4
                next_send += SEND_INTERVAL
            else:
                time.sleep(max(0.0, next_send - now - 0.001))
        else:
            beat_idx  = 0
            next_send = time.monotonic()
            time.sleep(0.05)

# ── HTML page ──────────────────────────────────────────────────────────────────

HTML = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CDJ Simulator</title>
<style>
  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

  body {
    background: #0d0d0d;
    color: #fff;
    font-family: 'Segoe UI', system-ui, sans-serif;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    min-height: 100vh;
    gap: 28px;
    padding: 24px;
  }

  h1 {
    font-size: 14px;
    letter-spacing: 4px;
    text-transform: uppercase;
    color: #666;
  }

  /* BPM display */
  #bpm-display {
    font-size: 96px;
    font-weight: 700;
    font-variant-numeric: tabular-nums;
    letter-spacing: -2px;
    color: #ff5500;
    text-shadow: 0 0 40px #ff550066;
    font-family: 'Courier New', monospace;
    min-width: 280px;
    text-align: center;
    line-height: 1;
  }

  /* Beat indicators */
  #beats {
    display: flex;
    gap: 14px;
  }
  .beat {
    width: 48px;
    height: 48px;
    border-radius: 50%;
    background: #1e1e1e;
    border: 2px solid #333;
    transition: background 0.04s, box-shadow 0.04s;
  }
  .beat.active {
    background: #ff5500;
    border-color: #ff7733;
    box-shadow: 0 0 20px #ff550099;
  }
  .beat.beat1.active {
    background: #ffaa00;
    border-color: #ffcc44;
    box-shadow: 0 0 20px #ffaa0099;
  }

  /* Player selector */
  #player-row {
    display: flex;
    gap: 10px;
    align-items: center;
  }
  .player-label {
    font-size: 12px;
    letter-spacing: 2px;
    color: #666;
    margin-right: 6px;
  }
  .player-btn {
    width: 44px;
    height: 44px;
    border-radius: 8px;
    border: 2px solid #333;
    background: #1a1a1a;
    color: #888;
    font-size: 18px;
    font-weight: 700;
    cursor: pointer;
    transition: all 0.15s;
  }
  .player-btn:hover { border-color: #555; color: #ccc; }
  .player-btn.active {
    background: #ff5500;
    border-color: #ff7733;
    color: #fff;
    box-shadow: 0 0 14px #ff550066;
  }

  /* BPM controls */
  #bpm-controls {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 12px;
    width: 100%;
    max-width: 360px;
  }

  #slider {
    width: 100%;
    accent-color: #ff5500;
    cursor: pointer;
  }

  #nudge-row {
    display: flex;
    gap: 8px;
  }
  .nudge-btn {
    padding: 10px 20px;
    border-radius: 8px;
    border: 2px solid #333;
    background: #1a1a1a;
    color: #ccc;
    font-size: 15px;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.12s;
    min-width: 60px;
  }
  .nudge-btn:hover { border-color: #ff5500; color: #fff; background: #2a1a10; }
  .nudge-btn:active { transform: scale(0.95); }

  /* Start / Stop */
  #toggle {
    width: 160px;
    height: 52px;
    border-radius: 10px;
    border: none;
    font-size: 16px;
    font-weight: 700;
    letter-spacing: 2px;
    text-transform: uppercase;
    cursor: pointer;
    transition: all 0.15s;
  }
  #toggle.running {
    background: #cc2200;
    color: #fff;
    box-shadow: 0 0 20px #cc220066;
  }
  #toggle.stopped {
    background: #1a7a1a;
    color: #fff;
    box-shadow: 0 0 20px #1a7a1a66;
  }
  #toggle:hover { filter: brightness(1.15); }
  #toggle:active { transform: scale(0.97); }

  #downbeat-btn {
    width: 160px;
    height: 52px;
    border-radius: 10px;
    border: 2px solid #ff5500;
    background: transparent;
    color: #ff5500;
    font-size: 14px;
    font-weight: 700;
    letter-spacing: 2px;
    text-transform: uppercase;
    cursor: pointer;
    transition: all 0.15s;
  }
  #downbeat-btn:hover { background: #ff550022; }
  #downbeat-btn:active { transform: scale(0.97); background: #ff550044; }

  #status {
    font-size: 12px;
    color: #444;
    letter-spacing: 1px;
  }
</style>
</head>
<body>

<h1>CDJ Simulator &mdash; Pro DJ Link</h1>

<div id="bpm-display">120.0</div>

<div id="beats">
  <div class="beat beat1" id="b0"></div>
  <div class="beat" id="b1"></div>
  <div class="beat" id="b2"></div>
  <div class="beat" id="b3"></div>
</div>

<div id="player-row">
  <span class="player-label">PLAYER</span>
  <button class="player-btn active" onclick="setPlayer(1)">1</button>
  <button class="player-btn" onclick="setPlayer(2)">2</button>
  <button class="player-btn" onclick="setPlayer(3)">3</button>
  <button class="player-btn" onclick="setPlayer(4)">4</button>
</div>

<div id="bpm-controls">
  <input id="slider" type="range" min="60" max="200" step="0.1" value="120" oninput="sliderMove(this.value)">
  <div id="nudge-row">
    <button class="nudge-btn" onclick="nudge(-1)">&#8722;1</button>
    <button class="nudge-btn" onclick="nudge(-0.1)">&#8722;0.1</button>
    <button class="nudge-btn" onclick="nudge(+0.1)">+0.1</button>
    <button class="nudge-btn" onclick="nudge(+1)">+1</button>
  </div>
</div>

<button id="toggle" class="running" onclick="toggleRunning()">Stop</button>
<button id="downbeat-btn" onclick="setDownbeat()">&#8635; Downbeat</button>

<div id="status">Broadcasting to UDP port 50001</div>

<script>
  let bpm     = 120.0;
  let player  = 1;
  let running = true;
  let beatIdx = 0;
  let beatTimer = null;

  function fmt(b) {
    return b.toFixed(1);
  }

  function updateDisplay() {
    document.getElementById('bpm-display').textContent = fmt(bpm);
    document.getElementById('slider').value = bpm;
  }

  function setPlayer(p) {
    player = p;
    document.querySelectorAll('.player-btn').forEach((b, i) => {
      b.classList.toggle('active', i + 1 === p);
    });
    send();
  }

  function nudge(delta) {
    bpm = Math.max(20, Math.min(400, Math.round((bpm + delta) * 10) / 10));
    updateDisplay();
    restartBeatTimer();
    send();
  }

  function sliderMove(val) {
    bpm = parseFloat(parseFloat(val).toFixed(1));
    updateDisplay();
    restartBeatTimer();
    send();
  }

  function toggleRunning() {
    running = !running;
    const btn = document.getElementById('toggle');
    btn.textContent = running ? 'Stop' : 'Start';
    btn.className   = 'toggle ' + (running ? 'running' : 'stopped');
    if (running) restartBeatTimer(); else clearBeatTimer();
    send();
  }

  // ── Beat animation (client-side, matches 30 ms UDP cadence) ─────────────────

  function clearBeatTimer() {
    if (beatTimer) { clearInterval(beatTimer); beatTimer = null; }
    document.querySelectorAll('.beat').forEach(b => b.classList.remove('active'));
  }

  function restartBeatTimer() {
    clearBeatTimer();
    if (!running) return;
    const interval = (60000 / bpm);
    beatIdx = 0;
    tickBeat();
    beatTimer = setInterval(tickBeat, interval);
  }

  function tickBeat() {
    document.querySelectorAll('.beat').forEach((b, i) => {
      b.classList.toggle('active', i === beatIdx);
    });
    beatIdx = (beatIdx + 1) % 4;
  }

  // ── Server sync ──────────────────────────────────────────────────────────────

  function send() {
    fetch('/set', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ bpm, player, running })
    });
  }

  function setDownbeat() {
    fetch('/downbeat', { method: 'POST' });
    // Flash the button briefly as confirmation
    const btn = document.getElementById('downbeat-btn');
    btn.style.background = '#ff5500';
    btn.style.color = '#fff';
    setTimeout(() => { btn.style.background = ''; btn.style.color = ''; }, 200);
  }

  // Keyboard shortcuts
  document.addEventListener('keydown', e => {
    if (e.key === 'ArrowUp'    || e.key === '+') nudge(1);
    if (e.key === 'ArrowDown'  || e.key === '-') nudge(-1);
    if (e.key === 'ArrowRight' || e.key === ']') nudge(0.1);
    if (e.key === 'ArrowLeft'  || e.key === '[') nudge(-0.1);
    if (e.key === ' ') { e.preventDefault(); toggleRunning(); }
  });

  // Init
  fetch('/state').then(r => r.json()).then(s => {
    bpm    = s.bpm;
    player = s.player;
    running = s.running;
    updateDisplay();
    setPlayer(player);
    const btn = document.getElementById('toggle');
    btn.textContent = running ? 'Stop' : 'Start';
    btn.className   = 'toggle ' + (running ? 'running' : 'stopped');
    if (running) restartBeatTimer();
  });
</script>
</body>
</html>
"""

# ── HTTP handler ───────────────────────────────────────────────────────────────

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass  # silence request log

    def do_GET(self):
        if self.path == '/state':
            with state_lock:
                body = json.dumps(state).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', len(body))
            self.end_headers()
            self.wfile.write(body)
        else:
            body = HTML.encode()
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', len(body))
            self.end_headers()
            self.wfile.write(body)

    def do_POST(self):
        if self.path == '/downbeat':
            with state_lock:
                state['downbeat'] = True
            self.send_response(204)
            self.end_headers()
            return
        length = int(self.headers.get('Content-Length', 0))
        data   = json.loads(self.rfile.read(length))
        with state_lock:
            state['bpm']     = max(20.0, min(400.0, float(data.get('bpm',    state['bpm']))))
            state['player']  = max(1,    min(4,     int(  data.get('player', state['player']))))
            state['running'] = bool(data.get('running', state['running']))
        self.send_response(204)
        self.end_headers()

# ── Entry point ────────────────────────────────────────────────────────────────

if __name__ == '__main__':
    bpm    = float(sys.argv[1]) if len(sys.argv) > 1 else 120.0
    player = int(sys.argv[2])   if len(sys.argv) > 2 else 1

    with state_lock:
        state['bpm']    = max(20.0, min(400.0, bpm))
        state['player'] = max(1, min(4, player))

    t = threading.Thread(target=udp_loop, daemon=True)
    t.start()

    url = f'http://localhost:{HTTP_PORT}'
    print(f'\n  CDJ Simulator running at {url}')
    print(f'  Broadcasting to UDP port {CDJ_PORT}')
    print(f'  Ctrl-C to stop\n')
    webbrowser.open(url)

    server = http.server.ThreadingHTTPServer(('', HTTP_PORT), Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print('\n  Stopped.')
