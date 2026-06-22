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
- **2** — how many other Link peers are connected to the session

On startup, tapbox joins the existing Link session tempo if one is already running, otherwise it starts at 120 BPM. You can tap a new tempo whenever you are ready.

### The Controls

tapbox has two buttons:

**Tap button** — the main button. Tap it in time with your music to set the tempo. A single tap immediately defines the downbeat (first beat of the bar). Three more taps locks in the tempo. Pressing the tap button in the menu moves to the next item or increments a value — hold it to auto-increment.

**Select button** — the confirm button. A short press enters the menu from normal mode, steps into an edit field, or confirms a value. Hold it for one second to go back or exit the menu.

---

## Setting the Tempo

Tap the tap button in rhythm. The downbeat for the Link session is immediately defined by the first tap. It takes three more taps before tapbox starts to adjust the tempo of the Link session. The current tempo and beat progress is displayed live at all times.

You do not have to be perfectly precise. tapbox averages the timing across all your taps, so the more taps in sequence, the more accurate the reading becomes.

After you are locked in, any further taps continue to refine the tempo. tapbox considers the session established when you stop tapping for two seconds. Simply start tapping again to define a new tempo.

---

## Connecting to Your Network

### Ethernet

Plug a standard network cable into the Ethernet port before powering on. tapbox displays the assigned IP address across two brief screens at startup — first the first two octets, then the last two. This tells you where to find it on the network for OSC control.

Ethernet is always preferred over WiFi. If both are available, tapbox uses Ethernet.

### WiFi

tapbox can connect to your WiFi network and run Ableton Link over it. This is useful when your performance space does not have an Ethernet switch nearby, or when you need to sync with a device that is on WiFi and your router bridges multicast between the two interfaces.

**The ESP32 radio supports 2.4 GHz only.** If your router has separate 2.4 GHz and 5 GHz networks (often shown as two different SSIDs, or a combined band with a single SSID), make sure you use the 2.4 GHz SSID when entering credentials.

#### First-time WiFi setup

1. Boot tapbox without an Ethernet cable plugged in, or select `AP` from the menu at any time.
2. tapbox creates an open WiFi network called **tapbox**.
3. Connect your phone or laptop to the **tapbox** network.
4. Open **http://192.168.4.1** in your browser.
5. Enter your WiFi network name (SSID) and password. The SSID field is case-sensitive — copy it exactly from your phone's WiFi list.
6. Adjust any other settings if needed, then tap **Save**.
7. tapbox saves the credentials and reboots. It connects to your network as a WiFi client and displays its assigned IP address.

#### Changing WiFi credentials later

Select `AP` in the menu. tapbox restarts its access point so you can connect and update the credentials via the same browser page.

#### Automatic Ethernet / WiFi switching

tapbox switches interfaces automatically without a reboot:

- **Ethernet plugged in** → tapbox stops WiFi and switches Link to Ethernet.
- **Ethernet unplugged** → tapbox starts WiFi (connecting to stored credentials, or starting the access point if none are stored).

If WiFi does not establish a connection within 60 seconds, it shuts down automatically to save power.

By default tapbox uses DHCP for the Ethernet connection. If you need a fixed address, configure a static IP in the `Lan.` / `IP` / `Sub.` / `Hub.` menu items.

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

### Acc. — Accuracy

**What it does:** sets the control sensitivity for both the tap auto-increment step (when holding the tap button in the menu) and OSC nudge commands.

| Setting | Value step | OSC nudge |
|---------|-----------|-----------|
| **Lo** | 1.0 BPM | 50 ms |
| **Std** | 0.5 BPM | 20 ms |
| **Hi** | 0.1 BPM | 5 ms |

**Lo** is best for live performance where you need to shift tempo decisively between sections.

**Std** is the everyday setting. Half a BPM per step gives enough resolution without overshooting.

**Hi** is for studio work where you are hunting for an exact tempo or need surgical phase correction.

*Default: Std*

---

### Led — Display Brightness

**What it does:** adjusts how bright the display glows, from a dim 1 up to a full-intensity 15.

The display gives you a live preview as you change the value. In a dark venue, turning it down to 3 or 4 keeps it readable without becoming distracting. In a brightly lit studio, 15 is easy to read from across the room.

*Default: 7*

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

### rSEt — Factory Reset

**What it does:** returns all settings to their original defaults, including clearing stored WiFi credentials.

When you select `rSEt` and press select, the display shows `rSEt SurE`. Press select once more to confirm. tapbox resets accuracy to Std, brightness to 7, network to Auto, static address to 192.168.1.200 / 255.255.255.0 / 192.168.1.1, and clears any saved WiFi SSID and password.

---

### UPd. — Firmware Update

**What it does:** downloads the latest firmware from the internet and installs it automatically.

Select `UPd.` and press select. tapbox connects to GitHub over Ethernet and downloads the latest release. The display shows a percentage as the download progresses. When it reaches 100, tapbox shows `donE` and reboots.

If something goes wrong (no network, server unreachable) the display shows `Er` and returns you to the menu without making changes.

**Requirements:** tapbox must be connected to the internet via Ethernet. OTA update does not run over WiFi.

*This item does nothing if the Ethernet cable is unplugged.*

---

### vEr — Firmware Version

**What it does:** shows the firmware version currently running on your tapbox as `major.minor.patch` — for example, version 1.3.0 appears as `1.3.0`. Read-only.

---

### bAt — Battery Level

**What it does:** shows the estimated state of charge of the connected battery as a percentage from 0 to 100.

Requires a 100 kΩ / 100 kΩ voltage divider connected from battery positive to GND, with the midpoint wired to IO36. If not fitted the reading is meaningless.

Accuracy is approximately ±10%. Read-only.

---

### AP — WiFi Access Point

**What it does:** restarts the tapbox WiFi access point so you can connect and update WiFi credentials or other settings via the browser.

After selecting `AP` and pressing confirm, tapbox creates the **tapbox** open WiFi network. Connect your phone or laptop to it and open **http://192.168.4.1**. The access point shuts down automatically after 60 seconds if no device connects.

Use this item to change which WiFi network tapbox connects to, or to re-enter a password after changing it on the router.

---

### done — Exit Menu

Returns to normal mode immediately.

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

**Tapping in from scratch:** for the most accurate reading, tap along with a steady source — a click track, a drum loop, or a song in headphones. Four clean taps is all you need to go live.

**The select button exits the menu.** Hold the select button for one second to back out or exit at any time, even mid-edit.

**Brightness at gigs:** set the brightness at soundcheck under the actual stage lighting, then save it. What looks comfortable in daylight may be too bright or too dim under stage wash.

**Using a static IP with OSC:** setting a static IP once means your OSC routing works every time without checking the display at each session.

**WiFi SSID is case-sensitive.** If tapbox cannot connect (reason 201 in the serial log), check that the SSID in the config page matches exactly — including capital letters and spaces.

**2.4 GHz only:** the ESP32 radio does not support 5 GHz WiFi. If your router broadcasts both bands under the same name, tapbox will find the 2.4 GHz one automatically. If it broadcasts them separately, enter the 2.4 GHz SSID.

**Keeping firmware up to date:** navigate to `UPd.` while connected via Ethernet and press select. The update takes about 30 seconds. Settings are not affected.

---

## Troubleshooting

**The IP address does not appear at boot.**  
tapbox could not connect to Ethernet within 10 seconds. Check the cable. If no cable is connected, tapbox will start WiFi instead.

**tapbox shows dashes on the display after boot.**  
It is connected to the network but has not received any Link peers yet. This is normal — the display fills in once another Link device joins the session.

**WiFi connects but peers show 0.**  
Ensure the other device (e.g. MadMapper on a laptop) is on the same network. Link uses UDP multicast — some routers do not bridge multicast between WiFi and Ethernet. If your PC is on Ethernet and tapbox is on WiFi, try connecting tapbox via Ethernet instead.

**tapbox cannot find my WiFi network (or connects then immediately disconnects).**  
Check that you entered the SSID exactly as it appears on your phone — SSIDs are case-sensitive. Also confirm the network is 2.4 GHz; the ESP32 cannot connect to 5 GHz networks. Use the `AP` menu item to reopen the config page.

**My OSC messages are not reaching tapbox.**  
Confirm the IP address on the display at next boot and update your OSC destination. If using a static IP, verify the address, subnet, and gateway are correct.

**I set a static IP and now tapbox is unreachable.**  
Use the factory reset (`rSEt` in the menu) to return to Auto DHCP. The display will show the assigned address at the next boot.

**The tempo drifts slightly after many taps.**  
tapbox calculates BPM as an average across all taps in the session. Small variations in tap timing do shift the average, though the effect becomes smaller with each additional tap. For a locked-in tempo, tap steadily for 8 or more beats, then stop and let Link hold the tempo.
