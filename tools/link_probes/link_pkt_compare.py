"""Capture one ALIVE from tapbox and one from LaserOS; dump header + payload keys."""
import socket, struct, time

ETH_IP  = '169.254.12.114'
TAPBOX  = '169.254.236.223'

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('', 20808))
s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
             socket.inet_aton('224.76.78.75') + socket.inet_aton(ETH_IP))
s.settimeout(1.0)

want = {TAPBOX: 'tapbox', ETH_IP: 'LaserOS'}
got = {}
end = time.time() + 15
while time.time() < end and len(got) < 2:
    try:
        data, src = s.recvfrom(512)
    except socket.timeout:
        continue
    name = want.get(src[0])
    if name and name not in got and data.startswith(b'_asdp_v\x01'):
        got[name] = (data, src)

for name, (data, src) in got.items():
    mtype   = data[8]
    ttl     = data[9]
    groupid = struct.unpack_from('>H', data, 10)[0]
    node    = data[12:20].hex()
    print(f"{name} ({src[0]}:{src[1]}), {len(data)} bytes")
    print(f"  proto={data[:8]!r} type={mtype} ttl={ttl} groupId={groupid} node={node}")
    off = 20
    while off + 8 <= len(data):
        key  = data[off:off+4]
        size = struct.unpack_from('>I', data, off+4)[0]
        off += 8
        val = data[off:off+size]
        extra = ''
        if key == b'mep4' and size >= 6:
            ip = '.'.join(str(b) for b in val[:4])
            port = struct.unpack_from('>H', val, 4)[0]
            extra = f' -> {ip}:{port}'
        if key == b'sess':
            extra = f' -> {val.hex()}'
        if key == b'tmln' and size >= 8:
            mpb = struct.unpack_from('>q', val, 0)[0]
            if mpb > 0:
                extra = f' -> {60_000_000/mpb:.2f} BPM'
        print(f"  payload {key!r} size={size}{extra}")
        off += size
    print()

missing = set(want.values()) - set(got)
if missing:
    print(f"not captured within 15 s: {', '.join(missing)}")
