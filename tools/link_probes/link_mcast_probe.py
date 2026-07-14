"""Probe tapbox's Link RX paths: unicast vs multicast ALIVE, watch for RESPONSE."""
import socket, time

ETH_IP = '169.254.12.114'
TAPBOX = ('169.254.236.223', 20808)
MCAST  = ('224.76.78.75', 20808)

# capture a real ALIVE from MadMapper to use as template
cap = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
cap.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
cap.bind(('', 20808))
cap.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
               socket.inet_aton(MCAST[0]) + socket.inet_aton(ETH_IP))
cap.settimeout(1.0)
alive = None
end = time.time() + 10
while time.time() < end and alive is None:
    try:
        data, src = cap.recvfrom(512)
    except socket.timeout:
        continue
    if src[0] == ETH_IP and data.startswith(b'_asdp_v\x01') and data[8] == 1:
        alive = bytearray(data)
cap.close()
if alive is None:
    print("no ALIVE captured — MadMapper running?")
    raise SystemExit(1)

def probe(name, dest):
    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    tx.bind((ETH_IP, 0))
    tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(ETH_IP))
    tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
    tx.settimeout(1.0)
    tx.sendto(bytes(alive), dest)
    end = time.time() + 4
    while time.time() < end:
        try:
            data, src = tx.recvfrom(512)
        except socket.timeout:
            continue
        if src[0] == TAPBOX[0]:
            kind = {1: 'ALIVE', 2: 'RESPONSE', 3: 'BYEBYE'}.get(data[8], data[8])
            print(f"{name}: reply from tapbox — {kind}")
            tx.close()
            return
    print(f"{name}: NO reply from tapbox")
    tx.close()

probe("unicast  ALIVE -> tapbox:20808", TAPBOX)
probe("multicast ALIVE -> 224.76.78.75", MCAST)
