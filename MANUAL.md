# tapbox User Manual

---

## What Is tapbox?

tapbox is a compact, dedicated tempo controller for musicians and live performers who use Ableton Link. It sits on your desk or in your rack, connects to your network via Ethernet, and keeps every device in your setup locked to the same beat — without you having to touch a laptop.

The idea is simple: tap the button a few times to set your tempo, and tapbox broadcasts it to every Link-enabled device on the network. Your Ableton Live session, your iOS apps, your hardware synths with Link support — they all lock in instantly. From that point on you can nudge the tempo up or down with the encoder knob, or send commands from a phone or tablet over OSC if you prefer to stay hands-free.

tapbox is always on, always listening, and always in sync.

---

## Getting to Know Your tapbox

### The Display

tapbox uses an 8-digit display that shows you everything you need at a glance. In normal operation it looks like this:

```
  1 2 0 . 0   3   2
```

Reading left to right:

- **120.0** — the current tempo in BPM, updated in real time as the Link session adjusts
- **3** — which beat of the bar you are on right now (this advances with the music)
- **2** — how many other Link peers are connected to the session

On startup, tapbox joins the Link session immediately at 120 BPM. You can tap a new tempo whenever you are ready, or simply start adjusting from 120 using the encoder knob.

### The Controls

**Tap button** — the main button. Tap it in time with your music to set the tempo. Also exits the menu immediately if you need to get back to playing in a hurry.

**Encoder knob** — turn it to nudge the tempo up or down while playing. Press it to open the settings menu.

---

## Setting the Tempo

Tap the button four times in rhythm and tapbox locks in the BPM and announces it to the Link session. After the fourth tap, the display lights up with the tempo and the beat counter starts running.

You do not have to be perfectly precise. tapbox averages the timing across all your taps, so the more you tap, the more accurate the reading becomes. Think of it like a conductor giving a clear downbeat — a few firm, confident taps is all it takes.

After you are locked in, any further taps continue to refine the tempo. If you stop tapping for two seconds, tapbox considers the session established and waits quietly. If you tap again after that pause, it treats it as the start of a new tempo reading.

### Adjusting Tempo on the Fly

Once you are live, turning the encoder knob trims the tempo up or down in small steps. This is ideal for those moments when the energy of the room calls for pushing the track just a touch faster, or pulling it back slightly to let a breakdown breathe. The change is immediate and smooth, and every connected device follows along.

The size of each step is configurable in the menu — see **BPM Step** below.

---

## Connecting to Your Network

tapbox connects via the Ethernet port on the back. Plug in a standard network cable before powering on.

On startup, tapbox displays the IP address it has been assigned across two brief screens — first the first two octets, then the last two. This tells you where to find it on the network if you want to send OSC commands.

By default tapbox uses DHCP, meaning your router assigns it an address automatically. If you need a fixed, predictable address — for example, to lock in OSC routing from a DAW template that always sends to the same destination — you can configure a static IP in the menu.

---

## The Settings Menu

Press the encoder knob to open the menu. Turn the knob to move between settings, then press again to edit the selected one. Turn to change the value, press to confirm. If you want to leave without saving, just press the tap button or wait six seconds — tapbox will return to normal mode on its own.

---

### Beat — Time Signature

**What it does:** tells tapbox how many beats are in a bar.

The beat counter on the display counts from 1 up to this number, then loops back to 1. This keeps you visually oriented in the musical phrase, which is especially useful on stage when you cannot always hear every instrument clearly.

**Available values:** 2, 3, 4, 5, 6, 7

**When to use it:** if you are playing a waltz or a piece in 3/4, set this to 3 so the counter runs 1–2–3–1–2–3 rather than counting to 4 and going out of sync with the phrase. For odd-time signatures like 7/8, set it to 7.

*Default: 4*

---

### StEP — BPM Encoder Step

**What it does:** controls how much the tempo changes with each click of the encoder knob.

**Available values:** 0.1, 0.2, 0.5, 1.0 BPM per click

**When to use it:** in a studio session where you are hunting for exactly the right feel, 0.1 BPM steps let you dial in tempo with fine precision. In a live performance where you need to shift tempo quickly and decisively between sections, switching to 1.0 BPM steps means fewer turns of the knob to get where you are going. Choose the value that matches how you work.

*Default: 0.1*

---

### nudg — Nudge Amount

**What it does:** sets how far tapbox shifts the phase of the beat when you send a nudge command over OSC.

Nudging does not change the tempo — it shifts the timing of the beat forward or backward by a small amount. This is useful when two Link sessions have drifted slightly out of phase alignment and you want to snap them back together without changing the BPM.

**Available values:** 5, 10, 20, 50, 100 ms

**When to use it:** if you are running a DJ set and you notice the downbeat of your tapbox session is landing just slightly ahead of the kick drum in Ableton, a quick nudge back by 10 or 20 ms can re-align them without anyone on the dance floor noticing. Use a smaller value for fine touch-ups and a larger value when you need a more noticeable correction.

*Default: 20 ms*

---

### brit — Display Brightness

**What it does:** adjusts how bright the display glows, from a dim 1 up to a full-intensity 15.

The display gives you a live preview as you turn the knob, so you can judge the right level for your environment without having to confirm and then go back in.

**When to use it:** in a dark venue, a very bright display can be distracting to the audience or wash out in photographs. Turning it down to 3 or 4 keeps it readable for you without becoming a light show of its own. In a brightly lit studio, cranking it up to 15 makes it easy to read from across the room.

*Default: 7*

---

### nEt — Network Mode

**What it does:** switches between automatic (DHCP) and manual (static) IP addressing.

- **dhcP** — your router assigns tapbox an IP address automatically every time it boots. This is the easiest option and works in most setups.
- **StAt** — tapbox uses a fixed IP address that you configure yourself. The address never changes between reboots.

**When to use static:** if you send OSC commands to tapbox from a DAW or control surface, you may have the destination address hard-coded in your template. DHCP can occasionally assign a different address after a power cycle, which would break those connections. Setting a static IP means your routing always works, no reconfiguration needed.

When you confirm a change to this setting, tapbox displays `bOOt` and restarts automatically to apply the new network configuration. No further action is needed.

*Default: dhcP*

---

### IP 1–4, Sn 1–4, Gt 1–4 — Static Network Address

These three groups of settings only appear in the menu when network mode is set to **StAt**.

- **IP 1–4** — the static IP address you want tapbox to use (e.g. 192 . 168 . 1 . 50)
- **Sn 1–4** — the subnet mask (e.g. 255 . 255 . 255 . 0)
- **Gt 1–4** — the gateway address, usually your router (e.g. 192 . 168 . 1 . 1)

Each group has four items, one for each part of the address. Turn the encoder to set each octet between 0 and 255, then press to confirm before moving to the next.

**Tip:** if you are unsure what values to use, check the address currently assigned via DHCP (shown on the display at boot), and use that as a starting point — just make sure the last number is not one your router might assign to another device.

---

### rSEt — Factory Reset

**What it does:** returns all settings to their original defaults and clears any network configuration.

When you select rSEt and press the encoder, the display asks `rSEt SurE` to make sure you did not land here by accident. Press the encoder once more to confirm, and tapbox resets everything: tempo step back to 0.1, brightness back to 7, network back to DHCP, all address fields cleared.

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

---

## Troubleshooting

**The IP address does not appear at boot.**
tapbox could not connect to the network within 10 seconds. Check that the Ethernet cable is plugged in securely and that the other end is connected to a live switch or router.

**My OSC messages are not reaching tapbox.**
Confirm the IP address on the display at next boot and update your OSC destination. If you are using a static IP, verify that the address, subnet, and gateway are all correct and that the address is not in use by another device.

**I set a static IP and now tapbox is unreachable.**
Use the factory reset (rSEt in the menu) to return to DHCP. The display will show the new DHCP address at the next boot.

**The tempo drifts slightly after many taps.**
tapbox calculates BPM as an average across all taps in the session. Small variations in tap timing do shift the average, though the effect becomes smaller with each additional tap. For a locked-in tempo, tap steadily for 8 or more beats, then stop tapping and let the encoder handle any fine adjustments.
