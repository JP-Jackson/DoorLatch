// Cage Lock main sketch.
// Inputs: door reed D7 (LOW = closed), chain lock feedback D6 dry contact (LOW = locked).
// The lock is a chain around the door, separate from the door itself.
// Yellow band (rows 0-15): combined state, e.g. "CLOSED & LOCKED", centered, in
//   bold bitmaps pre-rendered from DejaVu Sans Bold (band_text.h, band_gen.py.txt),
//   Door closed: static, swapping normal/inverted every 5 s.
//   Door open:   flashing normal <-> inverted.
// Blue area (rows 16-63): playlist per state, restarting whenever either input changes.
//   CLOSED + LOCKED:   JP + padlock slides in from the right, locks, padlock rattles
//                      -> "Need something?" -> "Get A-1" (A-1 parks centered) -> "OR"
//                      -> "Call Your Manager" slides in from the right
//                      -> PRODUCTION / P O L K / TECHNOLOGIES (fades)
//                      -> WiFi screen ("WiFi" / GREAT/GOOD/FAIR/WEAK/BAD/OFFLINE) -> repeat.
//   CLOSED + UNLOCKED: "CLOSED" / "BUT" / "NOT" / "LOCKED!" one huge word at a time (inverted).
//   OPEN + UNLOCKED:   "Close the Cage" -> JP unlock -> "CLOSE" / "THE" / "CAGE"
//                      -> JP unlock -> repeat (inverted colors).
//   OPEN + LOCKED:     "Chain is locked but door open!" (shouldn't happen).
// On-board LED mirrors the reed: on = closed.
// WiFi: joins the last network saved from the setup portal, or secrets.h if none was
//   saved, in the background (display and inputs keep running if it's down).
//   Setup hotspot: if WiFi isn't connected 30 s after boot (60 s after a drop), it
//   starts hotspot "CageLock" (password AP_PASS) with a setup page to pick a network.
//   Band alternates state / "WiFi SETUP MODE" / "Join WiFi: CageLock". With no one
//   on the hotspot it retries saved WiFi every 5 min. Serial "wifireset" forgets
//   the saved network and reboots (back to secrets.h). Signal strength is a screen
//   in the CLOSED + LOCKED loop. mDNS name: cagelock.local
// Web page: http://cagelock.local/ shows live status and WiFi rating, a name box and
//   Unlock button, Configure WiFi (starts the setup hotspot) and the event log. The
//   API key is typed into the page once and kept in that browser.
// Log: CSV in flash (LittleFS) /log.csv, 64 KB then rotated to /log.old.csv.
//   Columns: epoch,local_time,uptime_s,event,detail. Time comes from NTP once WiFi
//   is up (TZ_INFO, US Central); before that epoch is 0 and local_time blank.
//   Events: boot, door, chain, unlock_request, unlock_denied, relay_pulse,
//   wifi_connected, wifi_lost, setup_hotspot, wifi_setup_request, time_sync.
// Web API (port 80):
//   GET /status  -> {"door":"closed","chain":"locked","state":"CLOSED & LOCKED",
//                    "rssi":-61,"relay":false}
//   PUT /unlock?name=JP -> runs the unlock sequence (name optional, max 32 chars).
//                   Needs header "X-Api-Key: <API_KEY>". 202 accepted (relay fires
//                   at the end of the countdown), 401 bad/missing key, 409 busy.
//   PUT /wifisetup -> (key) switch to the CageLock setup hotspot. 202.
//   GET /log[?since=EPOCH] -> (key, header or ?key=) the log as CSV; with since,
//                   only synced entries at/after that time.
//   CORS is open so a browser web app can call it.
// Unlock sequence (blue area, black bold text on a lit background):
//   "Unlocking" -> "the" -> "Cage" -> "for" (one big word each, 0.45 s) -> NAME
//   scrolls across (24pt, any length) -> "in" -> 3 -> 2 -> 1 (0.6 s each) -> relay pulse -> "UNLOCKED" (12pt, biggest that fits).
//   Band (inverted, bigger bold text): "Unlocking remotely" / "Please standby"
//   alternating until the relay fires, then "UNLOCKED". Without a name it skips "for" + NAME. Input changes during the
//   sequence don't interrupt it; the state playlist resumes afterwards.
// Relay (D5, active LOW): never fires on its own. "unlock" or "unlock NAME" + Enter
// over serial (115200) runs the unlock sequence; 2 s minimum between pulses.
// Serial is ignored for the first 3 s after boot so noise can't trigger it.
// Boot/power loss: D5 (GPIO14) is high-impedance until setup() drives it HIGH,
// so the relay stays off through reset, brownout and power-up.
// Libraries: Adafruit SSD1306, Adafruit GFX Library, WiFiManager (tzapu).
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>
#include <LittleFS.h>
#include <time.h>
extern "C" {
#include <user_interface.h>  // wifi_station_disconnect()
}
#include "logo.h"
#include "band_text.h"
#include "company_text.h"
#include "seq_text.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy secrets.h.example to cage_lock/secrets.h and fill in WIFI_SSID, WIFI_PASS, API_KEY"
#endif

#ifndef AP_PASS
#define AP_PASS "cagelock"  // set your own in secrets.h (8+ chars)
#endif

const char HOSTNAME[] = "cagelock";
const char AP_NAME[] = "CageLock";
const uint32_t PORTAL_AFTER_BOOT_MS = 30000;  // no WiFi this long after boot -> hotspot
const uint32_t PORTAL_AFTER_DROP_MS = 60000;  // ...or this long after a drop
const uint32_t PORTAL_RETRY_MS = 300000;      // idle hotspot steps aside to retry WiFi
WiFiManager wm;
bool serverUp = false, everConnected = false;
uint32_t wifiDownSince = 0, portalSince = 0;
uint32_t portalRequestAt = 0;                 // web "Configure WiFi": start hotspot at this time

// ---------- event log ----------

const char TZ_INFO[] = "CST6CDT,M3.2.0,M11.1.0";  // US Central with DST
const char LOG_PATH[] = "/log.csv", LOG_OLD[] = "/log.old.csv";
const char LOG_HEADER[] = "epoch,local_time,uptime_s,event,detail\n";
const size_t LOG_MAX = 64 * 1024;
bool fsOk = false, timeLogged = false;

bool timeValid() { return time(nullptr) > 1700000000; }  // NTP has synced

// One CSV line to flash (and serial). Commas/quotes/newlines in detail become spaces.
void logEvent(const char *event, const char *detail = "") {
  char clean[80];
  uint8_t n = 0;
  for (; *detail && n < sizeof(clean) - 1; detail++)
    clean[n++] = (*detail == ',' || *detail == '"' || *detail == '\n' || *detail == '\r') ? ' ' : *detail;
  clean[n] = 0;
  char ts[20] = "";
  time_t now = time(nullptr);
  bool ok = timeValid();
  if (ok) {
    struct tm t;
    localtime_r(&now, &t);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &t);
  }
  char line[160];
  snprintf(line, sizeof(line), "%ld,%s,%lu,%s,%s\n", ok ? (long)now : 0L, ts,
           (unsigned long)(millis() / 1000), event, clean);
  Serial.print(F("LOG "));
  Serial.print(line);
  if (!fsOk) return;
  if (LittleFS.exists(LOG_PATH)) {
    File c = LittleFS.open(LOG_PATH, "r");
    size_t sz = c.size();
    c.close();
    if (sz > LOG_MAX) {  // rotate: keep one old file
      LittleFS.remove(LOG_OLD);
      LittleFS.rename(LOG_PATH, LOG_OLD);
    }
  }
  bool fresh = !LittleFS.exists(LOG_PATH);
  File f = LittleFS.open(LOG_PATH, "a");
  if (!f) return;
  if (fresh) f.print(LOG_HEADER);
  f.print(line);
  f.close();
}

// "GREAT" ... "BAD", or "OFFLINE".
int8_t wifiBars();
const char *wifiQuality() {
  static const char *Q[] = {"BAD", "WEAK", "FAIR", "GOOD", "GREAT"};
  int8_t b = wifiBars();
  return b < 0 ? "OFFLINE" : Q[b];
}
ESP8266WebServer server(80);
bool mdnsStarted = false;
bool wifiWasUp = false;

const uint8_t REED_PIN = D7;         // door reed to GND, LOW = closed
const uint8_t LOCK_PIN = D6;         // chain lock feedback, dry contact to GND, closed when locked
const uint8_t LOCKED_LEVEL = LOW;
const uint8_t LED_PIN = LED_BUILTIN; // D4/GPIO2, active LOW
const uint8_t RELAY_PIN = D5;        // relay IN1, active LOW
const uint8_t RELAY_ON = LOW, RELAY_OFF = HIGH;
const uint16_t PULSE_MS = 500;       // hard max for the lock coil - do not raise
const uint16_t PULSE_GAP_MS = 2000;  // let the solenoid cool between pulses
const uint16_t SERIAL_IGNORE_MS = 3000;  // drop serial input right after boot
const uint16_t FRAME_MS = 25;
const uint16_t DEBOUNCE_MS = 50;

// Title timing
const uint16_t FLASH_MS = 300;       // open: time per normal/inverted half

// Blue area timing
const uint16_t FADE_STEP_MS = 50;    // 16 steps each way (Polk only)
const uint16_t LOCK_MS = 3000;       // closed JP lock animation, total
const uint16_t UNLOCK_MS = 2200;     // open JP unlock animation, total
const uint16_t NEED_MS = 2000;       // "Need something?" on screen
const uint16_t GET_SCROLL_MS = 1720; // "Get A-1" scrolls until "Get" is off-screen
const uint16_t A1_MS = 1000;         // "A-1" alone after "Get" leaves
const uint16_t GET_MS = GET_SCROLL_MS + A1_MS;
const uint16_t OR_MS = 700;          // "OR" on screen
const uint16_t SLIDE_MS = 400;       // "Call Your Manager" slide-in
const uint16_t CALL_MS = 2000;       // "Call Your Manager" on screen
const uint16_t TEXT_HOLD_MS = 3000;  // company name
const uint16_t OPEN_MSG_MS = 3000;   // open message
const uint16_t GAP_MS = 300;         // blank between screens

// Open message (two lines, FreeSans Bold 12pt, max ~128 px each)
const char OPEN_L1[] = "Close the";
const char OPEN_L2[] = "Cage";
const uint16_t WORD_MS = 700;        // "CLOSE" / "THE" / "CAGE" each
const uint16_t LAST_WORD_MS = 1200;  // "CAGE" holds a bit longer
const uint16_t NOTLOCKED_MS = 3*700 + 1200;  // CLOSED / BUT / NOT 0.7 s each, LOCKED! 1.2 s
const uint16_t WARN_MS = 3000;       // open + locked message

const int16_t BAND_H = 16, BLUE_Y = 16, BLUE_H = 48;
const int16_t LOGO_Y = BLUE_Y + (BLUE_H - JP_H) / 2;

const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

Adafruit_SSD1306 display(128, 64, &Wire, -1);

int reedStable = -1, reedLast = -1;
int lockStable = -1, lockLast = -1;
uint32_t lockChange = 0;
bool relayActive = false;
uint32_t relayOnAt = 0, relayOffAt = 0;
uint32_t reedChange = 0, lastFrame = 0;

bool flashInv = false;
uint32_t tTimer = 0;

// Blue area: play the current playlist; Polk fades, everything else cuts.
enum BluePhase { B_IN, B_HOLD, B_OUT, B_GAP };
BluePhase bPhase = B_IN;
enum BlueItem { I_LOCK, I_MSG, I_POLK, I_UNLOCK, I_OPENMSG, I_OPENWORDS, I_NOTLOCKED, I_WARN, I_WIFI };
const uint8_t CLOSED_LIST[] = {I_LOCK, I_MSG, I_POLK, I_WIFI};  // closed + locked
const uint8_t NAG_LIST[] = {I_NOTLOCKED};                       // closed + unlocked
const uint8_t OPEN_LIST[] = {I_OPENMSG, I_UNLOCK, I_OPENWORDS, I_UNLOCK};  // open + unlocked
const uint8_t WARN_LIST[] = {I_WARN};                           // open + locked
uint8_t listPos = 0;
uint8_t item = I_LOCK;
uint8_t fadeLevel = 0;               // 0 = blank, 16 = fully drawn
uint32_t bTimer = 0;

bool isOpen() { return reedStable == HIGH; }
bool isLocked() { return lockStable == LOCKED_LEVEL; }
const char *titleText() {
  if (isOpen()) return isLocked() ? "OPEN & LOCKED" : "OPEN & UNLOCKED";
  return isLocked() ? "CLOSED & LOCKED" : "CLOSED & UNLOCKED";
}
const char *stateText() {
  if (isOpen()) return isLocked() ? "OPEN - LOCKED" : "OPEN - UNLOCKED";
  return isLocked() ? "CLOSED - LOCKED" : "CLOSED - UNLOCKED";
}

// Built-in font at 1x wide, 2x tall, drawn twice 1 px apart (faux bold) so
// strokes are 2 px wide. 7 px per letter, 3 px per space.
// "CLOSED & UNLOCKED" (the longest) is 110 px.
int16_t titleWidth(const char *s) {
  int16_t w = 0;
  for (; *s; s++) w += (*s == ' ') ? 3 : 7;
  return w - 1;
}

void printTitle(const char *s, int16_t x, uint16_t color) {
  display.setTextSize(1, 2);
  display.setTextColor(color);
  for (; *s; s++) {
    if (*s == ' ') { x += 3; continue; }
    display.setCursor(x, 1);
    display.print(*s);
    display.setCursor(x + 1, 1);
    display.print(*s);
    x += 7;
  }
  display.setTextSize(1);
}

// ---------- yellow band ----------

void resetTitle() {
  flashInv = false;
  tTimer = millis();
}

const uint16_t CLOSED_INV_MS = 5000;   // closed: swap normal/inverted every 5 s

// 0-4 bars from RSSI, -1 when not connected.
int8_t wifiBars() {
  if (WiFi.status() != WL_CONNECTED) return -1;
  long rssi = WiFi.RSSI();
  return rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : rssi > -85 ? 1 : 0;
}

// Pre-rendered bold bitmap (band_text.h) for a known phrase, centered.
bool drawBandBitmap(const char *t, uint16_t color) {
  for (const BandText &b : BAND_TEXTS) {
    if (strcmp(b.text, t) == 0) {
      display.drawBitmap((128 - b.w) / 2, 0, b.bmp, b.w, 16, color);
      return true;
    }
  }
  return false;
}

extern bool seqActive, seqFired;
extern uint32_t seqStart;
const uint16_t SEQ_BAND_SWAP_MS = 1500;

void drawTitle() {
  const char *t = titleText();
  if (seqActive) {
    // Web/serial unlock: alternate the two phrases until the relay fires.
    t = seqFired ? "UNLOCKED"
        : ((millis() - seqStart) / SEQ_BAND_SWAP_MS) % 2 ? "Please standby" : "Unlocking remotely";
  } else if (wm.getConfigPortalActive() && (millis() / 2000) % 4) {
    // Setup hotspot running: state, then the three setup lines, 2 s each.
    static const char *SETUP[] = {"", "WiFi SETUP MODE", "Join WiFi: CageLock", "Open 192.168.4.1"};
    t = SETUP[(millis() / 2000) % 4];
  }
  int16_t w = titleWidth(t);
  bool inv = seqActive ? true : isOpen() ? flashInv : (millis() / CLOSED_INV_MS) % 2;
  display.setFont(NULL);
  display.fillRect(0, 0, 128, BAND_H, inv ? SSD1306_WHITE : SSD1306_BLACK);
  uint16_t fg = inv ? SSD1306_BLACK : SSD1306_WHITE;
  if (!drawBandBitmap(t, fg)) printTitle(t, (128 - w) / 2, fg);

  // Open: flash between normal and inverted. Closed: set per playlist cycle.
  if (isOpen() && millis() - tTimer >= FLASH_MS) {
    tTimer = millis();
    flashInv = !flashInv;
  }
}

// ---------- blue area ----------

void printCentered(const char *s, int16_t baseline, int16_t xoff = 0) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(s, 0, baseline, &x1, &y1, &w, &h);
  display.setCursor(xoff + (128 - (int16_t)w) / 2 - x1, baseline);
  display.print(s);
}

// JP logo + padlock on a lit background.
// shift: px the whole group is pushed right. lift: shackle raise in px.
// shakeX/Y move the whole padlock; wigX/Y move only the shackle.
void drawPadlockScene(int16_t shift, int16_t lift, int16_t shakeX, int16_t shakeY,
                      int16_t wigX = 0, int16_t wigY = 0) {
  const int16_t GAP = 12, BODY_W = 26, BODY_H = 20;
  const int16_t lx = (128 - (JP_W + GAP + BODY_W)) / 2 + shift;
  const int16_t bx = lx + JP_W + GAP + shakeX, by = 43 + shakeY;
  const uint16_t bg = SSD1306_WHITE, fg = SSD1306_BLACK;

  display.fillRect(0, BLUE_Y, 128, BLUE_H, bg);
  display.drawBitmap(lx, LOGO_Y, JP_BMP, JP_W, JP_H, fg);
  // Shackle: hollow U, legs hidden in the body when closed
  int16_t sx = bx + 3 + wigX, sy = by - 16 - lift + wigY;
  display.fillRoundRect(sx, sy, 20, 22, 10, fg);
  display.fillRoundRect(sx + 4, sy + 4, 12, 22, 6, bg);
  // Body + keyhole
  display.fillRoundRect(bx, by, BODY_W, BODY_H, 3, fg);
  display.fillCircle(bx + 13, by + 7, 3, bg);
  display.fillRect(bx + 12, by + 8, 3, 7, bg);
}

const int16_t LIFT_MAX = 10;

// Closed. 0-500 ms: slides in from the right, shackle open.
// 500-900: pause. 900-1300: shackle drops. 1300-1705: padlock rattles.
void drawLockAnim(uint32_t t) {
  int16_t shift = 0, lift = LIFT_MAX, sx = 0, sy = 0;
  if (t < 500) {
    float p = 1 - t / 500.0;
    shift = 128 * p * p;            // ease out
  }
  if (t >= 1300) lift = 0;
  else if (t >= 900) {
    float p = (t - 900) / 400.0;
    lift = LIFT_MAX * (1 - p * p);  // accelerate like it's falling
  }
  if (t >= 1300 && t < 1705) {
    // left/right and up/down, dying out
    static const int8_t SX[] = {3, 0, -3, 0, 2, 0, -2, 0, 0};
    static const int8_t SY[] = {0, -2, 0, 2, 0, -1, 0, 1, 0};
    uint32_t i = min((uint32_t)8, (t - 1300) / 45);
    sx = SX[i];
    sy = SY[i];
  }
  drawPadlockScene(shift, lift, sx, sy);
}

// Open. 0-400 ms: locked. 400-700: shackle rises. 700-1240: shackle wiggles at the top.
void drawUnlockAnim(uint32_t t) {
  int16_t lift = 0, wx = 0, wy = 0;
  if (t >= 700) lift = LIFT_MAX;
  else if (t >= 400) {
    float p = 1 - (t - 400) / 300.0;
    lift = LIFT_MAX * (1 - p * p);  // fast then settle
  }
  if (t >= 700 && t < 1240) {
    static const int8_t WX[] = {2, -2, 2, -2, 2, -2, 1, -1, 1, -1, 0, 0};
    static const int8_t WY[] = {-1, 0, -1, 0, -1, 0, -1, 0, 0, 0, 0, 0};
    uint32_t i = min((uint32_t)11, (t - 700) / 45);
    wx = WX[i];
    wy = WY[i];
  }
  drawPadlockScene(0, lift, 0, 0, wx, wy);
}

// Two lines of 12pt text; baselines fit cap height + descenders in rows 16-63.
void drawMessage(const GFXfont *font, const char *l1, const char *l2,
                 int16_t xoff, uint16_t color, int16_t base1 = 33, int16_t base2 = 57) {
  display.setFont(font);
  display.setTextSize(1);
  display.setTextColor(color);
  printCentered(l1, base1, xoff);
  printCentered(l2, base2, xoff);
  display.setFont(NULL);
}

// One line of FreeSans Bold 24pt, centered.
void drawBig(const char *s, int16_t baseline) {
  display.setFont(&FreeSansBold24pt7b);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  printCentered(s, baseline);
  display.setFont(NULL);
}

// One word, centered, black on a lit area. track = extra px between letters
// (negative squeezes). Baseline centers the cap height in rows 16-63.
void drawWord(const char *w, const GFXfont *font, int8_t track, int16_t cap) {
  display.fillRect(0, BLUE_Y, 128, BLUE_H, SSD1306_WHITE);
  display.setFont(font);
  display.setTextSize(1);
  display.setTextColor(SSD1306_BLACK);
  int16_t x1, y1;
  uint16_t bw, bh;
  int16_t base = BLUE_Y + (BLUE_H - cap) / 2 + cap;
  display.getTextBounds(w, 0, base, &x1, &y1, &bw, &bh);
  int16_t width = bw + track * ((int16_t)strlen(w) - 1);
  display.setCursor((128 - width) / 2 - x1, base);
  for (const char *c = w; *c; c++) {
    display.print(*c);
    display.setCursor(display.getCursorX() + track, base);
  }
  display.setFont(NULL);
}

// "CLOSE" / "THE" / "CAGE", each as big as fits 128 px:
// CLOSE is 161 px in 24pt so it gets 18pt; CAGE is 132 px in 24pt, squeezed 2 px.
void drawOpenWords(uint32_t t) {
  if (t < WORD_MS) drawWord("CLOSE", &FreeSansBold18pt7b, 0, 25);
  else if (t < 2 * WORD_MS) drawWord("THE", &FreeSansBold24pt7b, 0, 34);
  else drawWord("CAGE", &FreeSansBold24pt7b, -2, 34);
}

// One word in the built-in font, 6x tall (42 px) and as wide as fits 128 px,
// black on a lit area. Built-in glyphs are 5 px + 1 px gap per char.
void drawBlockWord(const char *w) {
  int16_t n = strlen(w);
  int16_t sx = 128 / (6 * n - 1);
  if (sx > 8) sx = 8;
  if (sx < 1) sx = 1;
  int16_t sy = min(6, 3 * (int)sx);  // keep long names from looking like needles
  int16_t width = n * 6 * sx - sx;
  display.fillRect(0, BLUE_Y, 128, BLUE_H, SSD1306_WHITE);
  display.setFont(NULL);
  display.setTextSize(sx, sy);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor((128 - width) / 2, BLUE_Y + (BLUE_H - 7 * sy) / 2);
  display.print(w);
  display.setTextSize(1);
}

// Closed but chain not locked, one word at a time:
// CLOSED (3x wide), BUT (7x), NOT (7x), LOCKED! (3x).
void drawNotLocked(uint32_t t) {
  if (t < 700) drawBlockWord("CLOSED");
  else if (t < 1400) drawBlockWord("BUT");
  else if (t < 2100) drawBlockWord("NOT");
  else drawBlockWord("LOCKED!");
}

// WiFi signal screen: "WiFi" (12pt) over the rating (18pt, or the biggest that fits).
// Offline with the setup hotspot up: "Join WiFi" / "CageLock".
// Offline before the hotspot starts: "WiFi" / "Connecting".
// Then (connected or setup): "cagelock.local" / IP, or "Then open" / "192.168.4.1".
const uint16_t WIFI_MS = 5000;  // 2.5 s signal, 2.5 s address
int8_t wifiBars();
// Biggest of 18/12/9pt bold (then the built-in font) that fits 126 px.
// Returns false if nothing fit and the built-in font was selected.
bool fitFont(const char *q, bool allow18) {
  static const GFXfont *SIZES[] = {&FreeSansBold18pt7b, &FreeSansBold12pt7b, &FreeSansBold9pt7b};
  int16_t x1, y1;
  uint16_t w, h;
  for (uint8_t i = allow18 ? 0 : 1; i < 3; i++) {
    display.setFont(SIZES[i]);
    display.getTextBounds(q, 0, 0, &x1, &y1, &w, &h);
    if (w <= 126) return true;
  }
  display.setFont(NULL);  // built-in 6 px/char fallback
  return false;
}

void printFit(const char *q, int16_t baseline, bool allow18) {
  if (!fitFont(q, allow18)) {  // built-in font draws from the top, not the baseline
    display.setCursor((128 - (int16_t)strlen(q) * 6) / 2, baseline - 7);
    display.print(q);
  } else {
    printCentered(q, baseline);
  }
}

// First half: signal (or setup / connecting). Second half: where to browse.
void drawWifiScreen() {
  static const char *Q[] = {"BAD", "WEAK", "FAIR", "GOOD", "GREAT"};
  int8_t bars = wifiBars();
  bool setup = bars < 0 && wm.getConfigPortalActive();
  bool addr = millis() - bTimer >= WIFI_MS / 2 && (bars >= 0 || setup);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  if (addr) {
    static char ip[16];
    if (setup) {
      printFit("Then open", 35, false);
      printFit("192.168.4.1", 62, false);
    } else {
      strncpy(ip, WiFi.localIP().toString().c_str(), sizeof(ip) - 1);
      printFit("cagelock.local", 35, false);
      printFit(ip, 62, false);
    }
  } else {
    const char *q = setup ? "CageLock" : bars < 0 ? "Connecting" : Q[bars];
    display.setFont(&FreeSansBold12pt7b);
    printCentered(setup ? "Join WiFi" : "WiFi", 35);
    printFit(q, 62, true);
  }
  display.setFont(NULL);
}

// Open but chain locked: shouldn't happen (chain locked with the door open).
void drawWarn() {
  display.fillRect(0, BLUE_Y, 128, BLUE_H, SSD1306_WHITE);
  display.setFont(&FreeSansBold9pt7b);
  display.setTextSize(1);
  display.setTextColor(SSD1306_BLACK);
  printCentered("Chain is", 28);
  printCentered("locked but", 43);
  printCentered("door open!", 58);
  display.setFont(NULL);
}

// "Need something?" -> "Get A-1" -> "OR" -> inverted "Call Your Manager"
// panel slides in from the right. t = ms into the hold.
void drawMsgScene(uint32_t t) {
  if (t < NEED_MS) {
    drawMessage(&FreeSans12pt7b, "Need", "something?", 0, SSD1306_WHITE);
    return;
  }
  t -= NEED_MS;
  if (t < GET_MS) {
    // Scroll "Get A-1" together; "A-1" (67 px) parks in the center while
    // "Get" (77 px, A-1 starts 92 px after it) keeps going off the left edge.
    const int16_t GET_END = -78, A1_OFS = 92, A1_X = (128 - 67) / 2;
    int16_t x = (t < GET_SCROLL_MS) ? 128 - (int16_t)((128 - GET_END) * t / GET_SCROLL_MS) : GET_END;
    display.setFont(&FreeSansBold24pt7b);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(x, 57);
    display.print("Get");
    display.setCursor(max(x + A1_OFS, (int)A1_X), 57);
    display.print("A-1");
    display.setFont(NULL);
    return;
  }
  t -= GET_MS;
  drawBig("OR", 57);
  if (t < OR_MS) return;
  t -= OR_MS;
  int16_t x = (t >= SLIDE_MS) ? 0 : 128 - (int16_t)(128 * t / SLIDE_MS);
  display.fillRect(x, BLUE_Y, 128, BLUE_H, SSD1306_WHITE);
  drawMessage(&FreeSansBold12pt7b, "Call Your", "Manager", x, SSD1306_BLACK, 38, 58);  // 2 px gap, "g" ends on the last row
}

void drawCompany() {
  // PRODUCTION / P O L K / TECHNOLOGIES, one pre-rendered 128x48 bitmap
  // (company_text.h) laid out like the Polk logo, all lines the same width.
  display.drawBitmap(0, BLUE_Y, COMPANY_BMP, COMPANY_BMP_W, COMPANY_BMP_H, SSD1306_WHITE);
}

void drawOpenMsg() {
  display.fillRect(0, BLUE_Y, 128, BLUE_H, SSD1306_WHITE);
  drawMessage(&FreeSansBold12pt7b, OPEN_L1, OPEN_L2, 0, SSD1306_BLACK);
}

// Ordered dither: clear pixels above the current level so the area "fades".
void applyFade(uint8_t level) {
  if (level >= 16) return;
  for (int16_t y = BLUE_Y; y < BLUE_Y + BLUE_H; y++)
    for (int16_t x = 0; x < 128; x++)
      if (BAYER[y & 3][x & 3] >= level) display.drawPixel(x, y, SSD1306_BLACK);
}

// ---------- unlock sequence ----------

void startPlaylist();
bool pulseRelay();

const uint16_t SEQ_WORD_MS = 450;     // "Unlocking" / "the" / "Cage" / "for" / "in" each
const uint16_t NAME_PX_PER_S = 120;   // same speed as "Get A-1"...
const uint16_t NAME_MAX_MS = 5000;    // ...but long names speed up to finish in 5 s
const int16_t NAME_BASE = 52;         // 24pt baseline; leaves room for g/y/p tails
int16_t seqNameW = 0, seqNameX1 = 0;
uint16_t seqNameMs = 0;
const uint16_t SEQ_COUNT_MS = 600, SEQ_DONE_MS = 2000;
bool seqActive = false, seqFired = false;
uint32_t seqStart = 0;
char seqName[33] = "";               // up to 32 chars

uint32_t seqFireAt() {
  uint32_t t = 3 * SEQ_WORD_MS;                  // "Unlocking", "the", "Cage"
  if (seqName[0]) t += SEQ_WORD_MS + seqNameMs;  // "for" + name
  return t + SEQ_WORD_MS + 3 * SEQ_COUNT_MS;  // "in" + 3, 2, 1
}

// Keep printable ASCII, trim, max 32 chars.
void setSeqName(const char *n) {
  uint8_t len = 0;
  while (*n == ' ') n++;
  for (; *n && len < sizeof(seqName) - 1; n++)
    if (*n >= 32 && *n <= 126) seqName[len++] = *n;
  while (len && seqName[len - 1] == ' ') len--;
  seqName[len] = 0;
}

bool startUnlockSeq(const char *name, const char *src) {
  if (seqActive || relayActive) return false;
  setSeqName(name);
  char d[48];
  snprintf(d, sizeof(d), "%s %s", src, seqName[0] ? seqName : "(no name)");
  logEvent("unlock_request", d);
  // Measure the name so it scrolls fully on and off, at a fixed speed.
  int16_t y1;
  uint16_t w, h;
  display.setFont(&FreeSansBold24pt7b);
  display.setTextSize(1);
  display.getTextBounds(seqName, 0, NAME_BASE, &seqNameX1, &y1, &w, &h);
  display.setFont(NULL);
  seqNameW = w;
  seqNameMs = min((uint32_t)NAME_MAX_MS, (uint32_t)(128 + seqNameW) * 1000 / NAME_PX_PER_S);
  seqActive = true;
  seqFired = false;
  seqStart = millis();
  Serial.print(F("Unlock sequence for: "));
  Serial.println(seqName[0] ? seqName : "(no name)");
  return true;
}

// One word in 24pt bold, black on a lit area, centered on its full glyph box
// (so descenders like the g in "Cage" stay on screen).
void drawBigWord(const char *w) {
  display.fillRect(0, BLUE_Y, 128, BLUE_H, SSD1306_WHITE);
  display.setFont(&FreeSansBold24pt7b);
  display.setTextSize(1);
  display.setTextColor(SSD1306_BLACK);
  int16_t x1, y1;
  uint16_t bw, bh;
  display.getTextBounds(w, 0, 0, &x1, &y1, &bw, &bh);
  display.setCursor((128 - (int16_t)bw) / 2 - x1, BLUE_Y + (BLUE_H - (int16_t)bh) / 2 - y1);
  display.print(w);
  display.setFont(NULL);
}

void drawSeq(uint32_t t) {
  if (t < SEQ_WORD_MS) {
    // "Unlocking" is 223 px in 24pt; pre-rendered condensed so it can stay big.
    display.fillRect(0, BLUE_Y, 128, BLUE_H, SSD1306_WHITE);
    display.drawBitmap((128 - UNLOCKING_BMP_W) / 2, BLUE_Y + (BLUE_H - UNLOCKING_BMP_H) / 2,
                       UNLOCKING_BMP, UNLOCKING_BMP_W, UNLOCKING_BMP_H, SSD1306_BLACK);
    return;
  }
  t -= SEQ_WORD_MS;
  if (t < SEQ_WORD_MS) { drawBigWord("the"); return; }
  t -= SEQ_WORD_MS;
  if (t < SEQ_WORD_MS) { drawBigWord("Cage"); return; }
  t -= SEQ_WORD_MS;
  if (seqName[0]) {
    if (t < SEQ_WORD_MS) { drawBigWord("for"); return; }
    t -= SEQ_WORD_MS;
    if (t < seqNameMs) {
      // Scroll right to left: starts just off the right edge, ends just off the left.
      int16_t x = 128 - (int16_t)((uint32_t)(128 + seqNameW) * t / seqNameMs) - seqNameX1;
      display.fillRect(0, BLUE_Y, 128, BLUE_H, SSD1306_WHITE);
      display.setFont(&FreeSansBold24pt7b);
      display.setTextSize(1);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(x, NAME_BASE);
      display.print(seqName);
      display.setFont(NULL);
      return;
    }
    t -= seqNameMs;
  }
  if (t < SEQ_WORD_MS) { drawBigWord("in"); return; }
  t -= SEQ_WORD_MS;
  if (t < 3 * SEQ_COUNT_MS) {
    static const char *N[] = {"3", "2", "1"};
    drawBigWord(N[t / SEQ_COUNT_MS]);
    return;
  }
  // 137 px in 12pt bold; 2 px tighter letters bring it to 123 px.
  drawWord("UNLOCKED", &FreeSansBold12pt7b, -2, 17);
}

// Fires the relay at the end of the countdown, then hands the screen back.
void serviceSeq() {
  if (!seqActive) return;
  uint32_t t = millis() - seqStart, fireAt = seqFireAt();
  if (!seqFired && t >= fireAt) {
    seqFired = true;
    pulseRelay();
  }
  if (t >= fireAt + SEQ_DONE_MS) {
    seqActive = false;
    startPlaylist();
  }
}

bool itemFades(uint8_t i) { return i == I_POLK; }

uint16_t itemHoldMs(uint8_t i) {
  switch (i) {
    case I_LOCK: return LOCK_MS;
    case I_MSG: return NEED_MS + GET_MS + OR_MS + SLIDE_MS + CALL_MS;
    case I_POLK: return TEXT_HOLD_MS;
    case I_UNLOCK: return UNLOCK_MS;
    case I_OPENWORDS: return 2 * WORD_MS + LAST_WORD_MS;
    case I_NOTLOCKED: return NOTLOCKED_MS;
    case I_WARN: return WARN_MS;
    case I_WIFI: return WIFI_MS;
    default: return OPEN_MSG_MS;
  }
}

// Playlist for the current door + lock state.
const uint8_t *curList(uint8_t &n) {
  if (isOpen()) {
    if (isLocked()) { n = sizeof(WARN_LIST); return WARN_LIST; }
    n = sizeof(OPEN_LIST); return OPEN_LIST;
  }
  if (isLocked()) { n = sizeof(CLOSED_LIST); return CLOSED_LIST; }
  n = sizeof(NAG_LIST); return NAG_LIST;
}

// Start the playlist for the current state from the top, no fade.
void startPlaylist() {
  uint8_t n;
  listPos = 0;
  item = curList(n)[0];
  fadeLevel = 16;
  bPhase = B_HOLD;
  bTimer = millis();
}

void nextItem() {
  uint8_t n;
  const uint8_t *list = curList(n);
  listPos = (listPos + 1) % n;
  item = list[listPos];
}

void drawBlue() {
  display.fillRect(0, BLUE_Y, 128, BLUE_H, SSD1306_BLACK);
  uint32_t now = millis();
  if (seqActive) {
    drawSeq(now - seqStart);
    return;
  }
  uint32_t t = (bPhase == B_HOLD) ? now - bTimer : (bPhase == B_OUT ? 60000 : 0);

  if (bPhase != B_GAP) {
    switch (item) {
      case I_LOCK: drawLockAnim(t); break;
      case I_MSG: drawMsgScene(t); break;
      case I_POLK: drawCompany(); break;
      case I_UNLOCK: drawUnlockAnim(t); break;
      case I_OPENMSG: drawOpenMsg(); break;
      case I_OPENWORDS: drawOpenWords(t); break;
      case I_NOTLOCKED: drawNotLocked(t); break;
      case I_WARN: drawWarn(); break;
      case I_WIFI: drawWifiScreen(); break;
    }
    applyFade(fadeLevel);
  }

  switch (bPhase) {
    case B_IN:
      if (!itemFades(item)) { fadeLevel = 16; bPhase = B_HOLD; bTimer = now; break; }  // cut in
      if (now - bTimer >= FADE_STEP_MS) {
        bTimer = now;
        if (++fadeLevel >= 16) bPhase = B_HOLD;
      }
      break;
    case B_HOLD:
      if (now - bTimer >= itemHoldMs(item)) { bPhase = B_OUT; bTimer = now; }
      break;
    case B_OUT:
      if (!itemFades(item)) { fadeLevel = 0; bPhase = B_GAP; bTimer = now; break; }  // cut out
      if (now - bTimer >= FADE_STEP_MS) {
        bTimer = now;
        if (fadeLevel == 0 || --fadeLevel == 0) bPhase = B_GAP;
      }
      break;
    case B_GAP:
      if (now - bTimer >= GAP_MS) {
        nextItem();
        fadeLevel = 0;
        bPhase = B_IN;
        bTimer = now;
      }
      break;
  }
}

// ---------- relay ----------

bool pulseRelay() {
  if (relayActive || millis() - relayOffAt < PULSE_GAP_MS) {
    Serial.println(F("Pulse skipped (too soon)"));
    return false;
  }
  digitalWrite(RELAY_PIN, RELAY_ON);
  relayActive = true;
  relayOnAt = millis();
  logEvent("relay_pulse");
  return true;
}

// Called every loop: ends the pulse on time and keeps the coil off otherwise.
void serviceRelay() {
  if (relayActive && millis() - relayOnAt >= PULSE_MS) {
    relayActive = false;
    relayOffAt = millis();
  }
  if (!relayActive) digitalWrite(RELAY_PIN, RELAY_OFF);
}

// "unlock" or "unlock NAME" on a line runs the unlock sequence. Anything else is ignored.
void serviceSerial() {
  static char buf[32];
  static uint8_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (millis() < SERIAL_IGNORE_MS) { len = 0; continue; }
    if (c == '\n' || c == '\r') {
      buf[len] = 0;
      if (len && strcmp(buf, "wifireset") == 0) {
        Serial.println(F("Forgetting saved WiFi, rebooting"));
        wm.resetSettings();
        delay(200);
        ESP.restart();
      }
      if (len && strcmp(buf, "unlock") == 0) startUnlockSeq("", "serial");
      else if (len && strncmp(buf, "unlock ", 7) == 0) startUnlockSeq(buf + 7, "serial");
      len = 0;
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = c;
    } else {
      len = 0;  // too long, not a command
    }
  }
}

// ---------- WiFi + web ----------

void sendCors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, PUT, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "X-Api-Key, Content-Type");
}

const char PAGE[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Cage Lock</title>
<style>body{font-family:system-ui,sans-serif;background:#111;color:#eee;max-width:480px;margin:0 auto;padding:16px}
h1{font-size:1.4rem}h2{font-size:1.1rem;margin:22px 0 6px}#st{font-size:1.6rem;font-weight:700;padding:14px;border-radius:10px;background:#222;text-align:center}
.bad{color:#ffb000}input,button{width:100%;box-sizing:border-box;font-size:1.1rem;padding:12px;margin:6px 0;border-radius:8px;border:1px solid #444;background:#1b1b1b;color:#eee}
button{background:#1f6b4a;border:0;font-weight:700}button.alt{background:#333}button:disabled{opacity:.5}
#msg,#wmsg{min-height:1.4em;color:#aaa}small{color:#888}a{color:#7fc4ff}
table{width:100%;border-collapse:collapse;font-size:.85rem}td{padding:4px 6px;border-bottom:1px solid #333;vertical-align:top}
.tw{overflow-x:auto}</style></head>
<body><h1>Cage Lock</h1><div id="st">...</div><p><small id="sig"></small></p>
<input id="name" placeholder="Name (optional)" maxlength="32">
<button id="go">Unlock</button><div id="msg"></div>
<details><summary><small>API key</small></summary><input id="key" placeholder="API key"></details>
<h2>WiFi</h2>
<button id="wifi" class="alt">Configure WiFi</button><div id="wmsg"></div>
<h2>Log</h2>
<button id="showlog" class="alt">Show recent events</button>
<p><small><a id="dl" href="#">Download full log (CSV)</a></small></p>
<div class="tw"><table id="log"></table></div>
<script>
const $=id=>document.getElementById(id);$('key').value=localStorage.k||'';
const esc=t=>t.replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const key=()=>{localStorage.k=$('key').value;return $('key').value};
$('key').onchange=key;
async function poll(){try{const s=await(await fetch('/status')).json();
$('st').textContent=s.state;$('st').className=s.state=='CLOSED & LOCKED'?'':'bad';
const t=s.time?new Date(s.time*1000).toLocaleString():'clock not synced';
$('sig').textContent='WiFi '+s.wifi+' ('+s.rssi+' dBm) - '+t+(s.relay?' - relay ON':'');}catch(e){$('st').textContent='offline';}}
$('go').onclick=async()=>{$('go').disabled=true;
try{const r=await fetch('/unlock?name='+encodeURIComponent($('name').value),{method:'PUT',headers:{'X-Api-Key':key()}});
const j=await r.json();$('msg').textContent=r.status==202?'Unlocking in '+(j.fires_in_ms/1000).toFixed(1)+' s':
r.status==401?'Wrong API key':'Busy, try again';}catch(e){$('msg').textContent='Error';}
setTimeout(()=>$('go').disabled=false,3000);};
$('wifi').onclick=async()=>{
if(!confirm('The lock will leave this network and start the "CageLock" setup hotspot. Join it, then open http://192.168.4.1 to pick a network. Continue?'))return;
try{const r=await fetch('/wifisetup',{method:'PUT',headers:{'X-Api-Key':key()}});
$('wmsg').textContent=r.status==202?'Hotspot starting: join "CageLock", then open http://192.168.4.1':r.status==401?'Wrong API key':'Error';}
catch(e){$('wmsg').textContent='Error';}};
$('dl').onclick=e=>{e.preventDefault();location.href='/log?key='+encodeURIComponent(key());};
$('showlog').onclick=async()=>{const r=await fetch('/log',{headers:{'X-Api-Key':key()}});
if(r.status!=200){$('log').innerHTML='<tr><td>'+(r.status==401?'Wrong API key':'Error')+'</td></tr>';return;}
const rows=(await r.text()).trim().split('\n').slice(1).slice(-40).reverse();
$('log').innerHTML=rows.map(l=>{const c=l.split(',');const when=c[1]||('boot+'+c[2]+'s');
return '<tr><td>'+esc(when)+'</td><td>'+esc(c[3]||'')+'</td><td>'+esc(c[4]||'')+'</td></tr>'}).join('');};
poll();setInterval(poll,2000);
</script></body></html>)HTML";

void handleRoot() {
  server.send_P(200, "text/html", PAGE);
}

void handleStatus() {
  String j = "{\"door\":\"";
  j += isOpen() ? "open" : "closed";
  j += "\",\"chain\":\"";
  j += isLocked() ? "locked" : "unlocked";
  j += "\",\"state\":\"";
  j += titleText();
  j += "\",\"rssi\":";
  j += WiFi.RSSI();
  j += ",\"wifi\":\"";
  j += wifiQuality();
  j += "\",\"time\":";
  j += timeValid() ? (long)time(nullptr) : 0L;
  j += ",\"relay\":";
  j += relayActive ? "true" : "false";
  j += "}";
  sendCors();
  server.send(200, "application/json", j);
}

// Key from the X-Api-Key header, or ?key= (for plain download links). Logs denials.
bool keyOk(const char *what) {
  if (server.header("X-Api-Key") == API_KEY || server.arg("key") == API_KEY) return true;
  logEvent("unlock_denied", (String(what) + " from " + server.client().remoteIP().toString()).c_str());
  server.send(401, "application/json", "{\"ok\":false,\"error\":\"bad key\"}");
  return false;
}

void handleWifiSetup() {
  sendCors();
  if (!keyOk("wifisetup")) return;
  logEvent("wifi_setup_request", server.client().remoteIP().toString().c_str());
  server.send(202, "application/json", "{\"ok\":true,\"join\":\"CageLock\",\"open\":\"http://192.168.4.1\"}");
  portalRequestAt = millis() + 500;  // let this response go out first
}

// Streams one log file; with since>0 only lines whose epoch >= since.
void sendLogFile(const char *path, long since, bool skipHeader) {
  File f = LittleFS.open(path, "r");
  if (!f) return;
  bool first = true;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (first) { first = false; if (line.startsWith("epoch")) { if (!skipHeader) server.sendContent(line + "\n"); continue; } }
    if (since > 0 && line.toInt() < since) continue;
    server.sendContent(line + "\n");
    yield();
  }
  f.close();
}

void handleLog() {
  sendCors();
  if (!keyOk("log")) return;
  long since = server.arg("since").toInt();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  server.sendContent(LOG_HEADER);
  if (fsOk) {
    sendLogFile(LOG_OLD, since, true);
    sendLogFile(LOG_PATH, since, true);
  }
  server.sendContent("");
}

void handleUnlock() {
  sendCors();
  if (!keyOk("unlock")) return;
  if (startUnlockSeq(server.arg("name").c_str(), "web")) {
    String j = "{\"ok\":true,\"fires_in_ms\":";
    j += (long)seqFireAt();
    j += "}";
    server.send(202, "application/json", j);
  } else {
    server.send(409, "application/json", "{\"ok\":false,\"error\":\"busy\"}");
  }
}

void handlePreflight() {
  sendCors();
  server.send(204);
}

void setupWeb() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);  // networks picked in the setup portal are remembered
  // Saved network (from the portal) wins; secrets.h is the first-boot default.
  if (WiFi.SSID().length()) WiFi.begin();
  else WiFi.begin(WIFI_SSID, WIFI_PASS);  // non-blocking; loop() carries on
  wifiDownSince = millis();

  wm.setConfigPortalBlocking(false);   // display and inputs keep running
  wm.setTitle("Cage Lock WiFi setup");
  wm.setShowInfoUpdate(false);         // no firmware upload from the hotspot
  const char *menu[] = {"wifi", "exit"};
  wm.setMenu(menu, 2);

  const char *keys[] = {"X-Api-Key"};
  server.collectHeaders(keys, 1);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/unlock", HTTP_PUT, handleUnlock);
  server.on("/wifisetup", HTTP_PUT, handleWifiSetup);
  server.on("/wifisetup", HTTP_OPTIONS, handlePreflight);
  server.on("/log", HTTP_GET, handleLog);
  server.on("/log", HTTP_OPTIONS, handlePreflight);
  server.on("/status", HTTP_OPTIONS, handlePreflight);
  server.on("/unlock", HTTP_OPTIONS, handlePreflight);
  server.onNotFound([]() { sendCors(); server.send(404, "text/plain", "not found"); });
  // server.begin() happens once WiFi is up; the setup portal needs port 80 otherwise.
}

void startPortal() {
  if (serverUp) { server.stop(); serverUp = false; }
  // Stop retrying the saved network: each attempt makes the radio hop channels,
  // which knocks phones off the hotspot. (SDK call; keeps the saved config.)
  WiFi.setAutoReconnect(false);
  wifi_station_disconnect();
  logEvent("setup_hotspot", AP_NAME);
  wm.startConfigPortal(AP_NAME, AP_PASS);
  portalSince = millis();
}

void serviceWeb() {
  bool up = WiFi.status() == WL_CONNECTED;
  if (up && !wifiWasUp) {
    logEvent("wifi_connected", (WiFi.SSID() + " " + WiFi.localIP().toString()).c_str());
    if (wm.getConfigPortalActive()) wm.stopConfigPortal();
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    if (!mdnsStarted) mdnsStarted = MDNS.begin(HOSTNAME);
    if (!serverUp) { server.begin(); serverUp = true; }
    everConnected = true;
  } else if (!up && wifiWasUp) {
    logEvent("wifi_lost");
    wifiDownSince = millis();
  }
  wifiWasUp = up;

  if (wm.getConfigPortalActive()) {
    wm.process();
    // Nobody on the hotspot for a while: step aside and retry the saved WiFi.
    if (millis() - portalSince >= PORTAL_RETRY_MS && WiFi.softAPgetStationNum() == 0) {
      Serial.println(F("Setup hotspot idle - retrying saved WiFi"));
      wm.stopConfigPortal();
      WiFi.mode(WIFI_STA);
      WiFi.setAutoReconnect(true);
      WiFi.begin();
      wifiDownSince = millis();
    }
  } else if (!up && millis() - wifiDownSince >= (everConnected ? PORTAL_AFTER_DROP_MS : PORTAL_AFTER_BOOT_MS)) {
    startPortal();
  }

  if (portalRequestAt && (int32_t)(millis() - portalRequestAt) >= 0) {
    portalRequestAt = 0;
    if (!wm.getConfigPortalActive()) startPortal();
  }
  if (!timeLogged && timeValid()) {
    timeLogged = true;
    logEvent("time_sync");
  }
  if (mdnsStarted) MDNS.update();
  if (serverUp) server.handleClient();
}

// ---------- main ----------

void setup() {
  // Latch HIGH before switching to OUTPUT so the relay doesn't click at boot.
  digitalWrite(RELAY_PIN, RELAY_OFF);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);
  relayOffAt = millis() - PULSE_GAP_MS;

  pinMode(REED_PIN, INPUT_PULLUP);
  pinMode(LOCK_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);

  Wire.begin(D2, D1);  // SDA, SCL
  Wire.setClock(400000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("\nSSD1306 not found at 0x3C - check wiring"));
    while (true) delay(1000);
  }
  Serial.println(F("\nCage Lock"));
  fsOk = LittleFS.begin();
  configTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");  // syncs once WiFi is up
  logEvent("boot", ESP.getResetReason().c_str());
  setupWeb();
  display.setTextWrap(false);
  display.clearDisplay();

  reedStable = reedLast = digitalRead(REED_PIN);
  lockStable = lockLast = digitalRead(LOCK_PIN);
  digitalWrite(LED_PIN, reedStable);
  Serial.println(stateText());
  startPlaylist();
}

void loop() {
  serviceSerial();
  serviceWeb();
  serviceSeq();
  serviceRelay();

  // Door reed, debounced. LED on (LOW) when closed.
  int r = digitalRead(REED_PIN);
  if (r != reedLast) {
    reedLast = r;
    reedChange = millis();
  }
  if (millis() - reedChange >= DEBOUNCE_MS && r != reedStable) {
    reedStable = r;
    digitalWrite(LED_PIN, reedStable);
    logEvent("door", isOpen() ? "open" : "closed");
    resetTitle();
    startPlaylist();
  }

  // Lock feedback, debounced.
  int l = digitalRead(LOCK_PIN);
  if (l != lockLast) {
    lockLast = l;
    lockChange = millis();
  }
  if (millis() - lockChange >= DEBOUNCE_MS && l != lockStable) {
    lockStable = l;
    logEvent("chain", isLocked() ? "locked" : "unlocked");
    startPlaylist();
  }

  if (millis() - lastFrame >= FRAME_MS) {
    lastFrame = millis();
    drawBlue();   // first, so the band covers anything that slid above row 16
    drawTitle();
    display.display();
  }
}
