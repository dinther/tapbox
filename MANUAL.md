# tapbox User Manual

---

## What Is tapbox?

tapbox is a compact, dedicated tempo controller for musicians and live performers who use Ableton Link. It sits on your desk or in your rack, connects to your network via Ethernet, and keeps every device in your setup locked to the same beat — without you having to touch a laptop.

The idea is simple: tap the button on the beat to set your tempo, and tapbox broadcasts it to every Link-enabled device on the network. Your Ableton Live session, Resolume, MadMapper, LaserOS, your iOS apps, your hardware synths with Link support — they all lock in instantly. From that point on you can nudge the tempo up or down with the encoder knob, or send commands from a phone or tablet over OSC if you prefer to stay hands-free.

tapbox is always on, always listening, and always in sync.

---

## Getting to Know Your tapbox

### The Display

tapbox uses an 8-digit display that shows you everything you need at a glance. In normal operation it looks like this:

```
  1 2 0 . 0   3   2
```

Reading left to right:

- **120.0** — the current tempo in BPM, updated in real time when members in the Link session adjust
- **3** — which beat of the bar you are on right now (this advances with the music)
- **2** — how many other Link peers are connected to the session

On startup, tapbox starts with the tempo of the existing link session if one exists otherwise it starts with the default tempo of 120 BPM. You can tap a new tempo whenever you are ready, or simply start adjusting the tempo using the encoder knob.

### The Controls

**Tap button** — the main button. Tap it in time with your music to set the tempo. A single tap will instantly sync the link session to the downbeat. (first beat in a bar). Tap also exits the menu immediately if you need to get back to playing in a hurry.

**Encoder knob** — turn it to nudge the tempo up or down while playing. Press it to open the settings menu.

---

## Setting the Tempo

Tap the button in rhythm. The downbeat for the link session is immediately defined by the first tap. It takes three more taps before tapbox starts to adjust the tempo of the link session. The current tempo and beat progress is displayed live at all times.

You do not have to be perfectly precise. tapbox averages the timing across all your taps, so the more taps in sequence, the more accurate the reading becomes.

After you are locked in, any further taps continue to refine the tempo. tapbox considers the session established when you stop tapping for two seconds. Simply start tapping again to define a new tempo.

### Adjusting Tempo on the Fly

Turning the encoder knob adjusts the tempo up or down in small steps. This is ideal when you require a gradual tempo change called for by the music or when you slowly want to catch up with the downbeat that drifted away. The change is subtle and smooth, and every connected device follows along.

The size of each step depends on the **Acc.** setting in the menu.

---

## Connecting to Your Network

tapbox connects via the Ethernet port on the back. Plug in a standard network cable before powering on.

On startup, tapbox displays the IP address it has been assigned across two brief screens — first the first two octets, then the last two. This tells you where to find it on the network if you want to send OSC commands.

By default tapbox uses DHCP, meaning your router assigns it an address automatically. If you need a fixed, predictable address — for example, to lock in OSC routing from a DAW template that always sends to the same destination — you can configure a static IP, subnet and gateway in the menu.

---

## The Settings Menu

Press the encoder knob to open the menu. Turn the knob to move between settings, then press again to edit the selected one. Turn to change the value, press to confirm. Press the encoder once more to return to the menu and continue navigating.

Some settings (IP address, subnet, gateway) open a sub-menu with individual octets. Navigate and edit them the same way, then select **done** and press to return to the main menu.

If you want to leave at any point without saving, just press the tap button or wait six seconds — tapbox will return to normal mode on its own. When you next open the menu, it will return to the last item you were on rather than starting at the top.

---

### Beat — Time Signature

**What it does:** tells tapbox how many beats are in a bar.

The beat counter on the display counts from 1 up to this number, then loops back to 1. This keeps you visually oriented in the musical phrase, which is especially useful on stage when you cannot always hear every instrument clearly.

**Available values:** 2, 3, 4, 5, 6, 7

**When to use it:** if you are playing a waltz or a piece in 3/4, set this to 3 so the counter runs 1–2–3–1–2–3 rather than counting to 4 and going out of sync with the phrase. For odd-time signatures like 7/8, set it to 7.

*Default: 4*

---

### Acc. — Accuracy

**What it does:** sets the control sensitivity for both the encoder knob and OSC nudge commands in a single step.

| Setting | Encoder step | OSC nudge |
|---------|-------------|-----------|
| **Lo** | 1.0 BPM per click | 50 ms |
| **Std** | 0.5 BPM per click | 20 ms |
| **Hi** | 0.1 BPM per click | 5 ms |

**Lo** is best for live performance where you need to shift tempo decisively between sections — fewer turns of the knob to get where you are going, and a nudge big enough to feel when re-aligning two sources by ear.

**Std** is the everyday setting. Half a BPM per click gives you enough resolution to dial in a feel without overshooting, and a 20 ms nudge handles most phase drift situations cleanly.

**Hi** is for studio work where you are hunting for an exact tempo or need surgical phase correction — 0.1 BPM steps and 5 ms nudges let you inch toward the target without jumping past it.

*Default: Std*

---

### Led — Display Brightness

**What it does:** adjusts how bright the display glows, from a dim 1 up to a full-intensity 15.

The display gives you a live preview as you turn the knob, so you can judge the right level for your environment without having to confirm and then go back in.

**When to use it:** in a dark venue, a very bright display can be distracting to the audience or wash out in photographs. Turning it down to 3 or 4 keeps it readable for you without becoming a light show of its own. In a brightly lit studio, cranking it up to 15 makes it easy to read from across the room.

*Default: 7*

---

### Lan. — Network Mode

**What it does:** switches between automatic (DHCP) and manual (static) IP addressing.

- **Auto** — your router assigns tapbox an IP address automatically every time it boots. This is the easiest option and works in most setups.
- **Stat** — tapbox uses a fixed IP address that you configure yourself. The address never changes between reboots.

**When to use static:** if you send OSC commands to tapbox from a DAW or control surface, you may have the destination address hard-coded in your template. DHCP can occasionally assign a different address after a power cycle, which would break those connections. Setting a static IP means your routing always works, no reconfiguration needed.

When you confirm a change to this setting, tapbox displays `bOOt` and restarts automatically to apply the new network configuration. No further action is needed.

*Default: Auto*

---

### IP, Sub., Hub. — Static Network Address

These three settings only appear in the menu when network mode is set to **Stat**. Each one opens a sub-menu with four octets labelled **Oct1** through **Oct4**.

- **IP** — the static IP address you want tapbox to use (e.g. 192 . 168 . 1 . 50)
- **Sub.** — the subnet mask (e.g. 255 . 255 . 255 . 0)
- **Hub.** — the gateway address, usually your router (e.g. 192 . 168 . 1 . 1)

Press the encoder on any of these items to enter the sub-menu. Turn to move between Oct1–Oct4, press to edit the selected octet, turn to change the value (0–255), then press again to confirm. Navigate to **done** and press to return to the main menu, or press the tap button at any time to exit straight to normal mode.

The factory defaults are **192.168.1.200** for the IP, **255.255.255.0** for the subnet, and **192.168.1.1** for the gateway — a sensible starting point for most home and studio networks. Adjust as needed for your setup.

---

### UPd. — Firmware Update

**What it does:** downloads the latest firmware from the internet and installs it automatically.

When you select `UPd.` and press the encoder, tapbox connects to GitHub over Ethernet and downloads the latest release. The display shows a percentage as the download progresses. When it reaches 100, tapbox shows `donE` and reboots into the new firmware — the whole process takes around 30 seconds on a typical network connection.

If something goes wrong (no network, server unreachable, download error) the display shows `Er` for a few seconds and then returns you to the menu without making any changes.

**Requirements:** tapbox must be connected to the internet via Ethernet. The update uses HTTPS so there are no plain-text concerns on your local network.

**After an update:** all your settings — tempo history, brightness, network configuration — are preserved. Only the firmware changes.

*This item does nothing if the Ethernet cable is unplugged.*

---

### vEr — Firmware Version

**What it does:** shows the firmware version currently running on your tapbox.

The display shows the version number as `major.minor.patch` — for example, version 1.1.0 appears as `1.1.0`. This is read-only; pressing the encoder simply resets the menu timeout.

---

### bAt — Battery Level

**What it does:** shows the estimated state of charge of the connected battery as a percentage from 0 to 100.

This item is only meaningful if your tapbox has a battery connected via the optional voltage divider on IO4. If no voltage divider is fitted the reading will be unreliable and can be ignored.

The reading updates smoothly and is filtered to avoid jitter, so it changes gradually rather than jumping between values. It is a guide rather than a precision instrument — expect accuracy of roughly ±10%.

*This item is read-only. Requires the IO4 voltage divider hardware.*

---

### rSEt — Factory Reset

**What it does:** returns all settings to their original defaults and clears any network configuration.

When you select rSEt and press the encoder, the display asks `rSEt SurE` to make sure you did not land here by accident. Press the encoder once more to confirm, and tapbox resets everything: accuracy back to Std, brightness back to 7, network back to Auto, static address restored to 192.168.1.200 / 255.255.255.0 / 192.168.1.1.

**When to use it:** if you have been experimenting with static IP settings and gotten yourself into a state where the device will not connect, a factory reset is the quickest way back to a working configuration.

---

## OSC Control

tapbox listens for OSC messages on **UDP port 8000**. This lets you control it from Ableton Live, TouchOSC, Lemur, Max/MSP, or any other software that can send OSC over the network.

Send your messages to the IP address shown on the display at boot.

| Command | What it does |
|---------|-------------|
| `/tap` | Same as pressing the tap button |
| `/bpm <value>` | Set the tempo to a specific BPM directly |
| `/signature <value>` | Change the time signature (2 through 7) |
| `/nudge_up` | Shift the beat phase forward by the nudge amount |
| `/nudge_down` | Shift the beat phase backward by the nudge amount |
| `/downbeat` | Reset the downbeat to this exact moment |

**A few ways to put this to use:**

- Map `/tap` to a pad on an MIDI controller via your DAW so the whole band can tap tempo from the stage without touching the tapbox unit.
- Send `/bpm 128` from an Ableton Live clip to snap the tempo to a specific value at the start of a track.
- Use `/downbeat` at the top of a new section to re-align the beat grid when coming in after a break.
- Assign `/nudge_up` and `/nudge_down` to fader buttons on a mixing desk to subtly push and pull the phase against another source, like a DJ mixer being blended in.

---

## Tips and Tricks

**Tapping in from scratch:** for the most accurate reading, tap along with a steady source — a click track, a drum loop, or even a song playing in headphones. Four clean taps is all you need to go live.

**The tap button exits the menu instantly.** If something happens on stage and you need to tap a new tempo, you do not have to navigate out of the menu first. Just tap — tapbox responds immediately and returns to normal mode.

**Brightness at gigs:** a useful habit is to set the brightness at soundcheck under the actual stage lighting conditions, then save it. What looks comfortable in daylight may be too bright or too dim under stage wash.

**Using a static IP with OSC:** if you use tapbox regularly in the same studio or live rig, setting a static IP once means your OSC routing just works every time. Five minutes of setup saves you checking the IP address at every session.

**Keeping firmware up to date:** when a new release is available, navigate to `UPd.` in the menu and press the encoder while tapbox is connected to the internet. The update is automatic and takes about 30 seconds. Your settings are not affected.

---

## Troubleshooting

**The IP address does not appear at boot.**
tapbox could not connect to the network within 10 seconds. Check that the Ethernet cable is plugged in securely and that the other end is connected to a live switch or router.

**My OSC messages are not reaching tapbox.**
Confirm the IP address on the display at next boot and update your OSC destination. If you are using a static IP, verify that the address, subnet, and gateway are all correct and that the address is not in use by another device.

**I set a static IP and now tapbox is unreachable.**
Use the factory reset (rSEt in the menu) to return to Auto. The display will show the assigned address at the next boot.

**The tempo drifts slightly after many taps.**
tapbox calculates BPM as an average across all taps in the session. Small variations in tap timing do shift the average, though the effect becomes smaller with each additional tap. For a locked-in tempo, tap steadily for 8 or more beats, then stop tapping and let the encoder handle any fine adjustments.
