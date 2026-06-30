# tapbox — Audio Beat Detection

How the microphone-based BPM detection works, end to end.

This describes **Audio mode** (`MODE_AUDIO`), one of the three sync modes
(CDJ / Audio / Manual). All of the logic below lives in `mic_task()` and
`do_tap()` in `src/main.cpp`.

> Diagrams use [Mermaid](https://mermaid.js.org/) (GitHub renders them inline)
> plus ASCII where a waveform is clearer than a flowchart.

---

## 1. Design philosophy

The hard problem in automatic beat tracking is the **downbeat** — knowing which
pulse is "beat 1". tapbox sidesteps it by splitting the job between the human and
the machine:

| Job | Owner | Why |
|-----|-------|-----|
| **Tempo** (BPM) | the **microphone** | machines are good at measuring intervals |
| **Downbeat** (phase / "beat 1") | the **tap button** | humans hear musical phrasing |

So: **you tap the downbeat, the mic carries the tempo.** Your tap is *ground
truth*; the mic is only ever allowed to refine and track *around* it. This single
rule is why the system stays trustworthy — the mic can never wander off on its own.

```mermaid
flowchart LR
    A[Music in the room] -->|acoustic| MIC[INMP441 mic]
    MIC -->|tempo| LINK[Ableton Link grid]
    TAP[Your tap] -->|downbeat / anchor| LINK
    LINK --> OUT[Synced devices]
```

---

## 2. The whole pipeline at a glance

```mermaid
flowchart TD
    I2S["I2S read: 128 frames ~8 ms"] --> BP["Band-pass: DC block + low-pass"]
    BP --> EN["Frame energy = mean of lp squared"]
    EN --> BASE["Adaptive baseline (EMA)"]
    EN --> ONS{"Onset? energy above<br/>baseline×thr AND gate,<br/>AND 250 ms since last"}
    BASE --> ONS
    ONS -->|no| I2S
    ONS -->|yes| IVL["interval = now − lastOnset<br/>bpm = 60000 / interval"]
    IVL --> ARM{"Armed?<br/>(tap happened)"}
    ARM -->|no| HINT["Update display hint only"]
    ARM -->|yes| FOLD["Octave-fold toward tap anchor"]
    FOLD --> WIN{"Within window<br/>of anchor?"}
    WIN -->|no| REJECT["Reject — spurious / octave"]
    WIN -->|yes| EMA["Average clean interval (EMA)"]
    EMA --> DB{"Outside the<br/>0.4 BPM deadband?"}
    DB -->|no| FREEZE["Freeze tempo"]
    DB -->|yes| SLEW["Slew tracked BPM toward target"]
    SLEW --> CLAMP["Clamp to ±20% of tap anchor"]
    FREEZE --> APPLY
    CLAMP --> APPLY["Apply to Link: set tempo"]
    APPLY --> PLL["If locked: phase-lock nudge to nearest beat"]
```

Each stage is explained below.

---

## 3. Capturing audio (I2S)

- Mic: **INMP441** (digital I2S MEMS), `L/R` tied to GND.
- Sample rate: **16 kHz** — we only care about the kick band (< ~150 Hz), so this
  is plenty and cheap.
- INMP441 channel selection on the ESP32 is a known finicky point (L/R→GND
  *should* select the left slot, but configs don't always behave). In our
  bring-up, **mono mode returned all-zero samples on both slot settings**, while
  reading **both** slots in stereo and using only the **left** samples worked
  reliably — so that's what we do (`i += 2` through the buffer). This is an
  empirical workaround for our setup, not a documented driver bug.
- Each read returns **256 int32 samples = 128 frames ≈ 8 ms** of audio.

> ⚠️ This **8 ms read block is the resolution floor** of the whole system — see
> §11. Onsets can only be timestamped at read boundaries.

---

## 4. Band-pass filter — isolating the kick

A kick drum lives around 50–150 Hz. We isolate it per sample with a cheap
two-stage IIR filter, then measure its energy.

```
 raw sample x ──► [ DC blocker ] ──► [ low-pass ] ──► lp ──► energy += lp²
                  (high-pass)        (~150 Hz)
```

```c
x  = sample / 2^31;                 // normalize to ~[-1, 1)
y  = x - xprev + 0.995 * hp;        // DC blocker  (kills sub-bass rumble/offset)
lp = lp + 0.06 * (y - lp);          // 1-pole low-pass (~150 Hz at 16 kHz)
energy += lp * lp;                  // accumulate over the read block
```

The result is one **frame energy** value per ~8 ms block: high during a kick,
low between kicks.

---

## 5. Adaptive threshold — finding the beats

We never use an absolute loudness threshold (that would need recalibrating for
every venue). Instead a slow **baseline** tracks the running energy, and a beat
is an energy **rise above that baseline**:

```c
baseline += 0.02 * (energy - baseline);     // slow floor (~0.4 s)
onset = (energy > baseline * threshFactor)  // a clear rise...
     && (energy > gate)                     // ...above the noise floor
     && (now - lastOnset > 250 ms);         // refractory → max ~240 BPM
```

ASCII view of a few beats — `*` marks a detected onset:

```
energy
  │            ╭╮                     ╭╮                     ╭╮
  │           ╭╯╰╮                   ╭╯╰╮                   ╭╯╰╮
  │     ╭╮   ╭╯  ╰╮      ╭╮         ╭╯  ╰╮      ╭╮         ╭╯  ╰╮
  │   ╭─╯╰───╯    ╰──────╯╰─────────╯    ╰──────╯╰─────────╯    ╰──
  │ ··········· baseline × thr (adaptive) ·······························
  │
  └──────*──────────────*────────────────*────────────────────► time
       kick           kick             kick
        │◄── interval ──►│
```

- `threshFactor = 1 + thr/10` (the `thr` menu knob). Higher → only strong kicks
  count, fewer false triggers.
- `gate` is an absolute floor (the `gate` knob) so silence/room noise never locks.
- The **250 ms refractory** stops a single kick's ringing or a snare from
  double-triggering.

Each onset gives an **inter-onset interval**, and a raw BPM:

```
bpm = 60000 / interval_ms      (accepted only if 50 ≤ bpm ≤ 220)
```

---

## 6. Arming — the tap is ground truth

In Audio mode the mic is **display-only until you tap.** Your tap sets two
references used by everything downstream:

- `g_mic_tapAnchor` — the **anchor** tempo (centre of the acceptance window)
- `g_mic_tracked` — the **applied** tempo (what goes to Link)

**Tap grammar** (the box infers intent from how many taps arrive < 2 s apart):

```mermaid
flowchart TD
    T[Tap] --> N{How many taps<br/>in this session?}
    N -->|1 tap, then stop| ONE[Accept current mic BPM<br/>+ set downbeat = now]
    N -->|4+ taps| FOUR[Override tempo to tapped BPM<br/>+ re-anchor the mic]
    ONE --> ARMED[Armed - mic now tracks around anchor]
    FOUR --> ARMED
```

- **One tap** = "I agree, beat 1 is *now*" — accepts the mic's current estimate
  and sets the downbeat.
- **Four taps** = override the tempo with your own tapping, re-anchoring the mic.
- Once armed and locked, a **lone tap just re-syncs the downbeat** without
  touching the tempo (the mic owns BPM).

---

## 7. Octave fold + acceptance window — rejecting garbage

Raw detection is noisy: hi-hats, snares, and double-hits produce spurious BPMs,
and the detector often reports half/double tempo. Two guards clean this up, both
judged against the **stable tap anchor** (not the moving output — that distinction
matters, see §8):

**(a) Octave folding** — pull obvious half/double errors back toward the anchor:

```c
while (cand > anchor * 1.4) cand *= 0.5;   // 252 → 126
while (cand < anchor * 0.7) cand *= 2.0;   //  63 → 126
```

**(b) Acceptance window** — after folding, keep only candidates near the anchor:

```c
if (fabs(cand - anchor) <= anchor * window%)   // default ±10%
```

```
                 reject │   accept (±window)   │ reject
   ───────────────●─────┼──────────●───────────┼─────●───────────► BPM
                 88     113       126(anchor)  139   ...
              (folds up        the real kicks       (folds down
               to ~176,         land here            or rejected)
               rejected)
```

A detection like **88.9** (a syncopated hit) folds to ~178 and is rejected; a
genuine **125 / 128.8** sails through. This is what keeps `trk` rock-steady even
though the raw stream is full of junk.

---

## 8. Why the window anchors to the *tap*, not the output

This is subtle but important. If the window were centred on the *current tracked*
value, then a tiny downward drift would make the window admit the long-interval
(low-BPM) detections while rejecting their short-interval partners — biasing it
**further** down, which feeds back and **ratchets** the tempo away.

Centring the window on the **fixed tap anchor** keeps admission symmetric: both
the slightly-fast and slightly-slow detections get in, and they average to the
truth. The output can refine and drift, but the *gate* it must pass through stays
nailed to your tap.

---

## 9. Turning intervals into a stable tempo

Three stages convert the accepted, noisy intervals into the smooth value sent to
Link:

```mermaid
flowchart LR
    C["Accepted clean interval"] --> E["EMA of interval<br/>avg += 0.08 × diff"]
    E --> TGT["target = 60000 / avg"]
    TGT --> D{"|target − tracked|<br/>over 0.4 BPM?"}
    D -->|no| F["Freeze — no change"]
    D -->|yes| S["Slew toward target<br/>(max 0.1%/sec × slew)"]
    S --> CL["Clamp ±20% of tap anchor"]
```

1. **Interval EMA** — average the *interval* (not the BPM) of clean, un-folded
   beats. Averaging in the interval domain is unbiased and continuous, so it can
   represent e.g. 126.0 even though single reads only land on a coarse grid.
2. **Deadband (±0.4 BPM)** — once the tracked tempo is within 0.4 BPM of the
   target, **freeze it** so the displayed number stops flickering. Only genuine
   drift beyond the band moves it.
3. **Slew limit** — when it does move, cap the rate (`slew` knob, in 0.1 %/sec)
   so a stray reading can't yank it; a real DJ tempo ride sails through.
4. **Clamp (±20 % of the tap anchor)** — a hard safety rail. Even if everything
   else misbehaved, the mic can **never** fall into a half/double-tempo basin and
   lock there. Re-tap to move outside the rail.

---

## 10. Applying to Link — tempo *and* phase-lock

```c
abl_link_set_tempo(session, tracked, t);          // always: tempo
if (locked) {                                      // only once locked:
    beat    = abl_link_beat_at_time(session, t);
    nearest = round(beat);
    fixed   = beat + 0.15 * (nearest - beat);      // low-gain phase nudge
    abl_link_force_beat_at_time(session, fixed, t);
}
```

Tempo alone isn't enough: a 0.5 BPM error integrates into phase drift, and the
downbeat slowly walks off the music. So once **locked** (4 consecutive accepted
beats), a low-gain **phase-locked loop** gently pulls the beat grid so each
detected kick sits on the **nearest beat**:

```
                beat grid (Link)
   ──┬───────────┬───────────┬───────────┬──────►
     │           │           │           │
     ▼           ▼           ▼           ▼
   kick        kick        kick'       kick
                            └─ drifted a little late
                               → nudge grid 15% toward it each beat
```

Correcting to the **nearest** beat (never a whole bar) means the *bar position
you tapped is preserved* — your tap still decides which beat is "1"; the PLL only
keeps the pulse aligned. This is what holds the downbeat steady indefinitely,
even with the small residual tempo error.

---

## 11. Lock state & lifecycle

```mermaid
stateDiagram-v2
    [*] --> DisplayOnly: enter Audio mode
    DisplayOnly --> Armed: tap (sets anchor + downbeat)
    Armed --> Locked: 4 consecutive accepted beats
    Locked --> Armed: 3.5 s with no onset (lost the beat)
    Armed --> DisplayOnly: leave Audio mode
    Locked --> DisplayOnly: leave Audio mode
    Locked --> Locked: re-tap = re-sync downbeat
```

- **Display-only**: mic measures but does nothing to Link.
- **Armed**: tap anchored the tracker; mic is refining tempo.
- **Locked** (`lock=1`, shown by the `A` indicator + bottom mode bar): stable
  tempo, phase-lock active.
- Lock drops after **3.5 s** with no detected onset (a breakdown / silence), then
  re-acquires when the beat returns.

---

## 12. The resolution floor (and option 3)

The single biggest limit is §3's **8 ms read block**: onsets are timestamped to
that grid, so one interval resolves to ~2 BPM at 126. The EMA + deadband average
this down to a stable display, but a small residual (~±0.5 BPM and a slight bias)
remains. Genuinely beating it needs **finer audio framing** — either smaller I2S
reads or sub-frame interpolation of the threshold crossing — which is the planned
"option 3" enhancement.

Note this is mostly about the **displayed number**: the phase-lock (§10) keeps the
actual Ableton Link grid glued to the music regardless of that cosmetic wobble.

---

## 13. Tuning knobs (Audio-mode menu)

| Menu | Variable | Default | What it does |
|------|----------|---------|--------------|
| `thr`  | `g_micThr`  | 8 (→1.8×) | onset threshold = baseline × (1 + thr/10). Higher = only strong kicks |
| `gate` | `g_micGate` | 5 | absolute noise-gate floor (× 1e-5). Higher = ignores quieter signals |
| `uind` | `g_micWin`  | 10 | acceptance window ± % around the tap anchor. Keep wide enough to admit the natural spread |
| `SLEu` | `g_micSlew` | 10 | tempo slew limit, in 0.1 %/sec. Higher = follows drift faster but jitters more |

Fixed constants (in `mic_task`): LP α = 0.06, baseline α = 0.02, refractory =
250 ms, interval EMA α = 0.08, deadband = 0.4 BPM, clamp = ±20 %, PLL gain = 0.15,
lock = 4 beats, lock timeout = 3.5 s.

---

## 14. Source map

| Piece | Location (`src/main.cpp`) |
|-------|---------------------------|
| I2S init (stereo, left slot) | `init_i2s_mic()` |
| Capture → band-pass → onset → tracking → phase-lock | `mic_task()` |
| Tap grammar (arm / override / re-sync) | `do_tap()` |
| Mode + lock display (bars, `A` indicator) | `update_display()` |
| Tuning knobs (menu) | `MENU_MTHR / MGATE / MWIN / MSLEW` |
