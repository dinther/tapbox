# Ableton Link probe scripts

Packet-level diagnostics for Link discovery/session problems, written while
debugging why Link peers fail to sync over a direct Ethernet cable with
link-local (169.254.x.x) addressing. Python 3 standard library only.

All scripts have the local interface addresses and the peer's address
**hardcoded near the top** (`ETH_IP`, `WIFI_IP`, `TAPBOX`) — adjust them to
your setup before running (`ipconfig` shows yours; the tapbox display scrolls
its own).

| Script | What it shows |
|--------|---------------|
| `link_sniff_nodes.py` | Which Link peers (by node id) are transmitting on which interface. Confirms TX and interface binding without trusting any app's UI. |
| `link_pkt_compare.py` | Full decode of one ALIVE per peer: protocol version, group id, node id, tempo, session id, measurement endpoint. Spots structural incompatibilities. |
| `link_mcast_probe.py` | Replays a captured ALIVE to a device via unicast and via multicast and watches for a RESPONSE — separates message-processing failures from multicast-delivery failures. |
| `link_fake_peer.py` | Impersonates a complete Link peer (fresh node/session id, measurement endpoint pointed at its own socket) and reports whether the device under test answers discovery and initiates measurement pings. The full merge handshake, observable end to end. |
| `laseros_pong_test.py` | The decisive subnet-filter test: captures a genuine measurement ping, replays the identical bytes from a *same-/24* source, and watches for a pong. A responder that pongs the replay but ignored the original is filtering by source subnet. |

## Background

Ableton Link builds from Feb 2024 (upstream commit `a273b36`) to Mar 2026
(removed in `d466f42`) contain a hardcoded /24 source-subnet filter in
discovery. Link-local addressing is a /16, so two directly-cabled devices
usually land in different /24s and a filter-era peer goes silently deaf to
the other end. tapbox firmware ≥ v1.14.2 carries the upstream removal; these
scripts identify the same defect in other Link applications.
