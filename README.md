# tapbox

An ESP32-based tap-tempo controller that joins an [Ableton Link](https://www.ableton.com/en/link/) session over Ethernet. Built on the [WT32-ETH01](http://www.wireless-tag.com/portfolio/wt32-eth01/) module with a MAX7219 8-digit 7-segment display.

## Features

- **Tap tempo** — tap 4 times to lock in BPM and phase-align to the Link session
- **Ableton Link** — joins the Link network automatically on boot; peers shown on display
- **Encoder** — fine-tune BPM up/down in configurable steps (0.1 / 0.2 / 0.5 / 1.0)
- **OSC control** — UDP server on port 8000 for remote tap, BPM set, nudge, and downbeat reset
- **Menu system** — on-device configuration with NVS persistence across power cycles
- **Static or DHCP** — configure IP address, subnet, and gateway via menu
- **IP splash on boot** — displays assigned IP address across two screens at startup

## Hardware

| Component | Part |
|-----------|------|
| MCU / Ethernet | WT32-ETH01 (ESP32 + LAN8720A) |
| Display | MAX7219 8-digit 7-segment module |
| Input | Rotary encoder with push switch + momentary tap button |

### Pin Assignments

| GPIO | Function |
|------|----------|
| IO4  | Tap button (internal pull-up) |
| IO36 | Encoder A (encoder board pull-up) |
| IO39 | Encoder B (encoder board pull-up) |
| IO35 | Encoder switch (encoder board pull-up, input-only pin) |
| IO14 | MAX7219 CLK |
| IO2  | MAX7219 DIN |
| IO15 | MAX7219 LOAD/CS |

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
| `beat` | Time signature | 2, 3, 4, 5, 6, 7 |
| `StEP` | Encoder BPM step | 0.1, 0.2, 0.5, 1.0 |
| `nudg` | OSC nudge amount | 5, 10, 20, 50, 100 ms |
| `brit` | Display brightness | 1 – 15 |
| `nEt` | Network mode | `dhcP` / `StAt` |
| `IP 1`–`IP 4` | Static IP address octets | 0 – 255 |
| `Sn 1`–`Sn 4` | Subnet mask octets | 0 – 255 |
| `Gt 1`–`Gt 4` | Gateway octets | 0 – 255 |
| `rSEt` | Factory reset | confirm with second press |

IP / subnet / gateway items are only shown when network mode is `StAt`. Changing the network mode reboots the device after a 2-second `bOOt` display.

Menu times out after 6 seconds of inactivity without saving.

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
