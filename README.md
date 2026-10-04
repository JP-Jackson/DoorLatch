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
cage_lock/                main sketch: OPEN/CLOSED + LOCKED/UNLOCKED status, playlist per state, auto-lock with feedback check
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
