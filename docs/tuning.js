// tapbox BPM-tuning chart — development copy.
//
// Load it into the device page with:
//   http://<tapbox-ip>/?js=http://<your-pc-ip>:8000/tuning.js
// after serving this folder locally, e.g.:  python -m http.server
//
// The embedded copy in src/main.cpp (http_get_root) is the production source —
// fold changes made here back into the firmware before a release.
//
// Page globals available: mchart (canvas), mstat, mrawv, mtrkv, mstate, mnum2
// (elements with ids are window globals), and the [name=mwin] slider.
// Telemetry packet (20 Hz, CSV):
//   e,baseline,threshold,gate,onset(0/1/2),rawBPM,trackedBPM,state,
//   detected,accepted,anchorBPM,devBPM,onsetAgeMs

var mStats = [], mAnchor = 0;
var currentData = null;
// Cached once — mDraw runs at display rate, no per-frame DOM lookups
var canvas = document.getElementById('mchart'), ctx = canvas.getContext('2d'),
    mW = canvas.width, mH = canvas.height, mWinEl = document.querySelector('[name=mwin]');
ctx.imageSmoothingEnabled = false;
ctx.lineCap = "round";
ctx.lineJoin = "round";
ctx.lineWidth = 2;
ctx.strokeStyle = "#00ccff";
let pixelsPerSecond = 160;
let accumulated = 0;
let lastTime = performance.now();
let beatPx = NaN;      // pixels until the next expected-beat CENTER reaches the right edge
let winEarlyPx = 0, winLatePx = 0, periodPx = 0; // acceptance-band geometry, locked at seed time
let pendW = null;      // seed that arrived while the current band was painting — applied when it finishes
let sinceLast = 0;     // pixels shifted since the last trace point was painted
let lastY = null;      // y of that point (now sinceLast px left of the edge)

// Fixed log scale, 5 decades (1..1e5 chart units)
function yv(v) {
    return canvas.height -
        Math.max(0, Math.min(Math.log10(Math.max(v, 1)) / 5, 1)) * canvas.height;
}


// Fill background once
ctx.fillStyle = "black";
ctx.fillRect(0,0,canvas.width,canvas.height);

function mConnect() {
  var ws = new WebSocket('ws://' + location.host + '/ws');
  ws.onopen  = function () { 
    mstat.textContent = 'live';
            ctx.beginPath();
            ctx.moveTo(0, 0);
            ctx.lineTo(canvas.width - 1, canvas.height-1);
            ctx.stroke();
  };
  ws.onclose = function () { mstat.textContent = 'disconnected — retrying…'; setTimeout(mConnect, 2000); };
  ws.onerror = function () { ws.close(); };
  ws.onmessage = function (ev) {
    handleMessage(ev.data);
  };
}

function handleMessage(data){
    var p = data.split(',');
    var pt = { e: +p[0], b: +p[1], t: +p[2], g: +p[3], o: +p[4] };
    currentData = p;
    var raw = +p[5], trk = +p[6], st = +p[7];
    mAnchor = +p[10] || 0;
    // Trace: paint one segment per packet — from where the previous point now
    // sits (scrolled sinceLast px left) to this value at the right edge.
    // Nothing is painted while waiting, so slopes are real, not steps.
    var ny = yv(pt.e);
    if (lastY !== null && sinceLast <= canvas.width) {
        ctx.strokeStyle = "#00ccff"; ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(canvas.width - 1 - sinceLast, lastY);
        ctx.lineTo(canvas.width - 1, ny);
        ctx.stroke();
    }
    lastY = ny; sinceLast = 0;

    // Onset dot at its true position: onset-age (p[12] ms) left of the edge
    if (pt.o) {
        var dx = canvas.width - 1 - (+p[12] || 0) * pixelsPerSecond / 1000;
        if (pt.o === 2) {
            ctx.fillStyle = '#2ecc71';
            ctx.beginPath(); ctx.arc(dx, ny, 3.5, 0, 7); ctx.fill();
        } else {
            ctx.strokeStyle = '#888'; ctx.lineWidth = 1.5;
            ctx.beginPath(); ctx.arc(dx, ny, 3, 0, 7); ctx.stroke();
        }
    }
    // An accepted beat seeds the next acceptance window. Geometry is locked
    // from what is known NOW: center one period out, early/late edges from the
    // ±window slider mapped into time (asymmetric — a fixed BPM tolerance is a
    // longer stretch on the slow side than the fast side).
    if (pt.o === 2 && mAnchor > 0) {
      var u = +mWinEl.value || 0, Pms = 60000 / mAnchor, k = pixelsPerSecond / 1000;
      var w = { px:     (Pms - (+p[12] || 0)) * k,
                early:  (Pms - 60000 / (mAnchor + u)) * k,
                late:   ((mAnchor > u ? 60000 / (mAnchor - u) : Pms * 2) - Pms) * k,
                period: Pms * k };
      // If the current band is still painting (the accepted beat landed in it),
      // queue the seed — replacing beatPx now would truncate the band's tail.
      if (!isNaN(beatPx) && beatPx >= -winLatePx && beatPx <= winEarlyPx + 2) pendW = w;
      else { beatPx = w.px; winEarlyPx = w.early; winLatePx = w.late; periodPx = w.period; pendW = null; }
    }
    if (!(mAnchor > 0)) { beatPx = NaN; pendW = null; }
    mrawv.textContent = raw ? raw.toFixed(1) : '—';
    mtrkv.textContent = trk ? trk.toFixed(1) : '—';
    mstate.textContent = st == 2 ? 'LOCKED' : st == 1 ? 'SEARCHING' : 'IDLE';
    mstate.style.color = st == 2 ? '#B7F7A5' : st == 1 ? '#e90' : '#777';
    mStats.push({ ts: Date.now(), d: +p[8], a: +p[9] });
    while (mStats.length && Date.now() - mStats[0].ts > 10000) mStats.shift();
    if (mStats.length > 1) {
      var f = mStats[0], l = mStats[mStats.length - 1];
      var dd = (l.d - f.d + 100000) % 100000, da = (l.a - f.a + 100000) % 100000;
      mnum2.innerHTML = 'Last 10s: <b>' + dd + '</b> beats detected &nbsp;·&nbsp; <b>' + da + '</b> accepted';
    }  
}

// Drawn by a requestAnimationFrame loop: the accumulator converts elapsed
// time into whole-pixel shifts, carrying the fractional remainder forward.
function mDraw() {

    let now = performance.now();
    accumulated += (now - lastTime) * pixelsPerSecond / 1000;
    lastTime = now;
    // Hidden tab stops rAF; on return the backlog can be huge — one screen is enough
    if (accumulated > canvas.width) accumulated = canvas.width;

    while (accumulated >= 1) {

        accumulated--;
        sinceLast++;   // the last painted trace point moves one px further left

        // Shift canvas left one pixel
        ctx.drawImage(
            canvas,
            1, 0,
            canvas.width - 1, canvas.height,
            0, 0,
            canvas.width - 1, canvas.height
        );

        // Fresh right-edge column
        ctx.fillStyle = "#111";
        ctx.fillRect(canvas.width - 1, 0, 1, canvas.height);

        // Decade gridlines (10, 100, 1k, 10k) — painted per column, scroll along
        ctx.fillStyle = "#1e1e1e";
        for (let d = 1; d < 5; d++) ctx.fillRect(canvas.width - 1, yv(Math.pow(10, d)), 1, 1);

        if (currentData !== null) {
            // Threshold & gate — 1px ticks per column, so under blit they become
            // true history curves (you see the adaptive baseline breathe)
            ctx.fillStyle = "#e90";
            ctx.fillRect(canvas.width - 1, yv(+currentData[2]), 1, 2);
            ctx.fillStyle = "#c33";
            ctx.fillRect(canvas.width - 1, yv(+currentData[3]), 1, 2);
        }

        // Acceptance window: tint each column while `now` is inside the band —
        // the window paints itself as its time passes, no bookkeeping. Dashed
        // center column = the expected beat. If nothing consumed the window,
        // re-arm one period later with the same locked geometry.
        if (!isNaN(beatPx)) {
            beatPx--;
            if (pendW) pendW.px--;      // the queued window's clock keeps running too
            if (beatPx <= winEarlyPx && beatPx >= -winLatePx) {
                ctx.fillStyle = "rgba(183,247,165,0.18)";
                ctx.fillRect(canvas.width - 1, 0, 1, canvas.height);
                if (beatPx > -1 && beatPx <= 0) {              // center column
                    ctx.fillStyle = "rgba(183,247,165,0.9)";
                    for (let yy = 0; yy < canvas.height; yy += 6)
                        ctx.fillRect(canvas.width - 1, yy, 1, 3);   // vertical dashes
                }
            }
            if (beatPx < -winLatePx) {
                // Band finished: switch to the queued seed if one arrived,
                // otherwise re-arm one period ahead on the old geometry
                if (pendW) {
                    beatPx = pendW.px; winEarlyPx = pendW.early;
                    winLatePx = pendW.late; periodPx = pendW.period; pendW = null;
                } else beatPx += periodPx;
            }
        }
    }
}

mConnect();
(function mAnim() {
  mDraw();
  requestAnimationFrame(mAnim);
})();
