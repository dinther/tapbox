"""Send a real Link measurement ping to LaserOS's advertised endpoint from a
same-/24 source and check for a pong."""
import socket, struct, time, os

ETH_IP = '169.254.12.114'
TAPBOX = '169.254.236.223'
MCAST  = ('224.76.78.75', 20808)

# 1. get LaserOS's current mep4 from its ALIVE on eth
cap = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
cap.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
cap.bind(('', 20808))
cap.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
               socket.inet_aton(MCAST[0]) + socket.inet_aton(ETH_IP))
cap.settimeout(1.0)
laseros_mep = None
tmpl = None
end = time.time() + 10
while time.time() < end and (laseros_mep is None or tmpl is None):
    try:
        data, src = cap.recvfrom(512)
    except socket.timeout:
        continue
    if not data.startswith(b'_asdp_v\x01') or data[8] != 1:
        continue
    if src[0] == ETH_IP and laseros_mep is None:
        off = 20
        while off + 8 <= len(data):
            key  = data[off:off+4]
            size = struct.unpack_from('>I', data, off+4)[0]
            off += 8
            if key == b'mep4' and size >= 6:
                ip = socket.inet_ntoa(data[off:off+4])
                port = struct.unpack_from('>H', data, off+4)[0]
                laseros_mep = (ip, port)
            off += size
    if src[0] == ETH_IP and tmpl is None:
        tmpl = bytearray(data)
cap.close()
if laseros_mep is None:
    print("could not learn LaserOS mep4")
    raise SystemExit(1)
print(f"LaserOS measurement endpoint: {laseros_mep[0]}:{laseros_mep[1]}")

# 2. capture a genuine tapbox ping by re-advertising a fake peer
mep = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
mep.bind((ETH_IP, 0))
mep.settimeout(0.5)
fake_id = os.urandom(8)
tmpl[12:20] = fake_id
off = 20
while off + 8 <= len(tmpl):
    key  = bytes(tmpl[off:off+4])
    size = struct.unpack_from('>I', tmpl, off+4)[0]
    off += 8
    if key == b'sess' and size == 8:
        tmpl[off:off+8] = fake_id
    elif key == b'mep4' and size >= 6:
        tmpl[off:off+4] = socket.inet_aton(ETH_IP)
        struct.pack_into('>H', tmpl, off+4, mep.getsockname()[1])
    off += size

tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
tx.bind((ETH_IP, 0))
tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(ETH_IP))
tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)

ping = None
end = time.time() + 8
last = 0.0
while time.time() < end and ping is None:
    if time.time() - last > 1.0:
        tx.sendto(bytes(tmpl), MCAST)
        last = time.time()
    try:
        data, src = mep.recvfrom(512)
        if src[0] == TAPBOX and data.startswith(b'_link_v'):
            ping = data
    except socket.timeout:
        pass
if ping is None:
    print("no tapbox ping captured")
    raise SystemExit(1)
print(f"captured genuine tapbox ping ({len(ping)} bytes)")

# 3. send that ping to LaserOS's mep4 from our same-/24 socket
probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
probe.bind((ETH_IP, 0))
probe.settimeout(1.0)
got = 0
for i in range(3):
    probe.sendto(ping, laseros_mep)
    end = time.time() + 2
    while time.time() < end:
        try:
            data, src = probe.recvfrom(512)
            got += 1
            print(f"PONG from {src[0]}:{src[1]} ({len(data)} bytes, header {data[:8]!r})")
        except socket.timeout:
            break
if got == 0:
    print("no pong — LaserOS measurement responder is deaf even to a same-/24 source")
