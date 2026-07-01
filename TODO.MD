Done (v1.9.0):
- WiFi static IP support added (was Ethernet-only) — Lan./IP/Sub./Hub. now apply to whichever interface is active
- On-device menu trimmed to: Beat, Led, node, Lan., Addr, rSet, vEr, done
- IP/Sub./Hub. octets and the four mic-tuning params (uind/SLEu/thr/gAte) moved to web config page only
- Web config: added Access Point option to network mode, added mic-tuning fields
- "nud" removed entirely — menu item, web field, and NVS key all gone
- OSC: /nudge_up and /nudge_down replaced with a single /nudge <ms> (signed, +/-;
  no argument -> default 20ms)
- Default sync mode switched from CDJ to Audio (Aud)
- Fixed: long-pressing select from edit mode now backs out to menu nav (discarding
  the in-progress edit) instead of exiting the whole menu
- Mode bar moved from before the beat counter to the empty digit behind it

Next up:
- AP password: 8-digit numeric, obfuscated-from-MAC (not raw). Same PIN also gates
  HTTP Basic Auth on the whole web config page (currently wide open to anyone on
  the network) in both AP and normal network mode.
- Live audio-tuning UI: WebSocket stream of energy/baseline/threshold/gate values,
  rendered as a live strip chart on the web page, with sliders for the four mic
  params instead of number fields. Mobile-friendly layout.

Bugs:
- tapbox.local doesn't resolve — there's no actual mDNS responder in the firmware,
  just the text "tapbox.local" in the post-save HTML. esp_netif_set_hostname() only
  sets the DHCP hostname option, it doesn't make .local resolution work. Needs
  ESP-IDF's mdns component (mdns_init() + mdns_hostname_set("tapbox") + advertise
  the HTTP service) on whichever interface is active.

Cosmetic (parked):
- "uind" label is a rough abbreviation of "window" — consider a clearer label
- display.jpg needs retaking — the mode bar moved position on the physical layout
- MANUAL.md cleanup pass once the above lands
