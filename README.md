# tapbox

An ESP32-based tap-tempo controller that joins an [Ableton Link](https://www.ableton.com/en/link/) session over Ethernet or WiFi. Built on the [WT32-ETH01](http://www.wireless-tag.com/portfolio/wt32-eth01/) module with a MAX7219 8-digit 7-segment display.

<img width="50%" alt="image" src="https://github.com/user-attachments/assets/7fcc0947-bca3-4eb2-b496-423312c8f760" />




## Features

- **Three sync modes** — **Manual** (tap tempo), **Audio** (microphone auto-detects BPM, you tap the downbeat), and **CDJ** (Pioneer Pro DJ Link). Chosen with the `node` menu item; the active mode is shown by a bar on the display (top = CDJ, middle = Manual, bottom = Audio)
- **Tap tempo** — tap 4 times to lock in BPM and phase-align to the Link session (Manual mode)
- **Audio beat detection** — an INMP441 I2S microphone listens to the room and tracks the tempo automatically; you tap the downbeat for phase. → [How beat detection works](BEAT_DETECTION.md)
- **Pioneer CDJ sync** — passively listens for Pro DJ Link beat packets on the same network; bridges CDJ tempo directly into the Ableton Link session
- **Ableton Link** — joins the Link network automatically on boot; peers shown on display
- **Two-button control** — tap button for tempo and menu navigation; select button for confirm/back
- **Ethernet or WiFi** — Connects to your network as a client; browser config page for credentials; auto-failover to Wifi if no Ethernet present.
- **Static or DHCP** — configure IP address, subnet, and gateway via menu
- **IP ticker on boot** — non-blocking scroll of connection type and IP address at startup (`Eth`, `SSID`, or `AP`); device is fully operational during the scroll
- **OSC control** — UDP server on port 8000 for remote tap, BPM set, nudge, and downbeat reset
- **Menu system** — on-device configuration with NVS persistence across power cycles
- **OTA updates** — open the menu, hold both buttons 3 s, release and confirm with select; device reboots and flashes latest firmware automatically on next network connection
- **Factory reset** — open the menu, hold both buttons 8 s, confirm with select; returns device to store-bought state

## Hardware

| Component | Part |
|-----------|------|
| MCU / Ethernet | WT32-ETH01 (ESP32 + LAN8720A) |
| Display | MAX7219 8-digit 7-segment module |
| Input | Two momentary push buttons (tap + select) |
| Microphone | INMP441 I2S MEMS microphone (for Audio mode) |

### Pin Assignments

| GPIO | Function |
|------|----------|
| IO35 | Tap button — input-only, needs **external 10 kΩ pull-up to 3.3 V** |
| IO39 | Select button — input-only, needs **external 10 kΩ pull-up to 3.3 V** |
| IO4  | INMP441 SCK (I2S bit clock) |
| IO12 | INMP441 WS (I2S word select) |
| IO36 | INMP441 SD (I2S data in) |
| IO14 | MAX7219 CLK |
| IO2  | MAX7219 DIN |
| IO15 | MAX7219 LOAD/CS |

INMP441 wiring: **VDD → 3.3 V**, **GND → GND**, **L/R → GND** (selects the left channel), plus SCK/WS/SD as above.

> GPIO 0, 16, 18, 19, 21, 22, 23, 25, 26, 27 are used by the onboard Ethernet — do not reassign.  
> GPIO 34–39 are input-only with **no internal pull-up** — the tap/select buttons on IO35/IO39 therefore require external pull-ups.  
> IO12 is a flash-voltage strapping pin (used here for I2S WS); acceptable on the WT32-ETH01.  
> Battery monitoring was removed — IO36 is now the microphone data line.

## Display Layout

![Display layout](docs/display.jpg)

- **beat** — current BPM, four digits with decimal point (`120.0`)
- **mode bar** — single horizontal segment: **top = CDJ**, **middle = Tap only**, **bottom = Audio**
- **count** — beat position in the bar (1–4, or up to your time signature)
- **lock dot** — decimal point on the count digit: **solid** = locked (CDJ active / mic stable / tap set); **blinking** = Audio mode searching; **off** = no lock
- **peers** — number of other Ableton Link peers on the network

**Menu mode:**
```
[ label 0 ][ label 1 ][ label 2 ][ label 3 ][ blank ][ value right-justified ]
```
Value flashes at 4 Hz in edit mode.

## Controls

| Action | Result |
|--------|--------|
| Tap button (normal mode) | Tap tempo |
| Tap button (menu nav) | Advance to next item |
| Tap button held (menu edit) | Auto-increment value (5/sec after 500 ms, 20/sec after 1500 ms) |
| Select button short press | Enter menu / confirm |
| Select button long press (1 s) | Back / exit menu |

## Menu

| Label | Setting | Values |
|-------|---------|--------|
| `Beat` | Time signature | 2, 3, 4, 5, 6, 7 |
| `nud ` | OSC nudge size | 50 ms · 20 ms · 5 ms |
| `Led ` | Display brightness | 1 – 4 (live preview) |
| `node` | Sync mode | `Cdj` · `Aud` (audio) · `tAP` (manual) |
| `uind` | Mic accept window | ± BPM around tapped tempo, 1–10 (Audio mode only) |
| `SLEu` | Mic tempo slew | rate limit, 0.1 %/sec units (Audio mode only) |
| `thr ` | Mic onset threshold | sensitivity to kicks (Audio mode only) |
| `gAte` | Mic noise gate | absolute signal floor (Audio mode only) |
| `Lan.` | Network mode | `Auto` (DHCP) · `Stat` (static) |
| `IP  ` | Static IP address | sub-menu: Oct1–Oct4, 0–255 each |
| `Sub.` | Subnet mask | sub-menu: Oct1–Oct4, 0–255 each |
| `Hub.` | Gateway | sub-menu: Oct1–Oct4, 0–255 each |
| `vEr ` | Firmware version | read-only; shows major.minor.patch |
| `done` | Exit menu | returns to normal mode |

`node` selects the sync mode. The four mic-tuning items (`uind`, `SLEu`, `thr`, `gAte`) are **only shown when mode is `Aud`** — they tune the audio beat detector (see [BEAT_DETECTION.md](BEAT_DETECTION.md)).  
`IP`, `Sub.`, and `Hub.` are only shown when network mode is `Stat`. Each opens a sub-menu with four octets (Oct1–Oct4) plus a `done` item to return. Changing the network mode reboots after a 2-second `bOOt` display.  
Menu times out after 6 seconds of inactivity without saving. The menu resumes at the last-visited item when re-opened.

> Factory reset and OTA update are **not** in the menu — see [System Functions](#system-functions) below.

## System Functions

Both system functions are triggered from **within the menu** by holding both buttons simultaneously. Open the menu first (select short press), then hold both buttons.

### OTA Firmware Update

Open the menu, then hold both buttons for **3 seconds**. The display shows `UPd.----`. Release — the display shows `UPd SurE`. Press **select** to confirm. tapbox saves an update flag, erases OTA data if needed, and reboots. On the next boot, as soon as it gets a network connection, it downloads and installs the latest firmware. The display shows `UPd.` with a progress percentage, then `donE` before rebooting into the new firmware.

### Factory Reset

Open the menu, then hold both buttons for **8 seconds**. The display shows `UPd.----` at 3 s then `rSEt SurE` at 8 s. Release and press **select** to confirm. tapbox resets all settings and reboots. Press **tap**, hold **select**, or wait 6 seconds to cancel.

## WiFi

tapbox prefers Ethernet. WiFi is used automatically when no Ethernet cable is present, and as a fallback if the cable is unplugged while running.

**First-time setup:**

1. Boot tapbox without an Ethernet cable.
2. Connect your phone or laptop to the **tapbox** open WiFi network.
3. Open **http://192.168.4.1** in a browser.
4. Enter your WiFi SSID and password and tap **Save Network — tapbox will reboot**.
5. tapbox reboots and connects to your network as a client.

The ESP32 radio supports **2.4 GHz only**. If your router broadcasts separate 2.4 GHz and 5 GHz SSIDs, use the 2.4 GHz one.

If WiFi credentials are stored but the connection fails, tapbox falls back to AP mode automatically so you can reconfigure without a factory reset.

## Web Configuration

The config page at `http://<tapbox-ip>` is accessible from any browser over Ethernet or WiFi. It has two sections:

| Section | Fields | Button | Effect |
|---------|--------|--------|--------|
| Network | WiFi SSID/password, Ethernet mode, static IP/subnet/gateway | Save Network — tapbox will reboot | Saves and reboots |
| Display | Time signature, sync mode, brightness, nudge size | Save Display Settings | Saves and applies live — no reboot |

## OSC Interface

Send UDP packets to the device on **port 8000**.

| Address | Argument | Action |
|---------|----------|--------|
| `/tap` | — | Same as the tap button |
| `/bpm` | `float` or `int` | Set BPM directly |
| `/signature` | `int` | Set time signature (2–7) |
| `/nudge_up` | — | Advance phase by nudge amount |
| `/nudge_down` | — | Retard phase by nudge amount |
| `/downbeat` | — | Reset downbeat to now |

## Installing

Flash tapbox to your WT32-ETH01 directly from your browser — no software installation required:

**[Open Web Installer](https://dinther.github.io/tapbox/)**

Requires Chrome or Edge. Windows users may need the [CH340 driver](https://www.wch-ic.com/downloads/CH341SER_EXE.html) if the board is not detected.

Pre-built binaries are also available on the [Releases](https://github.com/dinther/tapbox/releases) page for manual flashing with esptool.

## Tools

Two Python scripts in `tools/` assist with development and testing. Both require Python 3 and no external packages.

### CDJ Simulator (`tools/cdj_sim_web.py`)

Broadcasts genuine Pro DJ Link 0x28 beat packets on UDP port 50001 — useful for testing CDJ sync without real hardware.

```bash
python tools/cdj_sim_web.py [bpm] [player]
```
<img width="602" height="817" alt="{47560FA2-C87A-4E2B-8EA1-346BB672038C}" src="https://github.com/user-attachments/assets/3cfe453d-4515-4b64-8fa7-15b0ca07dc36" />

Opens a browser-based control page at **http://localhost:8080** with:
- Live BPM display with ±1 / ±0.1 nudge buttons and a slider
- Beat-in-bar indicator (4 circles, beat 1 highlighted in amber)
- Player selector (1–4)
- Stop / Start toggle (tests the 2-second CDJ timeout on tapbox)
- **Downbeat** button — sends `Bb=1` immediately, triggering a bar-phase snap on tapbox

### Link Monitor (`tools/link_monitor.py`)

Prints the current Ableton Link session tempo and peer count to the terminal, useful for verifying that tapbox is pushing the right BPM into the Link session.

```bash
python tools/link_monitor.py
```

## Building

Requires [PlatformIO](https://platformio.org/).

```bash
# Build
python -m platformio run

# Build and flash
python -m platformio run --target upload
```

## License

MIT
