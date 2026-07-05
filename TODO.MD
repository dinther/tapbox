Ideas (parked):
- ws_push_task (browser telemetry) currently pushes at 20Hz, matching
  mic_task's poll rate purely by coincidence — the two are independent
  loops. mic_task's 20Hz cadence must stay (real tempo-path latency: how
  fast follow/hold reacts to a confident beat). ws_push_task is purely
  cosmetic (chart/readouts) and could likely drop to ~10Hz unnoticed —
  beat ticks are already stamped with their own precise timestamp (age
  field), and confidence/BPM don't change faster than perceptible anyway.
  Would roughly halve WS traffic + httpd_queue_work/malloc churn while a
  browser tab is open. Needs the chart's hardcoded frac divisor (currently
  50ms, in mDraw's rAF loop) updated to match the new interval so the
  scroll interpolation doesn't overshoot between packets.
- A real-time AI audio feature extractor is being developed externally and
  may replace the current front-end (FFT/mel/SuperFlux/BTrack); everything
  downstream (octave fold, snap/clamp, phase-lock, tuning UI) consumes onset
  timestamps and would carry over unchanged.
- CPU headroom is currently unmeasured — no core pinning (plain xTaskCreate,
  not xTaskCreatePinnedToCore) and no FreeRTOS runtime-stats config
  (CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS not set), so there's nothing to
  read off today, unlike flash/RAM which the build reports automatically.
  If worth knowing for real: enable runtime stats and print core idle time
  over serial, or wrap the mic_task hot path (FFT→Mel→SuperFlux→BTrack) in
  esp_timer to check per-hop cost against the 62.5Hz (16ms) budget. Case is
  currently screwed shut — not urgent, just want to check at some point.
- Move-tier false negative: confidence can sit just under g_micConfMove while
  the folded candidate is rock solid (observed on real hardware, known-correct
  reading that never moved the tempo). Add a consistency fallback at the move
  check in mic_task() (~main.cpp:2223): track consecutive lock-tier beats
  whose folded `cand` stays within a tight band (e.g. ±0.3 BPM) of each other,
  accumulating a growing count rather than a one-shot pass/fail — a run of
  otherwise-quality beats should eventually overcome a marginal confidence
  shortfall on its own, so let the count satisfy the move gate once it
  crosses a threshold (e.g. 4, matching the existing lock-count convention)
  even when confidence alone doesn't. This is an OR alternative to the
  confidence check, not an extra required condition — it only makes moves
  *more* permissive for cases already known to be trustworthy, so it
  shouldn't reintroduce the old accept-window's lag (which instead made
  every move harder to pass). Reset the accumulating counter wherever
  `consec` already resets (mode change, lock loss), and any time `cand`
  jumps outside the tight band (a real break in consistency, not just a
  low-confidence read).
  Also gate this fallback to a conservative BPM range — the algorithm has
  a known tendency to hang at the ~160 BPM alias ceiling during quiet
  passages (tempo_estimator.h's ~85-160 BPM reliable range; BEAT_DETECTION.md
  §6 on aliasing above ~164 BPM). A quiet passage is exactly when a wrong
  but stable alias could rack up enough consecutive "consistent" beats to
  trip this fallback, so don't let the counter alone override confidence
  when `cand` is sitting near that high ceiling — only apply it comfortably
  away from the edge (e.g. inside the anchor's ±15% rather than the full
  ±20% clamp, or some similar margin below ~160 BPM).

Nice to have (parked):
- WiFi AP alongside Ethernet: currently Ethernet connecting calls
  wifi_stop() unconditionally, so WiFi is fully off whenever Ethernet is
  active. Ethernet (LAN8720 PHY) and WiFi are separate hardware though —
  no radio conflict like AP+STA sharing the WiFi radio — so it's feasible
  to keep an AP running purely for config-page access from a phone/laptop
  while Ethernet still carries Link as normal. Would need a decision on
  default posture (always-on AP-over-Ethernet vs. a menu toggle — leaning
  toggle, since it's a real change to how the device presents itself on
  networks) and costs a bit more power draw.

Cosmetic (parked):
- tapbox_web_config_BPM_tuner.png needs retaking to reflect the current BPM
  tuning tab (Level floor / Lock confidence / Move confidence sliders, Log
  tab)
- MANUAL.md cleanup pass once the above screenshot lands
- Do we want slider changes to NOT hit NVS flash on every drag? Currently
  /apply saves to NVS each time (flash wear, minor).
- Save Tuning button should return to the correct tab after posting.
