# Research: BeatNet Evaluation

Evaluation of [BeatNet](https://github.com/mjhydri/BeatNet) — a CRNN + particle-filter/DBN
neural beat, downbeat, tempo, and meter tracker — as a possible reference or idea source for
improving tapbox's own beat detection, prompted by known low-confidence cases in the current
FFT/Mel/SuperFlux + BTrack pipeline (see `BEAT_DETECTION.md`) on dense, broadband club mixes.

**BeatNet is a desktop evaluation reference only, not a port candidate.** Its stack (PyTorch,
madmom, librosa) has no path onto the WT32-ETH01. The question this evaluation answers is
whether its detections are good enough to be worth mining for architectural ideas — not whether
it can run on tapbox itself.

## Tool

A standalone CLI wrapper (`beatnet_demo.py`, not kept in the repo — this evaluation was a
one-off, and the notes below are what's worth keeping) ran BeatNet against files or a live mic
and reported beat/downbeat timing stats. It needed its own Python 3.9 venv because madmom 0.16.1
requires a C compiler and has real compatibility issues on newer Python/NumPy. Getting a working
install also required overriding a few of BeatNet's own package pins (it downgrades numpy and
drags in an ancient numba, both incompatible with madmom's compiled extensions as built). If this
is revisited, rebuilding that environment from BeatNet's own README is straightforward — the
install-order gotchas are the only non-obvious part.

BeatNet ships three pretrained models (no others available short of training your own via its
included training pipeline):

| model | trained on |
|---|---|
| 1 | GTZAN |
| 2 | Ballroom |
| 3 | Rock Corpus |

And two inference decoders:

- **PF** (particle filter) — causal, the only mode comparable to tapbox's own always-causal
  BTrack.
- **DBN** (dynamic Bayesian network) — non-causal, needs the whole track buffered first. A
  best-case quality ceiling, not something a live device could run.

## Method

Ran all three models under both decoders against two tracks:

- **"Sweet Caroline" (Division 4 Remix)** — the known-hard case: a dense club remix where
  tapbox's own pipeline tracks at low confidence despite an audibly rock-solid beat.
- **"Born This Way" (Lady Gaga)** — a canonical, well-produced four-on-the-floor pop track, as
  an easier baseline.

For each run: instantaneous BPM from consecutive beat timestamps (median/std, and fraction
within ±3 BPM of the median), an outlier count (beats implying a tempo more than 20 BPM from the
median — i.e. likely misfires rather than steady jitter), and downbeat-to-downbeat interval
distribution (to check whether it's actually finding the real bar, not just *a* beat).

## Results

### "Sweet Caroline" (Division 4 Remix) — the hard case

| config | causal? | beats | BPM std | within ±3 BPM | downbeat pattern |
|---|---|---|---|---|---|
| model 1, PF | yes | 426 | 35.31 | 52% | flip-flops between bar lengths |
| model 2, PF | yes | 547 | 24.73 | 60% | flip-flops, ~50/50 half- vs. full-bar |
| model 3, DBN | no | 663 | 3.22 | 64% | consistently half-tempo (every 2nd beat) |
| model 2, DBN | no | 664 | 2.61 | 74% | consistently half-tempo (every 2nd beat) |

### "Born This Way" — easier baseline

| config | outliers (>20 BPM off) | jitter after trimming (std) | within ±3 BPM (trimmed) | downbeat = real 4-beat bar? |
|---|---|---|---|---|
| model 1, PF | 100/441 (23%) | 4.29 | 60% | mixed |
| model 2, PF | 104/499 (21%) | 4.22 | 57% | mixed |
| model 2, DBN | 130/572 (23%) | 3.61 | 65% | mostly half-tempo again |
| model 3, DBN | 43/547 (8%) | 4.05 | 62% | yes — ~90% real 4-beat bars |

## Conclusions

- **DBN (non-causal) is consistently steadier than PF (causal)** on both tracks — but DBN needs
  the full track buffered first, so it isn't a mode a live device could ever run. It's a ceiling,
  not a candidate.
- **The causal mode — the only one actually comparable to what tapbox can run in real time — is
  rough on both tracks**, including the "easier" canonical pop track: 21-23% outright outlier
  beats and ~4 BPM residual jitter even after removing those outliers. No clear win over the
  current pipeline was demonstrated.
- **Model 3 (Rock Corpus) stood out on the pop track**: far fewer outlier misfires (8% vs. ~22%
  for every other config) and it actually recovered the real 4-beat downbeat ~90% of the time —
  the only config across both tracks to do so. On the hard club remix, though, even model 3
  collapsed into marking every other beat as the downbeat, same as everything else.
- **Downbeat/meter tracking is unreliable on the club remix across every config tried** — it's
  not just tapbox's own pipeline that finds this track ambiguous.

## Recommendation

BeatNet's causal mode does not currently demonstrate a clear improvement over tapbox's own
BTrack-based tracking on the tracks tested, so it's not a drop-in replacement candidate right
now. Its non-causal DBN decoder is a genuinely useful reference for "how stable could the beat
grid be with full lookahead" but isn't itself portable to a live-tapping device. If this is
revisited, the more promising thread is model 3's meter-tracking behavior on clean four-on-the-
floor material, and BeatNet's CRNN activations as a conceptual comparison point against the
firmware's own FFT/Mel/SuperFlux front end — not wholesale adoption.
