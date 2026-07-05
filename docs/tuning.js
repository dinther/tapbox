// tapbox BPM-tuning chart — development copy.
//
// Load it into the device page with:
//   http://<tapbox-ip>/?js=http://<your-pc-ip>:8000/tuning.js
// after serving this folder locally, e.g.:  python -m http.server
//
// The embedded copy in src/main.cpp (http_get_root) is the production source —
// fold changes made here back into the firmware before a release.
//
// Page globals available: mchart (canvas), mstat, mrawv, mtrkv, mstate,
// mnum2, mconf (elements with ids are window globals), plus the sliders
// [name=mfloor], [name=mcsil], [name=mcmove].
// Telemetry packet (20 Hz, CSV):
//   levelDb,confidence,beat(0=none 1=lock-tier 2=moved tempo),
//   rawBPM,trackedBPM,state,seen,moved,anchorBPM,beatAgeMs
//
// Chart: two strips. Top = BPM (raw + Link traces, scaled around the tap
// anchor so the ±20% clamp band is always in view). Bottom = confidence
// 0..1 with the lock/move slider thresholds as live dashed lines and beat
// ticks (bright green = beat moved the tempo, grey = lock-tier only).

var mHist = [], mStats = [], mLastMsg = 0, logLines = [];
// Cached once — mDraw runs at display rate, no per-frame DOM lookups
var mC = document.getElementById('mchart'), mCtx = mC.getContext('2d'),
    mW = mC.width, mH = mC.height,
    lockEl = document.querySelector('[name=mcsil]'),
    moveEl = document.querySelector('[name=mcmove]');

function mConnect() {
  var ws = new WebSocket('ws://' + location.host + '/ws');
  ws.onopen  = function () { mstat.textContent = 'live'; };
  ws.onclose = function () { mstat.textContent = 'disconnected — retrying…'; setTimeout(mConnect, 2000); };
  ws.onerror = function () { ws.close(); };
  ws.onmessage = function (ev) {
    if (ev.data.charAt(0) === 'L') {
      logLines.push(ev.data.substring(1));
      if (logLines.length > 300) logLines.shift();
      var lb = document.getElementById('logbox');
      if (lb) { lb.textContent = logLines.join('\n'); lb.scrollTop = lb.scrollHeight; }
      return;
    }
    var p = ev.data.split(',');
    // f: beat tick's sub-sample shift left (beat age / 50ms)
    var pt = { c: +p[1], r: +p[3], k: +p[4], b: +p[2], a: +p[8] || 0, f: (+p[9] || 0) / 50 };
    mHist.push(pt);
    if (mHist.length > 120) mHist.shift();   // 120 x 50ms = 6s window
    mLastMsg = performance.now();
    var st = +p[5];
    mrawv.textContent = pt.r ? pt.r.toFixed(1) : '—';
    mtrkv.textContent = pt.k ? pt.k.toFixed(1) : '—';
    mstate.textContent = st == 2 ? 'LOCKED' : st == 1 ? 'SEARCHING' : 'IDLE';
    mstate.style.color = st == 2 ? '#B7F7A5' : st == 1 ? '#e90' : '#777';
    mStats.push({ ts: Date.now(), d: +p[6], a: +p[7] });
    while (mStats.length && Date.now() - mStats[0].ts > 10000) mStats.shift();
    if (mStats.length > 1) {
      var f = mStats[0], l = mStats[mStats.length - 1];
      var dd = (l.d - f.d + 100000) % 100000, da = (l.a - f.a + 100000) % 100000;
      mnum2.innerHTML = 'Last 10s: <b>' + dd + '</b> beats &nbsp;·&nbsp; <b>' + da + '</b> moved tempo';
    }
    mconf.innerHTML = 'Confidence <b>' + (+p[1]).toFixed(2) + '</b> &nbsp;·&nbsp; Level <b>' + (+p[0]).toFixed(0) + '</b> dB';
  };
}

// frac (0..1) is progress toward the next 20Hz frame so the scroll glides.
function mDraw(frac) {
  var ctx = mCtx, W = mW, H = mH, BH = 205, CT = 215, CH = H - CT;
  ctx.clearRect(0, 0, W, H);
  var n = mHist.length, per = W / 119;
  function xOf(v) { return (v - (n >= 120 ? frac : 0)) * per; }
  var last = mHist[n - 1] || { a: 0, k: 0 };
  var ctr = last.a > 0 ? last.a : (last.k > 0 ? last.k : 120);
  var lo = ctr * 0.72, hi = ctr * 1.28;
  function yB(v) { var t = (v - lo) / (hi - lo); if (t < 0) t = 0; if (t > 1) t = 1; return BH - t * BH; }
  function yC(v) { if (v < 0) v = 0; if (v > 1) v = 1; return CT + CH - v * CH; }

  // BPM strip: clamp band + anchor line
  if (last.a > 0) {
    var y1 = yB(last.a * 1.2), y2 = yB(last.a * 0.8);
    ctx.fillStyle = 'rgba(183,247,165,0.07)'; ctx.fillRect(0, y1, W, y2 - y1);
    ctx.strokeStyle = '#555'; ctx.setLineDash([2, 4]); ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(0, yB(last.a)); ctx.lineTo(W, yB(last.a)); ctx.stroke(); ctx.setLineDash([]);
  }
  // Strip separator
  ctx.strokeStyle = '#222'; ctx.lineWidth = 1;
  ctx.beginPath(); ctx.moveTo(0, BH + 5); ctx.lineTo(W, BH + 5); ctx.stroke();
  // Beat ticks across the confidence strip (bright = moved tempo)
  for (var i = 0; i < n; i++) {
    var b = mHist[i].b;
    if (b) {
      var xt = xOf(i - mHist[i].f);
      ctx.strokeStyle = b === 2 ? 'rgba(46,204,113,0.9)' : 'rgba(136,136,136,0.55)';
      ctx.lineWidth = b === 2 ? 2 : 1;
      ctx.beginPath(); ctx.moveTo(xt, CT); ctx.lineTo(xt, H); ctx.stroke();
    }
  }
  // Threshold lines, read live from the sliders
  var lk = (+lockEl.value || 0) / 100, mv = (+moveEl.value || 0) / 100;
  ctx.setLineDash([4, 3]); ctx.lineWidth = 1;
  ctx.strokeStyle = '#888';    ctx.beginPath(); ctx.moveTo(0, yC(lk)); ctx.lineTo(W, yC(lk)); ctx.stroke();
  ctx.strokeStyle = '#2ecc71'; ctx.beginPath(); ctx.moveTo(0, yC(mv)); ctx.lineTo(W, yC(mv)); ctx.stroke();
  ctx.setLineDash([]);
  // Confidence trace
  ctx.strokeStyle = '#e90'; ctx.lineWidth = 2; ctx.beginPath();
  for (i = 0; i < n; i++) {
    var x = xOf(i), y = yC(mHist[i].c);
    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  }
  ctx.stroke();
  // BPM traces — raw (cyan, gaps where confidence-gated to 0) and Link (green)
  function trace(key, col) {
    ctx.strokeStyle = col; ctx.lineWidth = 2; ctx.beginPath(); var pen = false;
    for (var j = 0; j < n; j++) {
      var v = mHist[j][key];
      if (v > 0) { var xx = xOf(j), yy = yB(v); if (pen) ctx.lineTo(xx, yy); else { ctx.moveTo(xx, yy); pen = true; } }
      else pen = false;
    }
    ctx.stroke();
  }
  trace('r', '#0af');
  trace('k', '#B7F7A5');
}

mConnect();
(function mAnim() {
  mDraw(mLastMsg ? Math.min((performance.now() - mLastMsg) / 50, 1) : 0);
  requestAnimationFrame(mAnim);
})();
