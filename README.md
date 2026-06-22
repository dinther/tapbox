# tapbox

An ESP32-based tap-tempo controller that joins an [Ableton Link](https://www.ableton.com/en/link/) session over Ethernet. Built on the [WT32-ETH01](http://www.wireless-tag.com/portfolio/wt32-eth01/) module with a MAX7219 8-digit 7-segment display.

<img width="994" height="768" alt="image" src="https://github.com/user-attachments/assets/c6e33068-3b57-4dc8-b9bd-07a2b8daecac" />


## Features

- **Tap tempo** — tap 4 times to lock in BPM and phase-align to the Link session
- **Ableton Link** — joins the Link network automatically on boot; peers shown on display
- **Encoder** — fine-tune BPM up/down; step size set by Acc. preset (Lo / Std / Hi)
- **OSC control** — UDP server on port 8000 for remote tap, BPM set, nudge, and downbeat reset
- **Menu system** — on-device configuration with NVS persistence across power cycles
- **Static or DHCP** — configure IP address, subnet, and gateway via menu sub-menus
- **IP splash on boot** — displays assigned IP address across two screens at startup
- **Battery level** — optional SoC readout via voltage divider on IO4
- **OTA updates** — pull and flash new firmware directly from the menu over Ethernet

## Hardware

| Component | Part |
|-----------|------|
| MCU / Ethernet | WT32-ETH01 (ESP32 + LAN8720A) |
| Display | MAX7219 8-digit 7-segment module |
| Input | Rotary encoder with push switch + momentary tap button |

### Pin Assignments

| GPIO | Function |
|------|----------|
| IO12 | Tap button (internal pull-up) |
| IO36 | Encoder A (encoder board pull-up) |
| IO39 | Encoder B (encoder board pull-up) |
| IO35 | Encoder switch (encoder board pull-up, input-only pin) |
| IO14 | MAX7219 CLK |
| IO2  | MAX7219 DIN |
| IO15 | MAX7219 LOAD/CS |
| IO4  | Battery ADC — midpoint of 100 kΩ / 100 kΩ voltage divider from battery+ to GND (optional) |

> GPIO 0, 16, 18, 19, 21, 22, 23, 25, 26, 27 are used by the onboard Ethernet — do not reassign.

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

## Menu

Press the encoder to enter the menu. Turn to navigate, press to edit a value, press again to confirm.  
The tap button exits the menu immediately (and also performs a tap).

| Label | Setting | Values |
|-------|---------|--------|
| `Beat` | Time signature | 2, 3, 4, 5, 6, 7 |
| `Acc.` | Accuracy preset | `Lo` (1.0 BPM / 50 ms) · `Std` (0.5 BPM / 20 ms) · `Hi` (0.1 BPM / 5 ms) |
| `Led ` | Display brightness | 1 – 15 (live preview) |
| `Lan.` | Network mode | `Auto` (DHCP) · `Stat` (static) |
| `IP  ` | Static IP address | sub-menu: Oct1–Oct4, 0–255 each |
| `Sub.` | Subnet mask | sub-menu: Oct1–Oct4, 0–255 each |
| `Hub.` | Gateway | sub-menu: Oct1–Oct4, 0–255 each |
| `rSEt` | Factory reset | confirm with second press |
| `UPd.` | OTA firmware update | downloads firmware.bin from GitHub Pages and reboots |
| `vEr ` | Firmware version | read-only; shows major.minor.patch |
| `bAt ` | Battery level | read-only; shows 0–100 (requires IO4 voltage divider) |
| `done` | Exit menu | returns to normal mode |

`Acc.` controls both the encoder BPM step and the OSC nudge amount together.  
`IP`, `Sub.`, and `Hub.` are only shown when network mode is `Stat`. Each opens a sub-menu with four octets (Oct1–Oct4) plus a `done` item to return. Changing the network mode reboots after a 2-second `bOOt` display.  
Static IP defaults: `192.168.1.200` / `255.255.255.0` / `192.168.1.1`.  
Menu times out after 6 seconds of inactivity without saving. The menu resumes at the last-visited item when re-opened.

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

Requires [PlatformIO](https://platformio.org/) and the [abl_link](https://github.com/Ableton/link) C library.

```bash
# Build
python -m platformio run

# Build and flash
python -m platformio run --target upload
```

## License

MIT
