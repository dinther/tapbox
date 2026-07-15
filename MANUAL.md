# tapbox User Manual

---

## What Is tapbox?

tapbox is a compact, dedicated tempo controller for musicians and live performers who use Ableton Link. It sits on your desk or in your rack, connects to your network via Ethernet or WiFi, and keeps every device in your setup locked to the same beat — without you having to touch a laptop.

The idea is simple: tap the button on the beat to set your tempo, and tapbox broadcasts it to every Link-enabled device on the network. Your Ableton Live session, Resolume, MadMapper, LaserOS, your iOS apps, your hardware synths with Link support — they all lock in instantly. From that point on you can send commands from a phone or tablet over OSC if you prefer to stay hands-free.

tapbox is always on, always listening, and always in sync.

---

## Getting to Know Your tapbox

### The Display

tapbox uses an 8-digit display that shows you everything you need at a glance.

![Display layout](docs/display.png)

Reading left to right:

- **beat** — the current tempo in BPM (`120.0`), updated in real time when any device in the Link session adjusts it
- **count** — which beat of the bar you are on right now (advances with the music)
- **lock dot** — the decimal point on the count digit: **solid** = locked (CDJ active / audio stable / tap set); **blinking** = Mic or Line source listening but not yet stable; **off** = no lock
- **source bar** — horizontal segments just after the count digit show which source is active: **top = CDJ**, **middle = Tap**, **bottom = Mic**, **top + bottom = Line**
- **peers** — how many other Ableton Link peers are connected to the session

On startup, tapbox joins the existing Link session tempo if one is already running, otherwise it starts at 120 BPM. You can tap a new tempo whenever you are ready.

### The Controls

tapbox has two buttons:

**Tap button** — the main button. Tap it in time with your music to set the tempo. A single tap immediately defines the downbeat (first beat of the bar). Three more taps locks in the tempo. In the menu, pressing tap moves to the next item or increments a value — hold it to auto-increment.

**Select button** — the confirm button. A short press enters the menu from normal mode, steps into an edit field, or confirms a value. Hold it for one second to go back or exit the menu. Hold select for 1 second to cancel.

---

## Setting the Tempo

Tap the tap button in rhythm. The downbeat for the Link session is immediately defined by the first tap. It takes three more taps before tapbox starts to adjust the tempo of the Link session. The current tempo and beat progress is displayed live at all times.

You do not have to be perfectly precise. tapbox averages the timing across all your taps, so the more taps in sequence, the more accurate the reading becomes.

After you are locked in, any further taps continue to refine the tempo. tapbox considers the session established when you stop tapping for two seconds. Simply start tapping again to define a new tempo.

---

## Sync Sources

tapbox can get its tempo four different ways. You pick one with the <img src="docs/menu_glyph_src.png" alt="Src" height="26" valign="middle"> menu item, and the active source is shown by a bar on the display (top = CDJ, middle = Tap, bottom = Mic, top + bottom = Line).

### Tap (`tAP`)

Classic tap tempo, exactly as described above. You are in full control — tap four times to set the tempo and the downbeat.

### Mic (`Aud`)

A small microphone listens to the room and works out the tempo for you, while **you** tap the downbeat. This is the hybrid mode: the machine handles the BPM, you handle the musical "beat 1".

How to use it:

1. Switch <img src="docs/menu_glyph_src.png" alt="Src" height="26" valign="middle"> to <img src="docs/menu_value_aud.png" alt="Aud" height="26" valign="middle">. The bottom bar lights up. Nothing happens to the tempo yet — the mic is just listening.
2. Play the music near the microphone.
3. **Tap once on the beat** to accept the detected tempo and set the downbeat to that moment — or **tap four times** if you want to set the tempo yourself and let the mic refine it from there.
4. The beat digit's decimal point **blinks** while the mic is searching for a stable lock, then goes **solid** once locked. From then on the tempo tracks the music automatically; a single tap any time re-aligns the downbeat without changing the tempo.

The detector analyzes the full spectrum via an FFT/mel-filterbank pipeline rather than a single frequency band, so it isn't limited to a specific kick sound. The web config page's BPM tuning tab carries a live diagnostic chart plus four sliders that tune the detector's sensitivity to your source and room, and how far you trust it to refine your tap — see **Audio Tuning** below, and the technical write-up in `BEAT_DETECTION.md` for how it works.

### Line (`LinE`)

The same hybrid beat detection as Mic, but fed directly from the **3.5 mm line input** instead of the microphone — plug in a feed from your mixer (booth/aux/record out), a media player, or a phone.

Everything works exactly as described for Mic: tap once to accept the detected tempo and set the downbeat, tap four times to override. The difference is the signal quality: line-in hears the music electrically, so there is no room noise, no chatter from the audience, and no acoustic delay between the speakers and a microphone. If a line feed is practical in your setup, prefer it over the mic — it locks faster and holds through quiet passages that would starve the microphone.

Switching between Mic and Line resets the detector, so re-tap once after switching to re-anchor.

### CDJ (`Cdj`)

tapbox passively reads Pioneer Pro DJ Link beat packets off the network and feeds the CDJ's tempo straight into Ableton Link — no tapping needed. See the CDJ details below.

---

## Connecting to Your Network

### Ethernet

Plug a standard network cable into the Ethernet port before powering on. tapbox scrolls the connection type and assigned IP address across the display at startup — for example `Eth 192.168.1.55`. This tells you where to find it on the network for OSC control.

Ethernet is always preferred over WiFi. If both are available, tapbox uses Ethernet.

#### DHCP, AutoIP fallback, and traps

With <img src="docs/menu_glyph_lan.png" alt="Lan." height="26" valign="middle"> left at the default <img src="docs/menu_value_auto.png" alt="Auto" height="26" valign="middle"> setting, Ethernet on tapbox is meant to work like a USB cable — plug it in and it just works. Most networks have a router that hands out an address automatically, and that's all tapbox needs.

Occasionally there's no router in the picture at all — for example, a cable run straight from tapbox to a CDJ player or a laptop's Ethernet port, with nothing else in between. There's nobody there to hand out an address, so tapbox assigns itself a temporary one starting with `169.254.` so it's still reachable. You don't need to do anything for this to happen.

> **Trap:** a `169.254.` address isn't guaranteed to be the final one. For about 2 minutes after it appears, tapbox keeps listening in case a proper router shows up late — if one does, tapbox switches to the address it offers and scrolls the new IP across the display, the same way it does at boot. If you jot down a `169.254.` address the moment it appears and hard-code it somewhere (an OSC destination, a saved shortcut) without noticing the display update a minute or two later, it may stop working.
>
> **Rule of thumb:** if the display shows a `169.254.` address, give it a couple of minutes to settle before writing it down. If you need an address that's guaranteed to never move, set <img src="docs/menu_glyph_lan.png" alt="Lan." height="26" valign="middle"> to <img src="docs/menu_value_stat.png" alt="Stat" height="26" valign="middle"> instead (see below).

The DHCP/AutoIP race only restarts if the Ethernet cable is unplugged and re-seated (or tapbox reboots) — so re-seating the cable is also a reasonable first step if tapbox ever seems stuck on a stale address.

### WiFi

tapbox can connect to your WiFi network and run Ableton Link over it. This is useful when your performance space does not have an Ethernet switch nearby, or when you need to sync with a device that is on WiFi and your router bridges multicast between the two interfaces.

**The ESP32 radio supports 2.4 GHz only.** If your router has separate 2.4 GHz and 5 GHz networks (often shown as two different SSIDs, or a combined band with a single SSID), make sure you use the 2.4 GHz SSID when entering credentials.

#### First-time WiFi setup

1. Boot tapbox without an Ethernet cable plugged in.
2. tapbox creates a WiFi network called **tapbox** and scrolls `AP 192.168.4.1` followed by an 8-digit PIN across the display — the PIN fills the whole display and pauses there for 6 seconds so you have time to read it.
3. Connect your phone or laptop to the **tapbox** network, using that PIN as the WiFi password.
4. Open **http://192.168.4.1** in your browser. It will ask for a username and password — enter `tapbox` and the same PIN.
5. Enter your WiFi network name (SSID) and password. The SSID field is case-sensitive — copy it exactly from your phone's WiFi list.
6. Tap **Save Network — tapbox will reboot**. The device saves the credentials and reboots, then connects to your network as a WiFi client and scrolls `StA` followed by the assigned IP address.

The PIN is derived from the device's own hardware, so it's fixed for the life of that unit and never needs to be written down — it follows the IP address on the display at every boot or reconnect, in any mode (Ethernet, WiFi, or AP), and is always available from the <img src="docs/menu_glyph_addr.png" alt="Addr" height="26" valign="middle"> item in the menu.

> **What the PIN is for:** it exists to stop someone at a gig or in an office from wandering up to tapbox and casually changing your settings — it is not intended as strong security. Anyone determined enough to take the firmware apart could work out how the PIN is derived. Don't rely on it to protect anything beyond "keep the unsuspecting public out."

#### Changing WiFi credentials later

Unplug the Ethernet cable (or boot without one). If the stored credentials fail, tapbox falls back to AP mode automatically — connect to the **tapbox** network using its PIN and open **http://192.168.4.1** to update them.

#### Automatic Ethernet / WiFi switching

tapbox switches interfaces automatically without a reboot:

- **Ethernet plugged in** → tapbox stops WiFi and switches Link to Ethernet.
- **Ethernet unplugged** → tapbox starts WiFi (connecting to stored credentials, or starting the access point if none are stored).

If WiFi credentials fail to connect, tapbox falls back to AP mode immediately so you can reconfigure without rebooting.

By default tapbox uses DHCP. If you need a fixed address, set <img src="docs/menu_glyph_lan.png" alt="Lan." height="26" valign="middle"> to <img src="docs/menu_value_stat.png" alt="Stat" height="26" valign="middle"> on the device and enter the IP/subnet/gateway on the web config page (see below) — this applies to whichever interface, Ethernet or WiFi, is actually active.

### Web Configuration Page

The config page is available at the device's IP address on port 80, from any browser over Ethernet or WiFi. Your browser will prompt for a username and password — enter `tapbox` and the 8-digit PIN that follows the IP address on the display (or via <img src="docs/menu_glyph_addr.png" alt="Addr" height="26" valign="middle"> in the menu). This applies every time, in every mode, since the page can change any setting on the device. A **Tap** button sits above the tabs and works from any of them — it's the same tap the physical button drives, useful for tapping tempo from a phone or laptop without reaching the device. The page below it is organized into four tabs — Network, Settings, and BPM tuning each have their own save button; the Log tab is view-only. Fields that don't apply to your current settings are greyed out rather than hidden, so the layout stays consistent.

![Network tab](docs/tapbox_web_config_network.png)

**Network** (WiFi SSID/password, network mode, static IP/subnet/gateway) — tap **Save Network — tapbox will reboot** to apply. The orange button is a reminder that a reboot follows. Network mode has three options — DHCP, Static, and Access Point — and static IP/subnet/gateway apply to whichever interface (Ethernet or WiFi) is active. The IP/Subnet/Gateway fields grey out unless Network Mode is Static.

![Settings tab](docs/tapbox_web_config_settings.png)

**Settings** (time signature, source, brightness) — tap **Save Settings** to apply immediately. No reboot occurs; the device updates live and the page returns with a confirmation link.

![BPM tuning tab](docs/tapbox_web_config_BPM_tuner.png)

**BPM tuning** — audio beat-detector parameters plus a live chart of the incoming signal, described in full under **Audio Tuning** below. The whole tab greys out (controls disabled, chart dimmed) whenever the Source isn't Mic or Line, since these settings only affect audio detection.

**Log** — a fourth, view-only tab: a live scrolling log of tap/arm/lock events, timestamped, useful for testing without a serial cable attached (e.g. watching tempo-tracking behavior while changing pitch/speed in DJ software). Only active while the tab is open in a browser — see `BEAT_DETECTION.md` for what's logged.

---

## The Settings Menu

Press the select button to open the menu. Press the tap button to move between items. Press the select button to enter edit mode for the selected item, then press the tap button to change the value (hold for fast auto-increment). Press select again to confirm and return to the menu.

![Menu mode example: Beat label with value 4](docs/display_menu_mode.png)

Each menu screen shows a 4-character label on the left, then the current value right-justified on the right — for example, <img src="docs/menu_glyph_beat.png" alt="Beat" height="26" valign="middle"> with a time signature of `4`.

To go back from an edit, hold the select button for one second. To exit the menu entirely, hold select or wait six seconds — tapbox returns to normal mode without saving the current edit. When you next open the menu it returns to the last item you were on.

---

### Beat — Time Signature

**What it does:** tells tapbox how many beats are in a bar.

The beat counter on the display counts from 1 up to this number, then loops back to 1. This keeps you visually oriented in the musical phrase.

**Available values:** 2, 3, 4, 5, 6, 7

**When to use it:** if you are playing a waltz in 3/4, set this to 3 so the counter runs 1–2–3 rather than counting to 4 and going out of sync with the phrase. For odd-time signatures like 7/8, set it to 7.

*Default: 4*

---

### Led — Display Brightness

**What it does:** adjusts how bright the display glows across four levels (1 – 4).

The display gives you a live preview as you change the value. In a dark venue, level 1 or 2 keeps it readable without becoming distracting. In a brightly lit studio, level 4 is easy to read from across the room.

*Default: 2*

---

### Src — Source

**What it does:** selects where tapbox gets its tempo. Four values:

- <img src="docs/menu_value_cdj.png" alt="Cdj" height="26" valign="middle"> — Pioneer Pro DJ Link (see below)
- <img src="docs/menu_value_aud.png" alt="Aud" height="26" valign="middle"> — audio beat detection from the microphone (you tap the downbeat)
- `LinE` — audio beat detection from the 3.5 mm line input (you tap the downbeat)
- <img src="docs/menu_value_tap.png" alt="tAP" height="26" valign="middle"> — manual tap tempo

The active source is shown by a bar on the display (top = CDJ, middle = Tap, bottom = Mic, top + bottom = Line). See the **Sync Sources** section earlier in this manual for how each one works in practice.

**About CDJ source:** when set to <img src="docs/menu_value_cdj.png" alt="Cdj" height="26" valign="middle">, tapbox listens on the same Ethernet switch as your CDJ players and reads their beat timing automatically. The active CDJ's BPM is fed directly into the Ableton Link session — all your Link peers follow the CDJ without any tapping. A `C` indicator confirms the lock. tapbox follows the lowest player number (1 → 2 → 3 → 4); if that player stops for more than two seconds it drops to the next. While a CDJ is actively driving the tempo, the tap button is ignored — the CDJ is in control. If no CDJ is present, CDJ source behaves like manual tap tempo.

> CDJ sync requires Ethernet, on the same wired switch as the players. Nothing is installed on the CDJs — tapbox listens passively.

*Default: Aud*

---

### Audio Tuning

These settings apply to both audio sources — Mic and Line — whichever is active. The beat detector runs an FFT/mel-filterbank onset detector feeding a dynamic-programming beat tracker (`BTrack`) — see `BEAT_DETECTION.md` for the full pipeline. Your tap is always ground truth: the anchor tempo it sets never moves except by re-tapping. While `BTrack` is confident *and* its estimate is within your allowed BPM range of that anchor, the tempo follows it directly (so it responds to a pitch ride within a couple of beats); when confidence drops — silence, a breakdown, a beat-free passage — or the estimate strays outside the allowed range, the tempo holds its last good value until a beat clears every check again.

What counts as "confident enough," how loud a sound has to be to count as a beat at all, and how far you trust audio sensing to refine your tap all vary with your source, your room, and how much you want the detector doing — so four parameters on the **web config page**'s BPM tuning tab control them directly. (Line-in is electrically clean — no room noise — so it typically needs no Level floor at all; the floor mainly earns its keep on the microphone.)

- **Level floor** (dB, off by default): an absolute loudness floor. Without it, a completely silent room can still produce confident "beats" from something as quiet as keyboard typing, because the detector measures *relative* spectral change, not loudness. Watch the **Level** readout under the chart while music plays and while the room is quiet, then set the floor between the two — the readout is smoothed, so it settles rather than jumping around, but at low listening volumes the gap between "room" and "music" can be narrow; a louder room gives you more margin.
- **Lock confidence** (0.00–1.00, default 0.30): how confident `BTrack` must be for a beat to keep the sync **lock** alive and keep the beat grid phase-aligned. Lower this if the "LOCKED" state keeps dropping out on a quiet or reverberant mic signal.
- **Move confidence** (0.00–1.00, default 0.60): how confident `BTrack` must be before a beat is allowed to actually **change** the tempo. Higher is more cautious — a beat-free passage or ambient noise won't be able to nudge the tempo unless it clears this bar. Lower it if the tempo isn't updating even though the **Audio BPM** readout looks correct.
- **BPM range** (±1–20, default ±6): how far the mic's estimate may drift from your tapped tempo and still be trusted to move it. A reading further than this from your tap is ignored outright, no matter how confident — the tempo just holds. This is the main dial for how much you trust the mic versus your own tap; lower it to keep tapbox close to what you tapped, raise it if you want the mic doing more of the work. Re-tap any time to re-center the window on a new tempo.

Above the chart, a live readout shows three numbers — **Tap BPM** (what you last tapped), **Audio BPM** (the mic's current confidence-gated estimate), and **Link BPM** (the tempo actually driving the session) — plus the tracking state (idle / searching / locked). The chart itself plots the confidence trace against the lock/move lines (which move as you drag the sliders), with a tick for every beat — bright green if it cleared both the move-confidence bar and the BPM range and moved the tempo, dim grey if it only held the lock, red if a predicted beat was rejected by the level floor. A cluster of bright ticks with no music playing means Move confidence needs to come up, or Level floor needs to come up to reject the noise outright. Each slider applies immediately, live — there's no separate save step while tuning.

For a real-time view of tap/lock events without a serial cable — useful when testing tempo tracking against something like a DJ pitch fader — see the **Log** tab described above.

Below the chart, a rolling 10-second count shows **beats seen vs beats that moved the tempo**. Seen-but-didn't-move beats cleared the level floor and lock confidence but not move confidence (or fell outside the BPM range) — a high seen count with a low moved count usually means Move confidence or BPM range is set too tight for the current room, or the passage is genuinely ambiguous (a breakdown, sparse content).

The defaults work for typical four-on-the-floor material. For the full explanation of what each does and how to dial them in, see `BEAT_DETECTION.md`.

#### Why the tempo never flickers — and never drifts

There's a tension at the heart of every beat tracker. Audio analysis naturally jitters by a fraction of a BPM from beat to beat, so if you publish every reading, the tempo flickers across your whole rig — displays dance, devices chase noise. But if you smooth or ignore the small stuff, tiny errors should slowly *accumulate*: hold 126.3 while the music is really at 126.0 and the beat grid slides a little further off with every beat, until "beat 1" is audibly wrong.

tapbox resolves this with two mechanisms that cover for each other:

- **A steady tempo number.** Changes smaller than 0.4 BPM are ignored, so the BPM published to the Link session is rock solid — what you see on the display is what every peer sees, and none of them waste effort chasing estimation noise.
- **A self-correcting beat grid.** While locked, every detected beat gently pulls the beat grid a small step toward where the music actually is. Drift from a tiny tempo mismatch adds a *fixed* amount per beat, but the correction grows with the gap — so instead of sliding away, the grid settles at a constant offset of a few milliseconds, inaudible and bounded, forever.

The result is the best of both: a BPM number stable enough to trust on a big screen, and a beat grid that stays glued to the actual music indefinitely. Your downbeat is safe throughout — corrections always target the *nearest* beat, never re-guessing which beat is "1". That decision stays yours, made with your tap.

---

### Lan. — Network Mode

**What it does:** switches network addressing between automatic (DHCP), manual (static), and forced Access Point mode — applies to whichever interface, Ethernet or WiFi, is currently active.

- **Auto** — your router assigns tapbox an IP address automatically every time it boots.
- **Stat** — tapbox uses a fixed IP address, entered on the web config page (see below).
- **AP** — tapbox starts a wifi network called `tapbox` with IP address `192.168.4.1`. Useful if there's no other way to reach a browser, or if your normal WiFi network associates but won't actually pass traffic to the device (client isolation, captive portal) — this menu item lets you force your way back to a working config page without needing the network to cooperate first.

**When to use static:** if you send OSC commands from a DAW or control surface with a hard-coded destination address, a static IP ensures that address never changes between reboots.

When you confirm a change to this setting, tapbox displays `bOOt` and restarts automatically.

The static IP, subnet mask, and gateway themselves are entered on the **web config page** — see **Web Configuration Page** above — and only take effect when <img src="docs/menu_glyph_lan.png" alt="Lan." height="26" valign="middle"> is set to **Stat**. Factory defaults: **192.168.1.200** / **255.255.255.0** / **192.168.1.1**.

*Default: Auto*

---

### vEr — Firmware Version

**What it does:** shows the firmware version currently running on your tapbox as `major.minor.patch` — for example, version 1.3.0 appears as `1.3.0`. Read-only.

---

### done — Exit Menu

Returns to normal mode immediately.

---

## System Functions

Both system functions are activated from **within the menu** by holding both buttons at the same time. Open the menu first (select short press), then hold both buttons. You do not need to power-cycle the device.

---

### OTA Firmware Update

Open the menu, then hold both the **tap button** and the **select button** for **3 seconds**. The display shows <img src="docs/confirm_ota_hold.png" alt="UPd.----" height="26" valign="middle">. Release both buttons — the display changes to <img src="docs/confirm_ota_confirm.png" alt="UPd Yes" height="26" valign="middle">.

Press **select** to confirm. tapbox saves a pending-update flag to memory, erases OTA data if necessary to return to the factory slot, and reboots. On the next boot, as soon as it obtains a network connection (Ethernet or WiFi), it downloads and installs the latest firmware automatically. The display shows `UPd.` followed by a progress percentage. When the percentage reaches 100, it shows <img src="docs/confirm_ota_done.png" alt="UPd. done" height="26" valign="middle"> and reboots into the new firmware.

If the download fails or the server is unreachable, the display shows <img src="docs/confirm_ota_err.png" alt="UPd.   Er" height="26" valign="middle"> and tapbox continues to boot normally. All settings are preserved across updates; only the firmware changes.

To cancel: press **tap**, hold **select** for 1 second, or wait 6 seconds — tapbox returns to normal without scheduling an update.

---

### Factory Reset

Open the menu, then hold both the **tap button** and the **select button** for **8 seconds**. You will see <img src="docs/confirm_ota_hold.png" alt="UPd.----" height="26" valign="middle"> at 3 seconds and then <img src="docs/confirm_reset_confirm.png" alt="rSet Yes" height="26" valign="middle"> at 8 seconds. Release the buttons.

Press **select** to confirm. tapbox resets all settings to factory defaults — source to Mic, time signature to 4, brightness to 2, network to Auto, static address to 192.168.1.200 / 255.255.255.0 / 192.168.1.1 — clears any stored WiFi SSID and password, and reboots.

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
| `/nudge <ms>` | Shift the beat phase by `<ms>` milliseconds — positive nudges forward, negative nudges back. Omit the argument for a default 20ms nudge forward. |
| `/downbeat` | Reset the downbeat to this exact moment |

**A few ways to put this to use:**

- Map `/tap` to a pad on a MIDI controller via your DAW so the whole band can tap tempo from the stage.
- Send `/bpm 128` from an Ableton Live clip to snap the tempo to a specific value at the start of a track.
- Use `/downbeat` at the top of a new section to re-align the beat grid after a break.
- Assign `/nudge 40` and `/nudge -40` to fader buttons on a mixing desk for a decisive push/pull, or `/nudge 5` / `/nudge -5` for fine phase correction.

**Nudge notes:** a nudge shifts the shared Link timeline itself, so every connected app sees its beat grid move — it's not local to tapbox. With a **Mic or Line source locked**, the beat detector owns phase alignment: its phase-lock will pull the grid back onto the detected beats within a few beats, undoing the nudge. Nudge is therefore most useful with the CDJ and Tap sources; with Mic or Line, re-tap the downbeat instead.

---

## App Integration Guides

tapbox works with any Ableton Link or OSC-capable application. Setup guides for specific apps live in their own documents:

- **MadMapper** — see [`MADMAPPER.md`](MADMAPPER.md) for Ableton Link and OSC setup.

More app guides will be added over time.

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
tapbox could not connect to Ethernet within the boot timeout — 3 seconds if no cable is detected, or up to 15 seconds if a cable is present and using DHCP, to allow time for a lease or an AutoIP self-assigned address. Check the cable. If no cable is connected, tapbox starts WiFi — the display will scroll the IP once a connection is established, or `AP 192.168.4.1` if it falls back to access point mode.

**tapbox's IP address changed on its own a minute or two after boot.**  
This is the DHCP/AutoIP handoff, not a fault — see [DHCP, AutoIP fallback, and traps](#dhcp-autoip-fallback-and-traps). tapbox self-assigned a `169.254.x.x` address because no DHCP server answered right away, then a real lease arrived within the 2-minute grace window and took over — the display scrolled the new address when it happened. If you need the address to never move, set a static IP instead.

**tapbox shows dashes on the display after boot.**  
It is connected to the network but has not received any Link peers yet. This is normal — the display fills in once another Link device joins the session.

**WiFi connects but peers show 0.**  
Ensure the other device (e.g. MadMapper on a laptop) is on the same network. Link uses UDP multicast — some routers do not bridge multicast between WiFi and Ethernet. If your PC is on Ethernet and tapbox is on WiFi, try connecting tapbox via Ethernet instead.

**tapbox cannot find my WiFi network (or connects then immediately disconnects).**  
Check that you entered the SSID exactly as it appears on your phone — SSIDs are case-sensitive. Also confirm the network is 2.4 GHz; the ESP32 cannot connect to 5 GHz networks. If credentials fail, tapbox falls back to AP mode automatically — connect to the **tapbox** network using its PIN and open **http://192.168.4.1** to correct them.

**My OSC messages are not reaching tapbox.**  
Confirm the IP address on the display at next boot and update your OSC destination. If using a static IP, verify the address, subnet, and gateway are correct.

**I set a static IP and now tapbox is unreachable.**  
Use the factory reset (open the menu, hold both buttons 8 s, then confirm with select) to return to Auto DHCP. The display will show the assigned address at the next boot.

**The display is very dim after a restart.**
tapbox detected a brownout (power dip) during the previous session and has automatically set brightness to level 1 to protect against a repeat. You can raise it in the menu under <img src="docs/menu_glyph_led.png" alt="Led" height="26" valign="middle"> and save. If it keeps happening, check your power supply.

**The tempo drifts slightly after many taps.**  
tapbox calculates BPM as an average across all taps in the session. Small variations in tap timing do shift the average, though the effect becomes smaller with each additional tap. For a locked-in tempo, tap steadily for 8 or more beats, then stop and let Link hold the tempo.
