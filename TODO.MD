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

Cosmetic (parked):
- tapbox_web_config_BPM_tuner.png needs retaking to reflect the current BPM
  tuning tab (Level floor / Lock confidence / Move confidence sliders, Log
  tab)
- MANUAL.md cleanup pass once the above screenshot lands
- Do we want slider changes to NOT hit NVS flash on every drag? Currently
  /apply saves to NVS each time (flash wear, minor).
- Save Tuning button should return to the correct tab after posting.
