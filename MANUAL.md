# tapbox User Manual

---

## What Is tapbox?

tapbox is a compact, dedicated tempo controller for musicians and live performers who use Ableton Link. It sits on your desk or in your rack, connects to your network via Ethernet or WiFi, and keeps every device in your setup locked to the same beat — without you having to touch a laptop.

The idea is simple: tap the button on the beat to set your tempo, and tapbox broadcasts it to every Link-enabled device on the network. Your Ableton Live session, Resolume, MadMapper, LaserOS, your iOS apps, your hardware synths with Link support — they all lock in instantly. From that point on you can send commands from a phone or tablet over OSC if you prefer to stay hands-free.

tapbox is always on, always listening, and always in sync.

---

## Getting to Know Your tapbox

### The Display

tapbox uses an 8-digit display that shows you everything you need at a glance. In normal operation it looks like this:

```
  1 2 0 . 0   3   2
```

Reading left to right:

- **120.0** — the current tempo in BPM, updated in real time when any device in the Link session adjusts it
- **3** — which beat of the bar you are on right now (this advances with the music)
- **C** — appears when tapbox is locked to a Pioneer CDJ via Pro DJ Link; blank when not active
- **2** — how many other Link peers are connected to the session

On startup, tapbox joins the existing Link session tempo if one is already running, otherwise it starts at 120 BPM. You can tap a new tempo whenever you are ready.

### The Controls

tapbox has two buttons:

**Tap button** — the main button. Tap it in time with your music to set the tempo. A single tap immediately defines the downbeat (first beat of the bar). Three more taps locks in the tempo. In the menu, pressing tap moves to the next item or increments a value — hold it to auto-increment.

**Select button** — the confirm button. A short press enters the menu from normal mode, steps into an edit field, or confirms a value. Hold it for one second to go back or exit the menu. When `UPd SurE` or `rSEt SurE` is showing, a short press of select confirms the action; press tap or hold select for 1 second to cancel.

---

## Setting the Tempo

Tap the tap button in rhythm. The downbeat for the Link session is immediately defined by the first tap. It takes three more taps before tapbox starts to adjust the tempo of the Link session. The current tempo and beat progress is displayed live at all times.

You do not have to be perfectly precise. tapbox averages the timing across all your taps, so the more taps in sequence, the more accurate the reading becomes.

After you are locked in, any further taps continue to refine the tempo. tapbox considers the session established when you stop tapping for two seconds. Simply start tapping again to define a new tempo.

---

## Connecting to Your Network

### Ethernet

Plug a standard network cable into the Ethernet port before powering on. tapbox scrolls the connection type and assigned IP address across the display at startup — for example `Eth 192.168.1.55`. This tells you where to find it on the network for OSC control.

Ethernet is always preferred over WiFi. If both are available, tapbox uses Ethernet.

### WiFi

tapbox can connect to your WiFi network and run Ableton Link over it. This is useful when your performance space does not have an Ethernet switch nearby, or when you need to sync with a device that is on WiFi and your router bridges multicast between the two interfaces.

**The ESP32 radio supports 2.4 GHz only.** If your router has separate 2.4 GHz and 5 GHz networks (often shown as two different SSIDs, or a combined band with a single SSID), make sure you use the 2.4 GHz SSID when entering credentials.

#### First-time WiFi setup

1. Boot tapbox without an Ethernet cable plugged in.
2. tapbox creates an open WiFi network called **tapbox** and scrolls `AP 192.168.4.1` across the display.
3. Connect your phone or laptop to the **tapbox** network.
4. Open **http://192.168.4.1** in your browser.
5. Enter your WiFi network name (SSID) and password. The SSID field is case-sensitive — copy it exactly from your phone's WiFi list.
6. Tap **Save Network — tapbox will reboot**. The device saves the credentials and reboots, then connects to your network as a WiFi client and scrolls `SSID` followed by the assigned IP address.

#### Changing WiFi credentials later

Unplug the Ethernet cable (or boot without one). If the stored credentials fail, tapbox falls back to AP mode automatically — connect to the **tapbox** network and open **http://192.168.4.1** to update them.

#### Automatic Ethernet / WiFi switching

tapbox switches interfaces automatically without a reboot:

- **Ethernet plugged in** → tapbox stops WiFi and switches Link to Ethernet.
- **Ethernet unplugged** → tapbox starts WiFi (connecting to stored credentials, or starting the access point if none are stored).

If WiFi credentials fail to connect, tapbox falls back to AP mode immediately so you can reconfigure without rebooting.

By default tapbox uses DHCP for the Ethernet connection. If you need a fixed address, configure a static IP in the `Lan.` / `IP` / `Sub.` / `Hub.` menu items.

### Web Configuration Page

The config page is available at the device IP address on port 80 from any browser, over Ethernet or WiFi. It is split into two independent sections:

**Network** (WiFi SSID/password, Ethernet mode, static IP/subnet/gateway) — tap **Save Network — tapbox will reboot** to apply. The orange button is a reminder that a reboot follows.

**Display** (time signature, brightness, accuracy) — tap **Save Display Settings** to apply immediately. No reboot occurs; the device updates live and the page returns with a confirmation link.

---

## The Settings Menu

Press the select button to open the menu. Press the tap button to move between items. Press the select button to enter edit mode for the selected item, then press the tap button to change the value (hold for fast auto-increment). Press select again to confirm and return to the menu.

To go back from any edit or sub-menu, hold the select button for one second. To exit the menu entirely, hold select or wait six seconds — tapbox returns to normal mode without saving the current edit. When you next open the menu it returns to the last item you were on.

---

### Beat — Time Signature

**What it does:** tells tapbox how many beats are in a bar.

The beat counter on the display counts from 1 up to this number, then loops back to 1. This keeps you visually oriented in the musical phrase.

**Available values:** 2, 3, 4, 5, 6, 7

**When to use it:** if you are playing a waltz in 3/4, set this to 3 so the counter runs 1–2–3 rather than counting to 4 and going out of sync with the phrase. For odd-time signatures like 7/8, set it to 7.

*Default: 4*

---

### nud — Nudge Size

**What it does:** sets the OSC nudge amount — how far the beat phase shifts in response to a `/nudge_up` or `/nudge_down` command.

Three options: **50 ms**, **20 ms**, **5 ms**.

50 ms is best for live performance where you need to push or pull the phase decisively. 5 ms is for studio work where you need surgical phase correction. 20 ms is a good everyday value.

*Default: 20 ms*

---

### Led — Display Brightness

**What it does:** adjusts how bright the display glows across four levels (1 – 4).

The display gives you a live preview as you change the value. In a dark venue, level 1 or 2 keeps it readable without becoming distracting. In a brightly lit studio, level 4 is easy to read from across the room.

*Default: 2*

---

### Cdj — Pioneer CDJ Sync

**What it does:** enables or disables passive listening for Pioneer Pro DJ Link beat packets on the network.

When **On**, tapbox listens on the same Ethernet switch as your CDJ players and reads their beat timing automatically. The current BPM from the active CDJ is fed directly into the Ableton Link session — all your Link peers follow the CDJ without any manual tapping required. A `C` indicator appears on the display between the beat counter and the peer count to confirm the lock is active.

tapbox follows whichever CDJ has the lowest player number (1 → 2 → 3 → 4). If that player stops sending for more than two seconds — because it was stopped or disconnected — tapbox drops down to the next available player. If no CDJ is found on the network, tapbox falls back to tap-tempo as normal.

While CDJ sync is active, the tap button does not affect the tempo — the CDJ is in control. Turn CDJ sync **Off** in this menu to regain manual tap-tempo control.

> CDJ sync requires Ethernet. tapbox must be on the same wired network switch as the CDJ players. No software needs to be installed on the CDJs — tapbox listens passively and the CDJs do not know it is there.

*Default: On*

---

### Lan. — Network Mode

**What it does:** switches the Ethernet interface between automatic (DHCP) and manual (static) IP addressing.

- **Auto** — your router assigns tapbox an IP address automatically every time it boots.
- **Stat** — tapbox uses a fixed IP address that you configure yourself.

**When to use static:** if you send OSC commands from a DAW or control surface with a hard-coded destination address, a static IP ensures that address never changes between reboots.

When you confirm a change to this setting, tapbox displays `bOOt` and restarts automatically.

*Default: Auto*

---

### IP, Sub., Hub. — Static Network Address

These three items only appear when network mode is **Stat**. Each opens a sub-menu with four octets labelled **Oct1** through **Oct4**.

- **IP** — the static IP address (e.g. 192 . 168 . 1 . 50)
- **Sub.** — the subnet mask (e.g. 255 . 255 . 255 . 0)
- **Hub.** — the gateway address, usually your router (e.g. 192 . 168 . 1 . 1)

Press select to enter the sub-menu. Press the tap button to move between Oct1–Oct4 and the **done** item. Press select to edit an octet, tap to increment, select to confirm. Navigate to **done** and press select to return to the main menu.

Factory defaults: **192.168.1.200** / **255.255.255.0** / **192.168.1.1**.

---

---

### vEr — Firmware Version

**What it does:** shows the firmware version currently running on your tapbox as `major.minor.patch` — for example, version 1.3.0 appears as `1.3.0`. Read-only.

---

### bAt — Battery Level

**What it does:** shows the estimated state of charge of the connected battery as a percentage from 0 to 100.

Requires a 100 kΩ / 100 kΩ voltage divider connected from battery positive to GND, with the midpoint wired to IO36. If not fitted the reading is meaningless.

Accuracy is approximately ±10%. Read-only.

---

### done — Exit Menu

Returns to normal mode immediately.

---

## System Functions

Both system functions are activated from **within the menu** by holding both buttons at the same time. Open the menu first (select short press), then hold both buttons. You do not need to power-cycle the device.

---

### OTA Firmware Update

Open the menu, then hold both the **tap button** and the **select button** for **3 seconds**. The display shows `UPd.----`. Release both buttons — the display changes to `UPd SurE`.

Press **select** to confirm. tapbox saves a pending-update flag to memory, erases OTA data if necessary to return to the factory slot, and reboots. On the next boot, as soon as it obtains a network connection (Ethernet or WiFi), it downloads and installs the latest firmware automatically. The display shows `UPd.` followed by a progress percentage. When the percentage reaches 100, it shows `donE` and reboots into the new firmware.

If the download fails or the server is unreachable, the display shows `Er` and tapbox continues to boot normally. All settings are preserved across updates; only the firmware changes.

To cancel: press **tap**, hold **select** for 1 second, or wait 6 seconds — tapbox returns to normal without scheduling an update.

---

### Factory Reset

Open the menu, then hold both the **tap button** and the **select button** for **8 seconds**. You will see `UPd.----` at 3 seconds and then `rSEt SurE` at 8 seconds. Release the buttons.

Press **select** to confirm. tapbox resets all settings to factory defaults — accuracy to Std, brightness to 2, network to Auto, static address to 192.168.1.200 / 255.255.255.0 / 192.168.1.1 — clears any stored WiFi SSID and password, and reboots.

To cancel: press nothing (or press tap). The display returns to normal after 6 seconds without resetting anything.

After a factory reset the device boots into the original firmware from the factory partition. This guarantees the device can always be returned to working condition regardless of what happened during a previous OTA update.

---

## OSC Control

tapbox listens for OSC messages on **UDP port 8000**. Send your messages to the IP address shown on the display at boot.

| Command | What it does |
|---------|-------------|
| `/tap` | Same as pressing the tap button |
| `/bpm <value>` | Set the tempo to a specific BPM |
| `/signature <value>` | Change the time signature (2 through 7) |
| `/nudge_up` | Shift the beat phase forward by the nudge amount |
| `/nudge_down` | Shift the beat phase backward by the nudge amount |
| `/downbeat` | Reset the downbeat to this exact moment |

**A few ways to put this to use:**

- Map `/tap` to a pad on a MIDI controller via your DAW so the whole band can tap tempo from the stage.
- Send `/bpm 128` from an Ableton Live clip to snap the tempo to a specific value at the start of a track.
- Use `/downbeat` at the top of a new section to re-align the beat grid after a break.
- Assign `/nudge_up` and `/nudge_down` to fader buttons on a mixing desk to subtly push and pull the phase.

---

## Tips and Tricks

**CDJ sync with Ableton Live:** plug tapbox into the same Ethernet switch as your CDJ players with CDJ sync turned On. The moment a CDJ starts playing, the `C` indicator lights up and every Ableton Live instance on the network locks to the CDJ tempo automatically — no tapping, no MIDI clock, no configuration on the CDJs.

**Tapping in from scratch:** for the most accurate reading, tap along with a steady source — a click track, a drum loop, or a song in headphones. Four clean taps is all you need to go live.

**The select button exits the menu.** Hold the select button for one second to back out or exit at any time, even mid-edit.

**Brightness at gigs:** set the brightness at soundcheck under the actual stage lighting, then save it. What looks comfortable in daylight may be too bright or too dim under stage wash.

**Using a static IP with OSC:** setting a static IP once means your OSC routing works every time without checking the display at each session.

**WiFi SSID is case-sensitive.** If tapbox cannot connect (reason 201 in the serial log), check that the SSID in the config page matches exactly — including capital letters and spaces.

**2.4 GHz only:** the ESP32 radio does not support 5 GHz WiFi. If your router broadcasts both bands under the same name, tapbox will find the 2.4 GHz one automatically. If it broadcasts them separately, enter the 2.4 GHz SSID.

**Keeping firmware up to date:** open the menu, hold both buttons for 3 seconds and release, then confirm with select. The update runs automatically on the next boot as soon as tapbox gets a network connection — Ethernet or WiFi. Takes about 30 seconds. Settings are not affected.

---

## CDJ Simulator (Development Tool)

If you do not have a physical CDJ player available, `tools/cdj_sim_web.py` is a Python script that broadcasts genuine Pro DJ Link beat packets over UDP on port 50001. Run it on any computer on the same network as tapbox to test CDJ sync without real hardware.

```bash
python tools/cdj_sim_web.py [bpm] [player]
```

This opens a control page in your browser at **http://localhost:8080**. From there you can:

- Set BPM with the slider, the ±1 / ±0.1 nudge buttons, or keyboard arrows
- Select which player number (1–4) the simulator pretends to be
- Stop and start the beat stream to test the two-second CDJ timeout behaviour on tapbox
- Press **Downbeat** to force `beat 1` of the bar immediately — tapbox snaps its Ableton Link phase to match

The simulator requires Python 3 and no external packages.

---

## Troubleshooting

**No IP address scrolls across the display at boot.**  
tapbox could not connect to Ethernet within the boot timeout (3 seconds for static IP, 5 seconds for DHCP). Check the cable. If no cable is connected, tapbox starts WiFi — the display will scroll the IP once a connection is established, or `AP 192.168.4.1` if it falls back to access point mode.

**tapbox shows dashes on the display after boot.**  
It is connected to the network but has not received any Link peers yet. This is normal — the display fills in once another Link device joins the session.

**WiFi connects but peers show 0.**  
Ensure the other device (e.g. MadMapper on a laptop) is on the same network. Link uses UDP multicast — some routers do not bridge multicast between WiFi and Ethernet. If your PC is on Ethernet and tapbox is on WiFi, try connecting tapbox via Ethernet instead.

**tapbox cannot find my WiFi network (or connects then immediately disconnects).**  
Check that you entered the SSID exactly as it appears on your phone — SSIDs are case-sensitive. Also confirm the network is 2.4 GHz; the ESP32 cannot connect to 5 GHz networks. If credentials fail, tapbox falls back to AP mode automatically — connect to the **tapbox** network and open **http://192.168.4.1** to correct them.

**My OSC messages are not reaching tapbox.**  
Confirm the IP address on the display at next boot and update your OSC destination. If using a static IP, verify the address, subnet, and gateway are correct.

**I set a static IP and now tapbox is unreachable.**  
Use the factory reset (open the menu, hold both buttons 8 s, then confirm with select) to return to Auto DHCP. The display will show the assigned address at the next boot.

**The display is very dim after a restart.**
tapbox detected a brownout (power dip) during the previous session and has automatically set brightness to level 1 to protect against a repeat. You can raise it in the menu under `Led` and save. If it keeps happening, check your power supply.

**The tempo drifts slightly after many taps.**  
tapbox calculates BPM as an average across all taps in the session. Small variations in tap timing do shift the average, though the effect becomes smaller with each additional tap. For a locked-in tempo, tap steadily for 8 or more beats, then stop and let Link hold the tempo.
