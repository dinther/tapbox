Done (v1.9.0):
- WiFi static IP support added (was Ethernet-only) — Lan./IP/Sub./Hub. now apply to whichever interface is active
- On-device menu trimmed to: Beat, Led, node, Lan., Addr, rSet, vEr, done
- IP/Sub./Hub. octets and the four mic-tuning params (uind/SLEu/thr/gAte) moved to web config page only
- Web config: added Access Point option to network mode, added mic-tuning fields
- "nud" removed entirely — menu item, web field, and NVS key all gone
- OSC: /nudge_up and /nudge_down replaced with a single /nudge <ms> (signed, +/-;
  no argument -> default 20ms)
- Default sync mode switched from CDJ to Audio (Aud)
- Fixed: long-pressing select from edit mode now backs out to menu nav (discarding
  the in-progress edit) instead of exiting the whole menu
- Mode bar moved from before the beat counter to the empty digit behind it
- AP password: 8-digit numeric derived from the MAC with a mixing step (not raw
  MAC digits). Same PIN gates HTTP Basic Auth on the whole web config page (was
  wide open to anyone on the network) in both AP and normal network mode. PIN is
  shown on the display in every network mode's boot/reconnect scroll and via the
  Addr menu item. Fixed: the PIN now fills the whole 8-digit display and holds
  there for 6 seconds instead of scrolling straight past — it was unreadable.
- Tried tapbox.local (ESP-IDF mdns component) to keep the web login working
  across changing DHCP IPs. Got Ethernet<->STA switching working correctly
  (mdns_netif_action on every interface change), but AP mode never reliably
  reached clients (ESP32 SoftAP multicast-delivery limitation) and added
  ongoing complexity for a device that isn't launched yet. Pulled out entirely
  — back to plain IP addresses everywhere, shown on the display as before.
- Live audio-tuning UI: added a WebSocket (/ws, CONFIG_HTTPD_WS_SUPPORT enabled
  in sdkconfig.defaults) streaming energy/baseline/threshold/gate/onset at 20 Hz
  to a live canvas chart on the web page (blue=energy, orange dashed=threshold,
  red dashed=gate, green dots=detected beats). The four mic-tuning fields are
  now sliders that apply live (throttled 200ms, via /apply) instead of requiring
  a Save button press.
  GOTCHA that cost several flashes: ESP-IDF 6.x completes the WS handshake
  itself and never calls the uri handler for the handshake GET (httpd_uri.c
  "do not call the uri->handler") — so the classic IDF 5.x pattern of capturing
  the client fd in the handler silently gets nothing. Fix: enumerate clients
  per-push with httpd_get_client_list() + httpd_ws_get_fd_info() ==
  HTTPD_WS_CLIENT_WEBSOCKET, sending to every connected WS client. Also: WS
  handshake is not gated by Basic Auth (browsers don't reliably attach
  credentials to WS upgrades); channel is read-only telemetry so nothing to
  protect. Verified end-to-end with a raw Python WS client: ~20Hz frames.

Cosmetic (parked):
- display.jpg needs retaking — the mode bar moved position on the physical layout
- tapbox_web_config_BPM_tuner.png needs retaking — Kick filter slider removed,
  Accept test A/B selector removed, new Log tab added (DSP pipeline rewrite,
  see BEAT_DETECTION.md §2)
- MANUAL.md cleanup pass once the above lands
- DONE: BPM tuning graph now plots on a fixed log scale (5 decades, faint
  decade gridlines) — never rescales. Bonus: the log gate slider now maps
  linearly to the red line's height.
- ANSWERED: yes, tuning parameters apply in real time — each slider posts to
  /apply (throttled to one request per 200ms) and the detector reads the
  values every ~4ms mic block. No save needed for them to take effect; Save
  Tuning only matters for... nothing actually, /apply already persists to NVS.
- Do we want slider changes to NOT hit NVS flash on every drag? Currently
  /apply saves to NVS each time (flash wear, minor).
- DONE: added a 5th tuning slider — Kick filter (60-300Hz), the low-pass
  cutoff that decides what counts as "kick" energy before the other four
  controls ever see it. Recomputed live each mic block (1-exp(-2*pi*fc/fs)),
  default 150Hz (unchanged from the old fixed constant). Persisted as "mfreq".
- Click on "Save Tuning" should at least return to the correct tab. Perhaps this is where we should save params in real-time
- DONE: Measured BPM and Link BPM moved above the chart as large (30px) tabular
  numbers, with a fixed-width state badge (IDLE/SEARCHING/LOCKED, color-coded)
  next to them.
- DONE: fixed layout jitter — big BPM numbers and the state badge are
  fixed-width with tabular-nums, so digit-count changes (e.g. 89.7 -> 128.3)
  or state-text-length changes (e.g. locked -> searching) no longer reflow the
  page. Last-10s detected/accepted counts below the chart got the same
  treatment.

Bugs:
- Fixed: lock stayed on with 0 accepted beats (found by setting Onset
  threshold to minimum: 40 detected, 0 accepted, lock never dropped). The
  lock-timeout compared against lastOnset, which was refreshed on every raw
  detection, not just accepted ones — so a detector spamming false onsets on
  noise kept the "still fresh" timer alive forever even though nothing was
  actually reinforcing the tracked tempo. lastOnset now only updates when a
  beat is accepted (passes the accept window).
- Fixed the overcorrection from the fix above ("hardly locking now"): the real
  weakness was that acceptance judged the interval between CONSECUTIVE raw
  detections — one spurious hit (hi-hat/snare/double-fire) between two kicks
  splits the clean kick-to-kick interval into two fragments that both fail the
  tempo test. Typical field numbers: 29 detected / 6 accepted per 10s, so
  acceptance gaps regularly exceeded the 3.5s hold and the lock flickered.
  Two-part remedy: (1) each detection is now judged by its spacing from the
  last ACCEPTED beat (beat-chain reference; the octave fold absorbs 2-4 beat
  spans; falls back to consecutive spacing before the first acceptance or when
  the chain goes stale), so junk between kicks no longer shatters the chain
  and acceptance rate ~= kick rate; (2) the lock hold is now 6 beat-periods
  (min 3.5s) instead of a flat 3.5s, so slow tempos get the same tolerance in
  musical time. The zero-accept torture test still unlocks correctly.
- Practical baseline after all tuning: 30-50% of beats accepted depending on
  the song. That's sufficient for stable lock/tracking (one accepted beat per
  1-2s vs a 6-beat hold), and roughly the ceiling for energy-band onset
  detection — it can't tell a kick from an off-grid bass hit. A real-time AI
  audio feature extractor is being developed (externally) that may replace
  the front-end; everything downstream (accept window, beat chain, folding,
  slew, phase-lock, tuning UI) consumes onset timestamps and would carry over
  unchanged.

