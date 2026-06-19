#!/usr/bin/env python3
"""
Ableton Link monitor — decodes UDP multicast packets and prints BPM / peer info.
Requires no dependencies beyond the Python standard library.

Usage:  python link_monitor.py
"""
import socket
import struct
from datetime import datetime

MCAST_GRP  = '224.76.78.75'
MCAST_PORT = 20808

# Every Link packet starts with this 8-byte magic
LINK_HEADER = b'_asdp_v\x01'

# Payload entry key for Timeline (carries tempo), 'tmln' in big-endian
TMLN_KEY = 0x746d6c6e

MSG_LABELS = {1: 'ALIVE', 2: 'RESP ', 3: 'BYE  '}

# Packet layout (all big-endian):
#   [0:8]   protocol header  (_asdp_v\x01)
#   [8]     message type     (uint8:  1=alive 2=response 3=byebye)
#   [9]     ttl              (uint8)
#   [10:12] group id         (uint16)
#   [12:20] node id          (8 raw bytes — unique peer identity)
#   [20:]   payload entries  (key:uint32 + size:uint32 + data) repeated
#
# Timeline entry data (24 bytes):
#   [0:8]   microseconds per beat  (int64)  → BPM = 60_000_000 / value
#   [8:16]  beat origin            (int64 micro-beats)
#   [16:24] time origin            (int64 microseconds)

HEADER_SIZE = 20   # 8 proto + 1 type + 1 ttl + 2 group + 8 node


def decode(data: bytes, src: tuple) -> None:
    if len(data) < HEADER_SIZE or not data.startswith(LINK_HEADER):
        return

    msg_type = data[8]
    ttl      = data[9]
    peer_id  = data[12:20].hex()   # 8-byte node id as hex string

    ts = datetime.now().strftime('%H:%M:%S')

    if msg_type == 3:
        print(f"[{ts}] BYE    {src[0]:15s}  peer {peer_id}")
        return

    # Walk payload entries
    offset = HEADER_SIZE
    bpm = None
    while offset + 8 <= len(data):
        key  = struct.unpack_from('>I', data, offset)[0]
        size = struct.unpack_from('>I', data, offset + 4)[0]
        offset += 8

        if offset + size > len(data):
            break

        if key == TMLN_KEY and size >= 8:
            micros_per_beat = struct.unpack_from('>q', data, offset)[0]
            if micros_per_beat > 0:
                bpm = 60_000_000 / micros_per_beat

        offset += size

    if bpm is not None:
        label = MSG_LABELS.get(msg_type, f'?{msg_type}  ')
        print(f"[{ts}] {label}  {src[0]:15s}  peer {peer_id}  {bpm:.3f} BPM  ttl={ttl}")


def main() -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('', MCAST_PORT))

    # Join the Link multicast group on all available interfaces
    mreq = socket.inet_aton(MCAST_GRP) + socket.inet_aton('0.0.0.0')
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

    print(f"Listening on {MCAST_GRP}:{MCAST_PORT}  —  Ctrl+C to stop\n")

    try:
        while True:
            data, src = sock.recvfrom(512)
            decode(data, src)
    except KeyboardInterrupt:
        print('\nStopped.')
    finally:
        sock.close()


if __name__ == '__main__':
    main()
