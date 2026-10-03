// Cage Lock main sketch.
// Yellow band (rows 0-15): "Cage Locked" (reed closed) / "Cage Open" (reed open).
//   Scrolls across once, then blinks centered, repeat.
//   Open blinks by swapping text/background to grab attention.
// Blue area (rows 16-63): JP logo fades in, holds 1 s, slides up to reveal
//   "Polk Production Technologies, Inc.", holds, repeat.
// On-board LED mirrors the reed: on = closed.
// Libraries: Adafruit SSD1306, Adafruit GFX Library.
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "logo.h"

const uint8_t REED_PIN = D7;         // window reed to GND, LOW = closed
const uint8_t LED_PIN = LED_BUILTIN; // D4/GPIO2, active LOW
const uint16_t FRAME_MS = 25;
const uint16_t DEBOUNCE_MS = 50;

// Title timing
const uint8_t SCROLL_PX = 2;         // px per frame
const uint16_t BLINK_MS = 300;       // per on/off half
const uint8_t BLINKS = 3;            // full on/off cycles

// Logo timing
const uint16_t FADE_STEP_MS = 50;    // 16 steps
const uint16_t LOGO_HOLD_MS = 1000;
const uint16_t TEXT_HOLD_MS = 3000;

const int16_t BAND_H = 16, BLUE_Y = 16, BLUE_H = 48;
const int16_t LOGO_X = (128 - JP_W) / 2;
const int16_t LOGO_Y = BLUE_Y + (BLUE_H - JP_H) / 2;
const int16_t TEXT_GAP = 14;        // sized so the logo fully clears the blue area
const int16_t TEXT_H = 20;           // two lines of 8 px + 4 px gap
// Slide until the two text lines are centered in the blue area
const int16_t SLIDE_MAX = (LOGO_Y + JP_H + TEXT_GAP) - (BLUE_Y + (BLUE_H - TEXT_H) / 2);

const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

Adafruit_SSD1306 display(128, 64, &Wire, -1);

int reedStable = -1, reedLast = -1;
uint32_t reedChange = 0, lastFrame = 0;

enum TitlePhase { T_SCROLL, T_BLINK };
TitlePhase tPhase = T_SCROLL;
int16_t scrollX = 128;
uint8_t blinkHalf = 0;
uint32_t tTimer = 0;

enum LogoPhase { L_FADE, L_HOLD, L_SLIDE, L_TEXT };
LogoPhase lPhase = L_FADE;
uint8_t fadeLevel = 0;
int16_t slide = 0;
uint32_t lTimer = 0;

bool isOpen() { return reedStable == HIGH; }
const char *titleText() { return isOpen() ? "Cage Open" : "Cage Locked"; }

// Size-2 text with a narrow 8 px space so "Cage Locked" fits in 128 px.
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
  tPhase = T_SCROLL;
  scrollX = 128;
}

void drawTitle() {
  const char *t = titleText();
  int16_t w = titleWidth(t);
  display.fillRect(0, 0, 128, BAND_H, SSD1306_BLACK);

  if (tPhase == T_SCROLL) {
    printTitle(t, scrollX, SSD1306_WHITE);
    scrollX -= SCROLL_PX;
    if (scrollX < -w) {
      tPhase = T_BLINK;
      blinkHalf = 0;
      tTimer = millis();
    }
    return;
  }

  // T_BLINK: even halves = "on", odd = "off"
  int16_t x = (128 - w) / 2;
  bool on = (blinkHalf % 2) == 0;
  if (isOpen()) {
    // swap foreground/background
    if (on) {
      printTitle(t, x, SSD1306_WHITE);
    } else {
      display.fillRect(0, 0, 128, BAND_H, SSD1306_WHITE);
      printTitle(t, x, SSD1306_BLACK);
    }
  } else if (on) {
    printTitle(t, x, SSD1306_WHITE);
  }
  if (millis() - tTimer >= BLINK_MS) {
    tTimer = millis();
    if (++blinkHalf >= BLINKS * 2) resetTitle();
  }
}

// ---------- blue area ----------

void drawLogoFade(uint8_t level) {
  for (int16_t y = 0; y < JP_H; y++)
    for (int16_t x = 0; x < JP_W; x++)
      if (BAYER[y & 3][x & 3] < level &&
          (pgm_read_byte(&JP_BMP[y * JP_BW + x / 8]) & (0x80 >> (x & 7))))
        display.drawPixel(LOGO_X + x, LOGO_Y + y, SSD1306_WHITE);
}

void printCentered(const char *s, int16_t y) {
  display.setCursor((128 - (int16_t)strlen(s) * 6) / 2, y);
  display.print(s);
}

void drawCompany(int16_t top) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  printCentered("Polk Production", top);
  printCentered("Technologies, Inc.", top + 12);
}

void drawBlue() {
  display.fillRect(0, BLUE_Y, 128, BLUE_H, SSD1306_BLACK);
  uint32_t now = millis();

  switch (lPhase) {
    case L_FADE:
      drawLogoFade(fadeLevel);
      if (now - lTimer >= FADE_STEP_MS) {
        lTimer = now;
        if (++fadeLevel >= 16) { lPhase = L_HOLD; lTimer = now; }
      }
      break;
    case L_HOLD:
      display.drawBitmap(LOGO_X, LOGO_Y, JP_BMP, JP_W, JP_H, SSD1306_WHITE);
      if (now - lTimer >= LOGO_HOLD_MS) { lPhase = L_SLIDE; slide = 0; }
      break;
    case L_SLIDE:
    case L_TEXT:
      // Anything above row 16 gets covered by the title band.
      display.drawBitmap(LOGO_X, LOGO_Y - slide, JP_BMP, JP_W, JP_H, SSD1306_WHITE);
      drawCompany(LOGO_Y + JP_H + TEXT_GAP - slide);
      if (lPhase == L_SLIDE) {
        if (++slide >= SLIDE_MAX) { slide = SLIDE_MAX; lPhase = L_TEXT; lTimer = now; }
      } else if (now - lTimer >= TEXT_HOLD_MS) {
        lPhase = L_FADE; fadeLevel = 0; lTimer = now;
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
  lTimer = millis();
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
