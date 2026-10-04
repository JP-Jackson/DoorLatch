# Cage Lock

ESP8266 (HiLetgo NodeMCU, CP2102) Cage Lock controller: 12V pulse-type cabinet lock via relay, OLED status, buzzer, door reed switch and lock feedback switch.

Wiring diagram + schematic: [`docs/wiring.html`](docs/wiring.html) (open in a browser).

## Pin map

| Function | NodeMCU | GPIO | Mode | Notes |
|---|---|---|---|---|
| Relay IN1 | D5 | 14 | OUTPUT | Active LOW. Write HIGH before `pinMode` so it doesn't click at boot. Pulse 500 ms max, never hold on. |
| Buzzer | D8 | 15 | OUTPUT | Passive piezo through 100 Ω. Boot strap pin, must be LOW at boot. |
| OLED SCL | D1 | 5 | I2C | SSD1306 128x64, address 0x3C. Board header order: GND, VCC, SCL, SDA. Yellow/blue two-color panel (top 16 rows yellow) |
| OLED SDA | D2 | 4 | I2C | |
| Door reed | D7 | 13 | INPUT_PULLUP | To GND. LOW = door closed |
| Lock feedback | D6 | 12 | INPUT_PULLUP | Lock's dry contact to GND, closed when locked (LOW = locked). Never 12V on this pin |

### Power
- Relay JD-VCC jumper **removed**: JD-VCC ← Vin (5V coil supply), VCC ← 3V3 (logic), both GNDs to ESP GND.
- 12V lock side is isolated through the relay contacts (COM/NO), 1N4007 flyback diode across the lock. 12V negative does **not** tie to ESP GND.

## Status table (cage_lock)

**1. CLOSED + LOCKED** (normal)
- Reed LOW, Chain LOW
- Band: CLOSED & LOCKED, swaps normal/inverted every 5 s
- Screen: JP lock -> Need something? -> Get A-1 -> OR -> Call Your Manager -> PRODUCTION / P O L K / TECHNOLOGIES
- LED on

**2. CLOSED + UNLOCKED**
- Reed LOW, Chain HIGH
- Band: CLOSED & UNLOCKED, swaps normal/inverted every 5 s
- Screen: CLOSED / BUT / NOT / LOCKED! one huge word at a time
- LED on

**3. OPEN + UNLOCKED** (cage in use)
- Reed HIGH, Chain HIGH
- Band: OPEN & UNLOCKED, flashing
- Screen: Close the Cage -> JP unlock -> CLOSE / THE / CAGE -> JP unlock
- LED off

**4. OPEN + LOCKED** (shouldn't happen)
- Reed HIGH, Chain LOW
- Band: OPEN & LOCKED, flashing
- Screen: Chain is locked but door open!
- LED off

- Both inputs use the internal pull-up: LOW = switch closed to GND. A disconnected wire reads HIGH (OPEN / UNLOCKED).
- Any input change restarts that state's loop and resets the band style.
- The relay never fires on a state change; only serial `unlock` + Enter sends a 500 ms pulse.
- Power-up/reset: D5 (GPIO14) stays high-impedance until setup() drives it HIGH, so the relay stays off. Wire the lock on COM + NO (never NC) and use a fail-secure lock.

## WiFi + web API (cage_lock)

Create `cage_lock/secrets.h` from `cage_lock/secrets.h.example` (gitignored) with WIFI_SSID, WIFI_PASS (2.4 GHz) and API_KEY. Change it and reflash when moving networks.

- `GET http://cagelock.local/status` -> `{"door":"closed","chain":"locked","state":"CLOSED & LOCKED","rssi":-61,"relay":false}`
- `http://cagelock.local/` -> simple page: live status, name box, Unlock button (API key entered once, saved in that browser).
- `PUT http://cagelock.local/unlock?name=JP` with header `X-Api-Key: <API_KEY>` -> unlock sequence: "Unlocking the Cage" / "for" / name scrolls (24pt bold, any length) / "in" / 3 / 2 / 1 -> relay pulse -> "UNLOCKED". Name optional (max 32 chars; long names scroll faster, 5 s max). 202 accepted (`fires_in_ms`), 401 bad key, 409 busy.
- Serial: `unlock` or `unlock JP` runs the same sequence. Band shows "Unlocking remotely" / "Please standby" until the relay fires, then "UNLOCKED".
- CORS open for a browser web app. Band shows "WiFi Signal: GREAT/GOOD/FAIR/WEAK/BAD" or "NO WIFI" for 2 s every 30 s.

PowerShell test:
```powershell
Invoke-RestMethod http://cagelock.local/status
Invoke-RestMethod -Method Put "http://cagelock.local/unlock?name=JP" -Headers @{ "X-Api-Key" = "<API_KEY>" }
```

## Parts

- HiLetgo NodeMCU ESP8266 (CP2102)
- 2-channel 5V relay module (JD-VCC jumper)
- 12V pulse-type cabinet / solenoid lock + 12V supply
- 1N4007 diode (flyback across lock)
- Passive piezo buzzer + 100 Ω resistor
- 0.96" I2C OLED, SSD1306, 0x3C
- Door reed switch + magnet
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
arduino-cli lib install "Adafruit SSD1306" "Adafruit GFX Library"

FQBN=esp8266:esp8266:nodemcuv2
arduino-cli compile -b $FQBN tests/relay_test
arduino-cli upload  -b $FQBN -p /dev/ttyUSB0 tests/relay_test   # COMx on Windows
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```
