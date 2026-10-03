// Cage Lock main sketch.
// Yellow band (rows 0-15):
//   Reed closed: "Cage Closed" scrolls continuously.
//   Reed open:   "Cage OPEN" centered, flashing normal <-> inverted.
// Blue area (rows 16-63): each screen fades in, holds, fades out, in order:
//   inverted JP logo -> JP + padlock locking -> "Need something?" -> "Call your Manager"
//   -> "Polk Production Technologies" -> repeat.
// On-board LED mirrors the reed: on = closed.
// Libraries: Adafruit SSD1306, Adafruit GFX Library.
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include "logo.h"

const uint8_t REED_PIN = D7;         // window reed to GND, LOW = closed
const uint8_t LED_PIN = LED_BUILTIN; // D4/GPIO2, active LOW
const uint16_t FRAME_MS = 25;
const uint16_t DEBOUNCE_MS = 50;

// Title timing
const uint8_t SCROLL_PX = 2;         // px per frame
const uint16_t FLASH_MS = 300;       // open: time per normal/inverted half

// Logo timing
const uint16_t FADE_STEP_MS = 50;    // 16 steps each way
const uint16_t LOGO_HOLD_MS = 1000;
const uint16_t LOCK_HOLD_MS = 2200;  // lock animation runs inside this
const uint16_t MSG_HOLD_MS = 2000;   // "Need something?" / "Call your Manager"
const uint16_t TEXT_HOLD_MS = 3000;  // company name
const uint16_t GAP_MS = 300;         // blank between fades

const int16_t BAND_H = 16, BLUE_Y = 16, BLUE_H = 48;
const int16_t LOGO_X = (128 - JP_W) / 2;
const int16_t LOGO_Y = BLUE_Y + (BLUE_H - JP_H) / 2;

const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

Adafruit_SSD1306 display(128, 64, &Wire, -1);

int reedStable = -1, reedLast = -1;
uint32_t reedChange = 0, lastFrame = 0;

int16_t scrollX = 128;
bool flashInv = false;
uint32_t tTimer = 0;

// Blue area cycle: each item fades in, holds, fades out, then a short gap.
enum BluePhase { B_IN, B_HOLD, B_OUT, B_GAP };
BluePhase bPhase = B_IN;
enum BlueItem { I_LOGO, I_LOCK, I_NEED, I_CALL, I_POLK, I_COUNT };
uint8_t item = I_LOGO;
uint8_t fadeLevel = 0;               // 0 = blank, 16 = fully drawn
uint32_t bTimer = 0;

bool isOpen() { return reedStable == HIGH; }
const char *titleText() { return isOpen() ? "Cage OPEN" : "Cage Closed"; }

// Size-2 text with a narrow 8 px space so "Cage Closed" fits in 128 px.
int16_t titleWidth(const char *s) {
  int16_t w = 0;
  for (; *s; s++) w += (*s == ' ') ? 8 : 12;
  return w - 2;  // drop trailing gap
}

void printTitle(const char *s, int16_t x, uint16_t color) {
  display.setTextSize(2);
  display.setTextColor(color);
  for (; *s; s++) {
    if (*s == ' ') { x += 8; continue; }
    display.setCursor(x, 0);
    display.print(*s);
    x += 12;
  }
}

// ---------- yellow band ----------

void resetTitle() {
  scrollX = 128;
  flashInv = false;
  tTimer = millis();
}

void drawTitle() {
  const char *t = titleText();
  int16_t w = titleWidth(t);
  display.setFont(NULL);

  if (!isOpen()) {
    // Closed: continuous scroll
    display.fillRect(0, 0, 128, BAND_H, SSD1306_BLACK);
    printTitle(t, scrollX, SSD1306_WHITE);
    scrollX -= SCROLL_PX;
    if (scrollX < -w) scrollX = 128;
    return;
  }

  // Open: centered, flash between normal and inverted
  display.fillRect(0, 0, 128, BAND_H, flashInv ? SSD1306_WHITE : SSD1306_BLACK);
  printTitle(t, (128 - w) / 2, flashInv ? SSD1306_BLACK : SSD1306_WHITE);
  if (millis() - tTimer >= FLASH_MS) {
    tTimer = millis();
    flashInv = !flashInv;
  }
}

// ---------- blue area ----------

void printCentered(const char *s, int16_t baseline) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(s, 0, baseline, &x1, &y1, &w, &h);
  display.setCursor((128 - (int16_t)w) / 2 - x1, baseline);
  display.print(s);
}

// JP logo + padlock, inverted style. t = ms into the animation.
//   0-400 ms: shackle raised (open)
//   400-800 ms: shackle drops into the body
//   800-950 ms: "click" flash (colors swap)
void drawLockScene(uint32_t t) {
  const int16_t GAP = 12, BODY_W = 26, BODY_H = 20;
  const int16_t lx = (128 - (JP_W + GAP + BODY_W)) / 2;
  const int16_t bx = lx + JP_W + GAP, by = 43;
  const int16_t LIFT_MAX = 10;

  int16_t lift = LIFT_MAX;
  if (t >= 800) lift = 0;
  else if (t >= 400) {
    float p = (t - 400) / 400.0;
    lift = LIFT_MAX * (1 - p * p);  // accelerate like it's falling
  }
  bool flash = (t >= 800 && t < 950);
  uint16_t bg = flash ? SSD1306_BLACK : SSD1306_WHITE;
  uint16_t fg = flash ? SSD1306_WHITE : SSD1306_BLACK;

  display.fillRect(0, BLUE_Y, 128, BLUE_H, bg);
  display.drawBitmap(lx, LOGO_Y, JP_BMP, JP_W, JP_H, fg);

  // Shackle: hollow U, legs hidden in the body when closed
  int16_t sx = bx + 3, sy = by - 16 - lift;
  display.fillRoundRect(sx, sy, 20, 22, 10, fg);
  display.fillRoundRect(sx + 4, sy + 4, 12, 22, 6, bg);
  // Body + keyhole
  display.fillRoundRect(bx, by, BODY_W, BODY_H, 3, fg);
  display.fillCircle(bx + 13, by + 7, 3, bg);
  display.fillRect(bx + 12, by + 8, 3, 7, bg);
}

// Two lines of 12pt text; baselines fit cap height + descenders in rows 16-63.
void drawMessage(const GFXfont *font, const char *l1, const char *l2) {
  display.setFont(font);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  printCentered(l1, 33);
  printCentered(l2, 57);
  display.setFont(NULL);
}

void drawCompany() {
  // FreeSans Bold 9pt is the largest font where "Technologies" fits 128 px.
  // Baselines leave room for the "g" descender on the last line.
  display.setFont(&FreeSansBold9pt7b);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  printCentered("Polk", 28);
  printCentered("Production", 43);
  printCentered("Technologies", 58);
  display.setFont(NULL);
}

// Inverted logo: lit blue area with the logo cut out.
void drawLogoInv() {
  display.fillRect(0, BLUE_Y, 128, BLUE_H, SSD1306_WHITE);
  display.drawBitmap(LOGO_X, LOGO_Y, JP_BMP, JP_W, JP_H, SSD1306_BLACK);
}

// Ordered dither: clear pixels above the current level so the area "fades".
void applyFade(uint8_t level) {
  if (level >= 16) return;
  for (int16_t y = BLUE_Y; y < BLUE_Y + BLUE_H; y++)
    for (int16_t x = 0; x < 128; x++)
      if (BAYER[y & 3][x & 3] >= level) display.drawPixel(x, y, SSD1306_BLACK);
}

void drawBlue() {
  display.fillRect(0, BLUE_Y, 128, BLUE_H, SSD1306_BLACK);
  uint32_t now = millis();

  if (bPhase != B_GAP) {
    switch (item) {
      case I_LOGO: drawLogoInv(); break;
      case I_LOCK: drawLockScene(bPhase == B_IN ? 0 : bPhase == B_HOLD ? now - bTimer : 60000); break;
      case I_NEED: drawMessage(&FreeSans12pt7b, "Need", "something?"); break;
      case I_CALL: drawMessage(&FreeSansBold12pt7b, "Call your", "Manager"); break;
      case I_POLK: drawCompany(); break;
    }
    applyFade(fadeLevel);
  }

  switch (bPhase) {
    case B_IN:
      if (now - bTimer >= FADE_STEP_MS) {
        bTimer = now;
        if (++fadeLevel >= 16) bPhase = B_HOLD;
      }
      break;
    case B_HOLD:
      if (now - bTimer >= (item == I_LOGO ? LOGO_HOLD_MS : item == I_LOCK ? LOCK_HOLD_MS :
                         item == I_POLK ? TEXT_HOLD_MS : MSG_HOLD_MS)) {
        bPhase = B_OUT;
        bTimer = now;
      }
      break;
    case B_OUT:
      if (now - bTimer >= FADE_STEP_MS) {
        bTimer = now;
        if (fadeLevel == 0 || --fadeLevel == 0) bPhase = B_GAP;
      }
      break;
    case B_GAP:
      if (now - bTimer >= GAP_MS) {
        item = (item + 1) % I_COUNT;
        fadeLevel = 0;
        bPhase = B_IN;
        bTimer = now;
      }
      break;
  }
}

// ---------- main ----------

void setup() {
  pinMode(REED_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);

  Wire.begin(D2, D1);  // SDA, SCL
  Wire.setClock(400000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("\nSSD1306 not found at 0x3C - check wiring"));
    while (true) delay(1000);
  }
  Serial.println(F("\nCage Lock"));
  display.setTextWrap(false);
  display.clearDisplay();

  reedStable = reedLast = digitalRead(REED_PIN);
  digitalWrite(LED_PIN, reedStable);
  Serial.println(titleText());
  bTimer = millis();
}

void loop() {
  // Window reed, debounced. LED on (LOW) when closed.
  int r = digitalRead(REED_PIN);
  if (r != reedLast) {
    reedLast = r;
    reedChange = millis();
  }
  if (millis() - reedChange >= DEBOUNCE_MS && r != reedStable) {
    reedStable = r;
    digitalWrite(LED_PIN, reedStable);
    Serial.println(titleText());
    resetTitle();
  }

  if (millis() - lastFrame >= FRAME_MS) {
    lastFrame = millis();
    drawBlue();   // first, so the band covers anything that slid above row 16
    drawTitle();
    display.display();
  }
}
