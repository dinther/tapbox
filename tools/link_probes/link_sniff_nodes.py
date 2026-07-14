"""Count Link peers by node id on Ethernet and WiFi for 12 s."""
import socket, select, time

IFACES = {'Ethernet': '169.254.12.114', 'WiFi': '192.168.1.96'}
socks = {}
for name, ip in IFACES.items():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(('', 20808))
    s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                 socket.inet_aton('224.76.78.75') + socket.inet_aton(ip))
    socks[s] = name

end = time.time() + 12
seen = {}
while time.time() < end:
    ready, _, _ = select.select(list(socks), [], [], 1.0)
    for s in ready:
        data, src = s.recvfrom(512)
        if not data.startswith(b'_asdp_v\x01'):
            continue
        key = (socks[s], src[0], data[12:20].hex())
        seen[key] = seen.get(key, 0) + 1

for (iface, ip, node), n in sorted(seen.items()):
    print(f"  {iface:8s}  {ip:16s} node {node}  {n:3d} pkts")
