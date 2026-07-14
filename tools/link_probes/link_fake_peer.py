"""Impersonate a Link peer with a controllable measurement endpoint and watch
whether tapbox initiates the session-merge measurement (pings us)."""
import socket, struct, time, os

ETH_IP = '169.254.12.114'
TAPBOX = '169.254.236.223'
MCAST  = ('224.76.78.75', 20808)

# capture a LaserOS ALIVE as a structural template (or tapbox's if needed)
cap = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
cap.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
cap.bind(('', 20808))
cap.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
               socket.inet_aton(MCAST[0]) + socket.inet_aton(ETH_IP))
cap.settimeout(1.0)
tmpl = None
end = time.time() + 10
while time.time() < end and tmpl is None:
    try:
        data, src = cap.recvfrom(512)
    except socket.timeout:
        continue
    if src[0] == ETH_IP and data.startswith(b'_asdp_v\x01') and data[8] == 1:
        tmpl = bytearray(data)
cap.close()
if tmpl is None:
    print("no local ALIVE template captured — is LaserOS running?")
    raise SystemExit(1)

# measurement endpoint socket — tapbox should ping this
mep = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
mep.bind((ETH_IP, 0))
mep_port = mep.getsockname()[1]
mep.settimeout(0.5)

fake_id = os.urandom(8)
tmpl[12:20] = fake_id                      # header node id

# rewrite payload entries: sess -> fake_id, mep4 -> our socket
off = 20
while off + 8 <= len(tmpl):
    key  = bytes(tmpl[off:off+4])
    size = struct.unpack_from('>I', tmpl, off+4)[0]
    off += 8
    if key == b'sess' and size == 8:
        tmpl[off:off+8] = fake_id
    elif key == b'mep4' and size >= 6:
        tmpl[off:off+4] = socket.inet_aton(ETH_IP)
        struct.pack_into('>H', tmpl, off+4, mep_port)
    off += size

print(f"fake peer node {fake_id.hex()}, measurement endpoint {ETH_IP}:{mep_port}")

tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
tx.bind((ETH_IP, 0))
tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(ETH_IP))
tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)
tx.settimeout(0.5)

pings = 0
responses = 0
end = time.time() + 20
last_send = 0.0
while time.time() < end:
    if time.time() - last_send > 1.0:
        tx.sendto(bytes(tmpl), MCAST)
        last_send = time.time()
    try:
        data, src = mep.recvfrom(512)
        pings += 1
        if pings == 1:
            print(f"PING #1 from {src[0]}:{src[1]}  ({len(data)} bytes, header {data[:8]!r})")
    except socket.timeout:
        pass
    try:
        data, src = tx.recvfrom(512)
        if src[0] == TAPBOX:
            responses += 1
    except socket.timeout:
        pass

print(f"total: {pings} packets to measurement endpoint, {responses} discovery replies from tapbox")
