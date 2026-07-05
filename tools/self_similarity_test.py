#!/usr/bin/env python3
"""Test: does a 4-beat span of onset-strength recur, shape-wise, in the next
4-beat span?

Runs the exact same FFT -> Mel -> SuperFlux onset-strength pipeline as the
firmware (ported from src/dsp/{fft_processor,mel_filterbank,superflux_onset}.h
and the hop plumbing in mic_task(), src/main.cpp) against either a live
microphone or a WAV file, then compares consecutive N-beat chunks of that
signal with a normalized (Pearson) correlation — a shape match, not a value
match. This does NOT reimplement BTrack's tempo induction; give it the
track's BPM directly with --bpm.

Setup (one-time):
    tools\\selfsim_env\\Scripts\\python.exe -m pip install -r requirements-selfsim.txt

Usage:
    tools\\selfsim_env\\Scripts\\python.exe tools\\self_similarity_test.py --bpm 120
    tools\\selfsim_env\\Scripts\\python.exe tools\\self_similarity_test.py --list-devices
    tools\\selfsim_env\\Scripts\\python.exe tools\\self_similarity_test.py --bpm 117 --file track.wav --out plot.png
"""
import argparse
import sys
import wave
from collections import deque

import numpy as np
import sounddevice as sd
import matplotlib.pyplot as plt
import matplotlib.animation as animation

# ---- Firmware constants — must match src/main.cpp / src/dsp exactly ----
SAMPLE_RATE = 32000
FFT_SIZE = 1024
HOP_SIZE = 512
N_MEL = 32
FREQ_MIN, FREQ_MAX = 80.0, 16000.0
HOP_HZ = SAMPLE_RATE / HOP_SIZE  # 62.5

# SuperFlux constants (src/dsp/superflux_onset.h)
MAX_FILTER_MU = 3
PEAK_PRE_MAX = 3
PEAK_POST_MAX = 3
PEAK_PRE_AVG = 10
PEAK_POST_AVG = 3
DELTA = 0.1
MIN_INTER_ONSET_FRAMES = 3
HISTORY_LEN = PEAK_PRE_AVG + PEAK_POST_AVG + 2


def hz_to_mel(hz):
    return 2595.0 * np.log10(1.0 + hz / 700.0)


def mel_to_hz(mel):
    return 700.0 * (10.0 ** (mel / 2595.0) - 1.0)


def build_mel_filterbank(sample_rate, n_fft, n_mel, freq_min, freq_max):
    """Port of MelFilterbank::compute_filterbank_ (dense matrix — N_MEL=32 is
    small enough that the sparse-storage optimization the firmware needs
    (embedded RAM) doesn't matter here)."""
    n_bins = n_fft // 2
    mel_min, mel_max = hz_to_mel(freq_min), hz_to_mel(freq_max)
    bin_hz = sample_rate / n_fft
    mel_points = mel_min + (mel_max - mel_min) * np.arange(n_mel + 2) / (n_mel + 1)
    bin_points = np.clip((mel_to_hz(mel_points) / bin_hz).astype(int), 0, n_bins - 1)

    fb = np.zeros((n_bins, n_mel), dtype=np.float64)
    for m in range(n_mel):
        left, center, right = bin_points[m], bin_points[m + 1], bin_points[m + 2]
        for b in range(left, right):
            if b < center:
                w = (b - left) / (center - left) if center > left else 0.0
            else:
                w = (right - b) / (right - center) if right > center else 0.0
            fb[b, m] = w
        s = fb[left:right, m].sum()
        if s > 0:
            fb[left:right, m] /= s
    return fb


class SuperFlux:
    """Port of SuperFluxOnset<N_MEL> — same ring-buffer/peak-picking state
    machine, including the ~3-hop reporting delay (candidate is picked
    kPeakPostMaxFrames back from the write head, same as the firmware)."""

    def __init__(self, n_mel):
        self.n_mel = n_mel
        self.log_prev = None
        self.history = np.zeros(HISTORY_LEN)
        self.write_idx = 0
        self.frames_since_onset = 0
        self.frames_seen = 0

    def process(self, mel_bands_raw):
        eps = 1e-6
        log_curr = np.log(mel_bands_raw + eps)

        if self.log_prev is None:
            self.log_prev = log_curr.copy()
            return 0.0, False

        flux = 0.0
        for m in range(self.n_mel):
            lo = max(0, m - MAX_FILTER_MU)
            hi = min(self.n_mel - 1, m + MAX_FILTER_MU) + 1
            diff = log_curr[m] - self.log_prev[lo:hi].max()
            if diff > 0.0:
                flux += diff
        self.log_prev = log_curr.copy()

        self.history[self.write_idx] = flux
        self.write_idx = (self.write_idx + 1) % HISTORY_LEN
        self.frames_since_onset += 1

        candidate = (self.write_idx + HISTORY_LEN - PEAK_POST_MAX - 1) % HISTORY_LEN
        candidate_val = self.history[candidate]

        is_local_max = True
        for off in range(-PEAK_PRE_MAX, PEAK_POST_MAX + 1):
            if off == 0:
                continue
            idx = (candidate + HISTORY_LEN + off) % HISTORY_LEN
            if self.history[idx] >= candidate_val:
                is_local_max = False
                break

        is_above_mean = False
        if is_local_max:
            vals = [self.history[(candidate + HISTORY_LEN + off) % HISTORY_LEN]
                    for off in range(-PEAK_PRE_AVG, PEAK_POST_AVG + 1)]
            is_above_mean = candidate_val > (sum(vals) / len(vals) + DELTA)

        if self.frames_seen < HISTORY_LEN:
            self.frames_seen += 1
        warm = self.frames_seen >= HISTORY_LEN

        event = warm and is_local_max and is_above_mean and (self.frames_since_onset >= MIN_INTER_ONSET_FRAMES)
        if event:
            self.frames_since_onset = 0
        return flux, event


def make_hamming(n):
    i = np.arange(n)
    return 0.54 - 0.46 * np.cos(2 * np.pi * i / (n - 1))


class OnsetPipeline:
    """FFT -> mag^2 -> Mel -> SuperFlux, one hop (HOP_SIZE new samples) at a
    time — shared by both live-mic and file-input modes so they run the
    identical path."""

    def __init__(self):
        self.mel_fb = build_mel_filterbank(SAMPLE_RATE, FFT_SIZE, N_MEL, FREQ_MIN, FREQ_MAX)
        self.window = make_hamming(FFT_SIZE)
        self.sf = SuperFlux(N_MEL)
        self.audio_buf = np.zeros(FFT_SIZE, dtype=np.float64)

    def process_hop(self, block):
        self.audio_buf = np.concatenate([self.audio_buf[HOP_SIZE:], block])
        spec = np.fft.rfft(self.audio_buf * self.window, n=FFT_SIZE)[:FFT_SIZE // 2]
        mag_sq = (np.abs(spec) / FFT_SIZE) ** 2
        mel_frame = mag_sq @ self.mel_fb
        flux, event = self.sf.process(mel_frame)
        return flux, event


class ChunkSimilarity:
    """Buffers onset-strength values into N-beat chunks and Pearson-correlates
    each completed chunk against the one before it (shape match, not value
    match — correlation is scale/offset invariant)."""

    def __init__(self, frames_per_chunk):
        self.frames_per_chunk = frames_per_chunk
        self.buf = []
        self.prev_chunk = None

    def add(self, flux):
        """Returns a similarity score (float) if a chunk just completed, else None."""
        self.buf.append(flux)
        if len(self.buf) < self.frames_per_chunk:
            return None
        chunk = np.array(self.buf)
        self.buf = []
        score = None
        if self.prev_chunk is not None and chunk.std() > 1e-9 and self.prev_chunk.std() > 1e-9:
            score = float(np.corrcoef(self.prev_chunk, chunk)[0, 1])
        self.prev_chunk = chunk
        return score


def read_wav_mono_f32(path):
    """Minimal WAV reader (stdlib `wave`, no extra deps) — expects PCM16 mono
    at SAMPLE_RATE (use ffmpeg to convert: -ar 32000 -ac 1 -f wav)."""
    with wave.open(path, "rb") as wf:
        sr = wf.getframerate()
        n_ch = wf.getnchannels()
        sampwidth = wf.getsampwidth()
        raw = wf.readframes(wf.getnframes())
    if sampwidth != 2:
        raise ValueError(f"{path}: expected 16-bit PCM, got {sampwidth*8}-bit")
    data = np.frombuffer(raw, dtype=np.int16).astype(np.float64) / 32768.0
    if n_ch > 1:
        data = data.reshape(-1, n_ch).mean(axis=1)
    if sr != SAMPLE_RATE:
        raise ValueError(f"{path}: sample rate is {sr}, expected {SAMPLE_RATE} "
                          f"(ffmpeg -i in.mp3 -ar {SAMPLE_RATE} -ac 1 -f wav out.wav)")
    return data


def make_figure(beats, bpm):
    plt.style.use("dark_background")
    fig, (ax_flux, ax_sim) = plt.subplots(2, 1, figsize=(11, 6), sharex=True,
                                           gridspec_kw={"height_ratios": [2, 1]})
    fig.suptitle(f"tapbox onset-strength self-similarity — {beats}-beat chunks @ {bpm:.1f} BPM")

    (flux_line,) = ax_flux.plot([], [], color="#4da3ff", linewidth=1.0, label="onset strength (flux)")
    ax_flux.set_ylabel("flux")
    ax_flux.grid(True, color="#333333", linewidth=0.5)
    ax_flux.legend(loc="upper right", fontsize=8, framealpha=0.3)

    (sim_line,) = ax_sim.plot([], [], color="#5fd97a", marker="o", markersize=3, linewidth=1.0,
                              label="shape similarity (Pearson r, prev vs. this chunk)")
    ax_sim.axhline(0.0, color="#666666", linewidth=0.5)
    ax_sim.set_ylim(-1.05, 1.05)
    ax_sim.set_xlabel("time (s)")
    ax_sim.set_ylabel("similarity")
    ax_sim.grid(True, color="#333333", linewidth=0.5)
    ax_sim.legend(loc="upper right", fontsize=8, framealpha=0.3)
    return fig, ax_flux, ax_sim, flux_line, sim_line


def run_file(path, frames_per_chunk, beats, bpm, out_path):
    samples = read_wav_mono_f32(path)
    dur = len(samples) / SAMPLE_RATE
    print(f"Loaded {path}: {dur:.1f}s at {SAMPLE_RATE} Hz mono")

    pipeline = OnsetPipeline()
    chunker = ChunkSimilarity(frames_per_chunk)

    flux_hist, time_hist, chunk_boundaries, sim_scores = [], [], [], []
    hop_i = 0
    for i in range(0, len(samples) - HOP_SIZE, HOP_SIZE):
        block = samples[i:i + HOP_SIZE]
        flux, _event = pipeline.process_hop(block)
        t = hop_i / HOP_HZ
        flux_hist.append(flux)
        time_hist.append(t)
        score = chunker.add(flux)
        if score is not None or len(chunker.buf) == 0:
            chunk_boundaries.append(t)
        if score is not None:
            sim_scores.append((t, score))
        hop_i += 1

    print(f"Processed {hop_i} hops, {len(sim_scores)} chunk comparisons")
    if sim_scores:
        scores = np.array([s for _, s in sim_scores])
        print(f"similarity: mean={scores.mean():+.3f} std={scores.std():.3f} "
              f"min={scores.min():+.3f} max={scores.max():+.3f}")

    fig, ax_flux, ax_sim, flux_line, sim_line = make_figure(beats, bpm)
    t_arr = np.array(time_hist)
    flux_line.set_data(t_arr, np.array(flux_hist))
    ax_flux.set_xlim(0, dur)
    ax_flux.set_ylim(0, max(0.5, np.max(flux_hist) * 1.1))
    for cb in chunk_boundaries:
        ax_flux.axvline(cb, color="#888888", linewidth=0.4, linestyle="--", alpha=0.6)
    if sim_scores:
        xs, ys = zip(*sim_scores)
        sim_line.set_data(xs, ys)
    plt.tight_layout()

    if out_path:
        fig.savefig(out_path, dpi=130)
        print(f"Saved plot to {out_path}")
    else:
        plt.show()


def run_live(frames_per_chunk, beats, bpm, device):
    pipeline = OnsetPipeline()
    chunker = ChunkSimilarity(frames_per_chunk)

    PLOT_SECONDS = 12
    max_hops = int(PLOT_SECONDS * HOP_HZ)
    flux_hist = deque(maxlen=max_hops)
    time_hist = deque(maxlen=max_hops)
    chunk_boundaries = deque(maxlen=64)
    sim_scores = deque(maxlen=64)
    hop_counter = {"i": 0}

    def audio_callback(indata, frames, time_info, status):
        if status:
            print(status, file=sys.stderr)
        flux, _event = pipeline.process_hop(indata[:, 0].astype(np.float64))
        t = hop_counter["i"] / HOP_HZ
        hop_counter["i"] += 1
        flux_hist.append(flux)
        time_hist.append(t)
        score = chunker.add(flux)
        if score is not None:
            chunk_boundaries.append(t)
            sim_scores.append((t, score))
            print(f"[{t:7.2f}s] chunk similarity (shape, not value): {score:+.3f}")

    fig, ax_flux, ax_sim, flux_line, sim_line = make_figure(beats, bpm)
    chunk_lines = []

    def update(_frame):
        if not time_hist:
            return flux_line, sim_line
        t_arr = np.array(time_hist)
        flux_line.set_data(t_arr, np.array(flux_hist))
        t0, t1 = t_arr[0], t_arr[-1]
        ax_flux.set_xlim(t0, max(t1, t0 + 1))
        ax_flux.set_ylim(0, max(0.5, np.max(flux_hist) * 1.1))

        for ln in chunk_lines:
            ln.remove()
        chunk_lines.clear()
        for cb in chunk_boundaries:
            if cb >= t0:
                chunk_lines.append(ax_flux.axvline(cb, color="#888888", linewidth=0.6, linestyle="--"))

        if sim_scores:
            xs, ys = zip(*[(x, y) for x, y in sim_scores if x >= t0])
            if xs:
                sim_line.set_data(xs, ys)
        return flux_line, sim_line

    ani = animation.FuncAnimation(fig, update, interval=100, cache_frame_data=False)
    with sd.InputStream(samplerate=SAMPLE_RATE, channels=1, blocksize=HOP_SIZE,
                         dtype="float32", device=device, callback=audio_callback):
        plt.tight_layout()
        plt.show()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bpm", type=float, help="Track tempo — sets the N-beat chunk length")
    ap.add_argument("--beats", type=int, default=4, help="Beats per chunk (default 4)")
    ap.add_argument("--device", type=int, default=None, help="sounddevice input device index (live mode)")
    ap.add_argument("--list-devices", action="store_true", help="List audio devices and exit")
    ap.add_argument("--file", type=str, default=None,
                    help="WAV file (16-bit PCM, mono, 32000 Hz) instead of live mic input")
    ap.add_argument("--out", type=str, default=None, help="Save the plot to this PNG instead of showing it")
    args = ap.parse_args()

    if args.list_devices:
        print(sd.query_devices())
        return
    if not args.bpm:
        ap.error("--bpm is required (or use --list-devices)")

    frames_per_chunk = int(round(HOP_HZ * 60.0 / args.bpm * args.beats))
    print(f"Hop rate: {HOP_HZ:.1f} Hz | chunk = {args.beats} beats = {frames_per_chunk} hops "
          f"(~{frames_per_chunk / HOP_HZ:.2f}s) at {args.bpm:.1f} BPM")

    if args.file:
        run_file(args.file, frames_per_chunk, args.beats, args.bpm, args.out)
    else:
        run_live(frames_per_chunk, args.beats, args.bpm, args.device)


if __name__ == "__main__":
    main()
