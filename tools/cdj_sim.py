"""
cdj_sim.py  —  Pioneer Pro DJ Link beat packet simulator

Broadcasts genuine 0x28 beat packets on UDP port 50001, exactly as a CDJ-2000
would. Run this on a PC on the same network/switch as the tapbox.

Usage:
    python cdj_sim.py [bpm] [player]

    bpm     Starting BPM, e.g. 128.5  (default: 120.0)
    player  CDJ player number 1-4     (default: 1)

Keys (while running):
    UP / +      BPM + 1
    DOWN / -    BPM - 1
    RIGHT / ]   BPM + 0.1
    LEFT / [    BPM - 0.1
    Q           Quit
"""

import socket
import struct
import sys
import time
import msvcrt

DEST_PORT     = 50001
BROADCAST     = '255.255.255.255'
SEND_INTERVAL = 0.030           # 30 ms, matching real CDJ cadence
NEUTRAL_PITCH = 0x00100000      # 1 048 576 — no pitch adjustment
MAGIC         = bytes([0x51, 0x73, 0x70, 0x74, 0x31, 0x57, 0x6d, 0x4a, 0x4f, 0x4c])
BPM_MIN       = 20.0
BPM_MAX       = 400.0


def build_beat_packet(bpm: float, beat_in_bar: int, player: int) -> bytes:
    raw_bpm  = max(0, min(0xFFFF, int(round(bpm * 100))))
    beat_ms  = int(60000.0 / bpm)
    bar_ms   = beat_ms * 4

    pkt = bytearray(96)

    pkt[0x00:0x0a] = MAGIC
    pkt[0x0a]      = 0x28                               # beat packet type

    name = b'CDJ-sim\x00'                               # device name (21 bytes)
    pkt[0x0b:0x0b + len(name)] = name

    pkt[0x20] = 0x00                                    # subtype
    pkt[0x21] = player                                  # device / player number

    struct.pack_into('>H', pkt, 0x22, 0x003C)           # length: 60 remaining bytes

    struct.pack_into('>I', pkt, 0x24, beat_ms)          # nextBeat
    struct.pack_into('>I', pkt, 0x28, beat_ms * 2)      # 2ndBeat
    struct.pack_into('>I', pkt, 0x2c, bar_ms)           # nextBar
    struct.pack_into('>I', pkt, 0x30, beat_ms * 4)      # 4thBeat
    struct.pack_into('>I', pkt, 0x34, bar_ms * 2)       # 2ndBar
    struct.pack_into('>I', pkt, 0x38, beat_ms * 8)      # 8thBeat

    for i in range(0x3c, 0x54):                         # 0xFF padding
        pkt[i] = 0xFF

    struct.pack_into('>I', pkt, 0x54, NEUTRAL_PITCH)    # pitch
    struct.pack_into('>H', pkt, 0x5a, raw_bpm)          # BPM × 100
    pkt[0x5c] = beat_in_bar                             # beat in bar (1-4)
    pkt[0x5f] = player                                  # device number (repeated)

    return bytes(pkt)


def read_key():
    """Non-blocking key check. Returns char or None. Handles arrow keys."""
    if not msvcrt.kbhit():
        return None
    ch = msvcrt.getch()
    if ch == b'\xe0':                   # extended key prefix (arrows)
        ch2 = msvcrt.getch()
        if ch2 == b'H': return 'UP'
        if ch2 == b'P': return 'DOWN'
        if ch2 == b'K': return 'LEFT'
        if ch2 == b'M': return 'RIGHT'
        return None
    return ch.decode('ascii', errors='ignore').lower()


BEATS = ['1', '2', '3', '4']


def run(bpm: float, player: int):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.bind(('', 0))

    beat_idx  = 0
    next_send = time.monotonic()

    print()
    print(f"  CDJ simulator  —  player {player}")
    print()
    print("  UP/+    BPM + 1        DOWN/-   BPM - 1")
    print("  RIGHT/] BPM + 0.1      LEFT/[   BPM - 0.1")
    print("  Q       Quit")
    print()

    try:
        while True:
            key = read_key()
            if key in ('q',):
                break
            elif key in ('up', '+'):
                bpm = min(BPM_MAX, round(bpm + 1.0, 2))
            elif key in ('down', '-'):
                bpm = max(BPM_MIN, round(bpm - 1.0, 2))
            elif key in ('right', ']'):
                bpm = min(BPM_MAX, round(bpm + 0.1, 2))
            elif key in ('left', '['):
                bpm = max(BPM_MIN, round(bpm - 0.1, 2))

            now = time.monotonic()
            if now >= next_send:
                beat_in_bar = beat_idx + 1
                pkt = build_beat_packet(bpm, beat_in_bar, player)
                sock.sendto(pkt, (BROADCAST, DEST_PORT))

                bar = ''.join(
                    f'[{BEATS[i]}]' if i == beat_idx else f' {BEATS[i]} '
                    for i in range(4)
                )
                print(f"\r  {bpm:6.2f} BPM  player {player}  {bar}  ", end='', flush=True)

                beat_idx  = (beat_idx + 1) % 4
                next_send += SEND_INTERVAL
            else:
                time.sleep(max(0.0, next_send - now - 0.001))

    except KeyboardInterrupt:
        pass
    finally:
        sock.close()
        print('\n\n  Stopped.\n')


if __name__ == '__main__':
    bpm    = float(sys.argv[1]) if len(sys.argv) > 1 else 120.0
    player = int(sys.argv[2])   if len(sys.argv) > 2 else 1

    bpm = max(BPM_MIN, min(BPM_MAX, bpm))
    player = max(1, min(4, player))

    run(bpm, player)
