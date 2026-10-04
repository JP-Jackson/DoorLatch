# Cage Lock

ESP8266 (HiLetgo NodeMCU, CP2102) Cage Lock controller: 12V pulse-type cabinet lock via relay, OLED status, buzzer, door switch and lock feedback switch.

Wiring diagram + schematic: [`docs/wiring.html`](docs/wiring.html) (open in a browser).

## Pin map

| Function | NodeMCU | GPIO | Mode | Notes |
|---|---|---|---|---|
| Relay IN1 | D5 | 14 | OUTPUT | Active LOW. Write HIGH before `pinMode` so it doesn't click at boot. Pulse 500 ms max, never hold on. |
| Buzzer | D8 | 15 | OUTPUT | Passive piezo through 100 Ω. Boot strap pin, must be LOW at boot. |
| OLED SCL | D1 | 5 | I2C | SSD1306 128x64, address 0x3C. Board header order: GND, VCC, SCL, SDA. Yellow/blue two-color panel (top 16 rows yellow) |
| OLED SDA | D2 | 4 | I2C | |
| Door switch | D7 | 13 | INPUT_PULLUP | To GND. LOW = door closed |
| Lock feedback | D6 | 12 | INPUT_PULLUP | Lock's dry contact to GND, closed when locked (LOW = locked). Never 12V on this pin |

### Power
- Relay JD-VCC jumper **removed**: JD-VCC ← Vin (5V coil supply), VCC ← 3V3 (logic), both GNDs to ESP GND.
- 12V lock side is isolated through the relay contacts (COM/NO), 1N4007 flyback diode across the lock. 12V negative does **not** tie to ESP GND.

## Status table (cage_lock)

**1. CLOSED + LOCKED** (normal)
- Door LOW, Chain LOW
- Band: CLOSED & LOCKED, swaps normal/inverted every 5 s
- Screen: JP lock -> Need something? -> Get A-1 -> OR -> Call Your Manager -> PRODUCTION / P O L K / TECHNOLOGIES -> WiFi (address only, once a minute, when the signal is FAIR or better; full signal screen when WEAK/BAD/offline)
- LED on

**2. CLOSED + UNLOCKED**
- Door LOW, Chain HIGH
- Band: CLOSED & UNLOCKED, swaps normal/inverted every 5 s
- Screen: CLOSED / BUT / NOT / LOCKED! one huge word at a time
- LED on

**3. OPEN + UNLOCKED** (cage in use)
- Door HIGH, Chain HIGH
- Band: OPEN & UNLOCKED, flashing
- Screen: Close the Cage -> JP unlock -> CLOSE / THE / CAGE -> JP unlock
- LED off

**4. OPEN + LOCKED** (shouldn't happen)
- Door HIGH, Chain LOW
- Band: OPEN & LOCKED, flashing
- Screen: Chain is locked but door open!
- LED off

- Both inputs use the internal pull-up: LOW = switch closed to GND. A disconnected wire reads HIGH (OPEN / UNLOCKED).
- Any input change restarts that state's loop and resets the band style.
- The relay never fires on a state change; only serial `unlock` + Enter sends a 500 ms pulse.
- Power-up/reset: D5 (GPIO14) stays high-impedance until setup() drives it HIGH, so the relay stays off. Wire the lock on COM + NO (never NC) and use a fail-secure lock.

## WiFi + web API (cage_lock)

Create `cage_lock/secrets.h` from `cage_lock/secrets.h.example` (gitignored) with WIFI_SSID, WIFI_PASS (2.4 GHz), API_KEY and AP_PASS.

**Setup hotspot (no reflash needed to change WiFi):** if WiFi isn't connected 30 s after boot (60 s after a drop), the lock starts hotspot **CageLock** (password = AP_PASS). Join it on a phone, the setup page pops up (or browse to http://192.168.4.1), pick the network, save. It's remembered across reboots and wins over secrets.h. The band shows "WiFi SETUP MODE" / "Join WiFi: CageLock" / "Open 192.168.4.1". With nobody on the hotspot it retries the saved WiFi every 5 min. Serial `wifireset` forgets the saved network and goes back to secrets.h. The unlock page/API are offline while the hotspot is up.

- `GET http://cagelock.local/status` -> `{"door":"closed","chain":"locked","state":"CLOSED & LOCKED","rssi":-61,"relay":false}`
- `http://cagelock.local/` -> simple page: live status, name box, Unlock button (API key entered once, saved in that browser).
- `PUT http://cagelock.local/unlock?name=JP&by=Tony` with header `X-Api-Key: <API_KEY>` -> unlock sequence: Unlocking / the / Cage / for (one word each, fast) / name scrolls (24pt bold, any length) / in / 3 / 2 / 1 (0.6 s each) -> relay pulse -> "UNLOCKED". `name` = who it's opened for (shown on screen), `by` = who is unlocking (logged); both optional, max 32 chars; long names scroll faster, 5 s max. 202 accepted (`fires_in_ms`), 401 bad key, 409 busy.
- `PUT /wifisetup` (key) -> lock switches to the CageLock setup hotspot (also a "Configure WiFi" button on the page).
- `GET /log` (key via header or `?key=`) -> event log CSV; `?since=<epoch>` returns only newer synced entries.
- Serial: `unlock` or `unlock JP` runs the same sequence. Band shows "Unlocking remotely" / "Please standby" until the relay fires, then "UNLOCKED".
- CORS open for a browser web app. WiFi signal is a screen in the CLOSED & LOCKED loop: "WiFi" / GREAT, GOOD, FAIR, WEAK, BAD; "WiFi" / "Connecting" while offline; "Join WiFi" / "CageLock" while the setup hotspot is up. Second half of the screen shows where to browse: "cagelock.local" / IP, or "Then open" / "192.168.4.1" in setup mode.

PowerShell test:
```powershell
Invoke-RestMethod http://cagelock.local/status
Invoke-RestMethod -Method Put "http://cagelock.local/unlock?name=JP&by=Tony" -Headers @{ "X-Api-Key" = "<API_KEY>" }
```

## Event log (cage_lock)

CSV in flash (`/log.csv`, 64 KB, then rotated to `/log.old.csv`; ~1,500-3,000 events kept). Clock from NTP once WiFi is up (US Central, `TZ_INFO`). Before sync, `epoch` is 0 and `local_time` blank; `uptime_s` still orders them.

```
epoch,local_time,uptime_s,event,detail
0,,0,boot,Power On
1759600212,2026-10-04 13:30:12,8,wifi_connected,IOT 192.168.1.57
1759600213,2026-10-04 13:30:13,9,time_sync,
1759601402,2026-10-04 13:50:02,1198,unlock_request,src=web;for=JP Jackson;by=Tony
1759601407,2026-10-04 13:50:07,1203,relay_pulse,for=JP Jackson;by=Tony
1759601415,2026-10-04 13:50:15,1211,chain,unlocked
1759601420,2026-10-04 13:50:20,1216,door,open;for=JP Jackson;by=Tony
1759601612,2026-10-04 13:53:32,1408,door,closed;open_s=192;for=JP Jackson;by=Tony
1759601620,2026-10-04 13:53:40,1416,chain,locked
1759601800,2026-10-04 13:56:40,1596,unlock_denied,unlock from 192.168.1.88
1759602000,2026-10-04 14:00:00,1796,wifi_lost,
1759602060,2026-10-04 14:01:00,1856,setup_hotspot,CageLock
1759602100,2026-10-04 14:01:40,1896,wifi_setup_request,192.168.1.20
```

`detail` is `key=value;key=value`. A door opening within 2 minutes of an unlock carries that unlock's `for`/`by`; the matching close adds `open_s` (seconds open). A door opened without a recent unlock logs `open;for=;by=`.

## Parts

- HiLetgo NodeMCU ESP8266 (CP2102)
- 2-channel 5V relay module (JD-VCC jumper)
- 12V pulse-type cabinet / solenoid lock + 12V supply
- 1N4007 diode (flyback across lock)
- Passive piezo buzzer + 100 Ω resistor
- 0.96" I2C OLED, SSD1306, 0x3C
- Door switch (magnetic) + magnet
- 12V lock has a built-in feedback switch (dry contact, closed when locked)
- Breadboard + jumpers

## Layout

```
cage_lock/                main sketch: door + chain-lock status (band shows e.g. CLOSED & LOCKED), playlist per state, relay via serial "unlock" or web PUT /unlock (API key)
docs/wiring.html          wiring page (keep in sync with hardware changes)
tests/buzzer_test/        chirps every 3 s
tests/relay_test/         send 'p' over serial for one 500 ms pulse
tests/oled_test/          scrolling "Cage Locked/Unlocked" title (D6) + JP logo
tests/reed_test/          prints OPEN/CLOSED, on-board LED mirrors state
secrets.h.example         copy to secrets.h (gitignored) for WiFi creds
```

## Build / flash (arduino-cli)

```sh
arduino-cli config add board_manager.additional_urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli core update-index && arduino-cli core install esp8266:esp8266
arduino-cli lib install "Adafruit SSD1306" "Adafruit GFX Library" "WiFiManager"   # LittleFS + time are built into the ESP8266 core

FQBN=esp8266:esp8266:nodemcuv2
arduino-cli compile -b $FQBN tests/relay_test
arduino-cli upload  -b $FQBN -p /dev/ttyUSB0 tests/relay_test   # COMx on Windows
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```
