# tapbox

An ESP32-based tap-tempo controller that joins an [Ableton Link](https://www.ableton.com/en/link/) session over Ethernet or WiFi. Built on the [WT32-ETH01](http://www.wireless-tag.com/portfolio/wt32-eth01/) module with a MAX7219 8-digit 7-segment display.

<img width="994" height="768" alt="image" src="https://github.com/user-attachments/assets/c6e33068-3b57-4dc8-b9bd-07a2b8daecac" />


## Features

- **Tap tempo** — tap 4 times to lock in BPM and phase-align to the Link session
- **Ableton Link** — joins the Link network automatically on boot; peers shown on display
- **Two-button control** — tap button for tempo and menu navigation; select button for confirm/back
- **WiFi** — connects to your WiFi network as a client; browser config page for credentials; auto-failover between Ethernet and WiFi
- **OSC control** — UDP server on port 8000 for remote tap, BPM set, nudge, and downbeat reset
- **Menu system** — on-device configuration with NVS persistence across power cycles
- **Static or DHCP** — configure IP address, subnet, and gateway via menu
- **IP splash on boot** — displays assigned IP address across two screens at startup
- **Battery level** — optional SoC readout via voltage divider on IO36
- **OTA updates** — pull and flash new firmware directly from the menu over Ethernet

## Hardware

| Component | Part |
|-----------|------|
| MCU / Ethernet | WT32-ETH01 (ESP32 + LAN8720A) |
| Display | MAX7219 8-digit 7-segment module |
| Input | Two momentary push buttons (tap + select) |

### Pin Assignments

| GPIO | Function |
|------|----------|
| IO12 | Tap button (internal pull-up) |
| IO4  | Select button (internal pull-up) |
| IO36 | Battery ADC — midpoint of 100 kΩ / 100 kΩ voltage divider from battery+ to GND (optional) |
| IO14 | MAX7219 CLK |
| IO2  | MAX7219 DIN |
| IO15 | MAX7219 LOAD/CS |

> GPIO 0, 16, 18, 19, 21, 22, 23, 25, 26, 27 are used by the onboard Ethernet — do not reassign.  
> GPIO 34–39 are input-only (no internal pull-up). IO36 and IO39 are safe for ADC1 use alongside WiFi.

## Display Layout

**Normal mode:**
```
[ BPM hundreds ][ BPM tens ][ BPM units . ][ BPM tenths ][ blank ][ beat ][ blank ][ peers ]
```
Example: `120.0` at beat 3 of 4 with 2 peers → `·120.0 3 2`

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
| Select button short press | Enter menu / confirm / advance |
| Select button long press (1 s) | Back / exit menu |

## Menu

| Label | Setting | Values |
|-------|---------|--------|
| `Beat` | Time signature | 2, 3, 4, 5, 6, 7 |
| `Acc.` | Accuracy preset | `Lo` (1.0 BPM / 50 ms) · `Std` (0.5 BPM / 20 ms) · `Hi` (0.1 BPM / 5 ms) |
| `Led ` | Display brightness | 1 – 15 (live preview) |
| `Lan.` | Network mode | `Auto` (DHCP) · `Stat` (static) |
| `IP  ` | Static IP address | sub-menu: Oct1–Oct4, 0–255 each |
| `Sub.` | Subnet mask | sub-menu: Oct1–Oct4, 0–255 each |
| `Hub.` | Gateway | sub-menu: Oct1–Oct4, 0–255 each |
| `rSEt` | Factory reset | confirm with second select press |
| `UPd.` | OTA firmware update | downloads firmware.bin from GitHub Pages and reboots (Ethernet required) |
| `vEr ` | Firmware version | read-only; shows major.minor.patch |
| `bAt ` | Battery level | read-only; shows 0–100 (requires IO36 voltage divider) |
| `AP  ` | WiFi access point | restarts the "tapbox" config AP |
| `done` | Exit menu | returns to normal mode |

`Acc.` controls both the tap auto-increment step and the OSC nudge amount.  
`IP`, `Sub.`, and `Hub.` are only shown when network mode is `Stat`. Each opens a sub-menu with four octets (Oct1–Oct4) plus a `done` item to return. Changing the network mode reboots after a 2-second `bOOt` display.  
Menu times out after 6 seconds of inactivity without saving. The menu resumes at the last-visited item when re-opened.

## WiFi

tapbox prefers Ethernet. WiFi is used automatically when no Ethernet cable is present, and as a fallback if the cable is unplugged while running.

**First-time setup:**

1. Boot tapbox without an Ethernet cable (or use the `AP` menu item).
2. Connect your phone or laptop to the **tapbox** open WiFi network.
3. Open **http://192.168.4.1** in a browser.
4. Enter your WiFi SSID and password, adjust any other settings, and tap **Save**.
5. tapbox reboots and connects to your network as a client.

The ESP32 radio supports **2.4 GHz only**. If your router broadcasts separate 2.4 GHz and 5 GHz SSIDs, use the 2.4 GHz one.

WiFi shuts down automatically after 60 seconds if no connection is established.

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
