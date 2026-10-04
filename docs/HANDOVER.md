# Handover - Cage Lock (2026-10-04, end of session)

Start here. Plan details: [REMOTE_PLAN.md](REMOTE_PLAN.md). Wiring: [wiring.html](wiring.html).
Firmware reference: header comment at the top of `cage_lock/cage_lock.ino`.

## Where things live

- GitHub: `jp-jackson/cage-lock` (renamed from doorlatch). All work is on branch
  `claude/epic-cori-u8asmn`; **no PR, nothing merged to main yet**.
- JP's PC (Windows): repo at `~\Documents\doorlatch`, board on **COM12**, local Claude Code
  does the compile/flash with arduino-cli (FQBN `esp8266:esp8266:nodemcuv2`).
- Base44 app: `polkproduction.base44.app` (Elite plan). Nothing cage-related built there yet.
- Cloudflare: JP owns a domain there. Tunnel not set up yet.
- Raspberry Pi: JP has one for the shop (model not confirmed; not the Home Assistant Pi).

## Status

### Working and confirmed on the board
- OLED screens, door switch (D7), chain feedback (D6), on-board LED = door.
- Relay clicks (D5). **12V lock not wired yet.**
- WiFi + local web page/API, setup hotspot "CageLock" (portal page confirmed working after
  the STA-retry fix, commit 62500a2).

### Pushed but NOT yet compiled/flashed (only syntax-checked with stubs)
Everything after 62500a2, roughly:
- Event log in flash + NTP time + `/log` + Configure WiFi button + WiFi rating on the page
- for/by names, door open duration and attribution
- WiFi screen only when signal is weak (address once a minute otherwise)
- `cmd`/`ts`/JSON body/replay protection/`boot_id` on `/unlock`

**First thing tomorrow:** have local Claude run
`git pull, compile and upload cage_lock/, then open the serial monitor on COM12`
and fix any real compile errors (the stubs can't catch everything, e.g. library API details).
If the compile complains about the filesystem, pick board option Flash Size 4MB (FS:2MB).

### JP's local `cage_lock/secrets.h` (gitignored, not in repo) - check it
```
#define WIFI_SSID "IOT"
#define WIFI_PASS "wrongpass"          <-- left wrong from the hotspot test; set the real one back
#define API_KEY   "..."                (lock API key)
#define AP_PASS   "nopassword"         (setup hotspot password; remove any duplicate AP_PASS line)
```
The lock has IOT saved from the setup portal, so it connects anyway; the wrong password only
matters after `wifireset`.

Local Claude also had an uncommitted buzzer frequency-sweep change in `tests/buzzer_test` on
the PC; commit or discard it before pulling if git complains.

### Libraries (arduino-cli)
Adafruit SSD1306, Adafruit GFX Library, WiFiManager (tzapu). LittleFS/time are in the ESP8266
core. PubSubClient only for the parked `tests/mqtt_test`. QRCode no longer used.

## Next steps (in order)

1. Compile/flash current firmware and test: page shows WiFi rating, "Your name" box, Show log,
   unlock logs `for`/`by`, door close logs `open_s`. Fix WIFI_PASS in secrets.h.
2. Set up the Pi at the shop + Cloudflare Tunnel + Access (steps in REMOTE_PLAN.md). Test with curl.
3. Claude writes the **Node-RED flow** (importable JSON) for the Pi: poll `/log?since`,
   persist `seq`, HMAC-sign, POST to `cageDeviceSync`; heartbeat every 15 min and on change.
4. Base44 builds entities -> `cageDeviceSync` -> secrets -> `cageUnlock` -> status page ->
   EventLog mirroring (contracts in REMOTE_PLAN.md). Paste Base44's answers back to Claude.
5. Wire the 12V lock: relay **COM + NO only (never NC)**, 1N4007 across the lock, confirm the
   lock is fail-secure. Test unlock end-to-end.

## Open items / decisions

- **Human hostname (`cage.<domain>`) should be status-only** so every remote unlock goes through
  Base44's audit trail. Options: don't share the lock API key with people, or add a
  "status-only when reached through the tunnel" mode to the firmware. Not decided.
- Pi model and Cloudflare domain name not yet given to Claude.
- Buzzer is too quiet: needs an NPN transistor driver from 5V (2N2222/S8050, 1k base resistor).
- `tests/reed_test` folder still has the old name (it's the door switch test); rename if wanted.
- Physical fallback: keep a key for the cage.

## How to resume with Claude

Paste: "Read docs/HANDOVER.md and docs/REMOTE_PLAN.md in jp-jackson/cage-lock (branch
claude/epic-cori-u8asmn) and continue from Next steps." Local Claude on the PC should
`git pull` first.
