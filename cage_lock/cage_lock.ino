// Cage Lock main sketch.
// Boot: JP logo animation (1 of 4) + credit scroll, then status screen.
// Status: yellow band scrolls Cage Locked/Unlocked (D6),
//         blue area shows logo + window OPEN/CLOSED (D7 reed).
// On-board LED mirrors the reed: on = closed.
// Serial 115200: send 1-4 to replay that animation.
// Libraries: Adafruit SSD1306, Adafruit GFX Library.
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "logo.h"

const uint8_t REED_PIN = D7;         // window reed to GND, LOW = closed
const uint8_t LOCK_PIN = D6;         // lock status switch to GND (not wired yet)
const uint8_t LED_PIN = LED_BUILTIN; // D4/GPIO2, active LOW
const uint8_t LOCKED_LEVEL = LOW;    // flip if the lock switch reads backwards
const uint8_t BOOT_ANIM = 0;         // 0 = random each boot, 1-4 = fixed
const uint16_t SCROLL_MS = 30;       // title: ms per 2 px step
const uint16_t DEBOUNCE_MS = 50;

const char CREDIT[] = "Designed by JP for Polk Production Technologies, Inc.";

// Splash centers the logo; status screen puts it on the left.
const int16_t SPLASH_X = (128 - JP_W) / 2, LOGO_Y = 19;
const int16_t STATUS_X = 2;

Adafruit_SSD1306 display(128, 64, &Wire, -1);

int lockShown = -1, reedStable = -1, reedLast = -1;
uint32_t reedChange = 0, lastStep = 0;
const char *title = "";
int16_t scrollX = 128;

bool logoPx(int16_t x, int16_t y) {
  return pgm_read_byte(&JP_BMP[y * JP_BW + x / 8]) & (0x80 >> (x & 7));
}

void drawLogo(int16_t x, int16_t y) {
  display.drawBitmap(x, y, JP_BMP, JP_W, JP_H, SSD1306_WHITE);
}

// ---------- boot animations ----------

// 1: wipe in left to right with a leading edge line
void animWipe() {
  for (int16_t c = 0; c <= JP_W; c += 2) {
    display.clearDisplay();
    drawLogo(SPLASH_X, LOGO_Y);
    display.fillRect(SPLASH_X + c, LOGO_Y, JP_W - c, JP_H, SSD1306_BLACK);
    if (c < JP_W) display.drawFastVLine(SPLASH_X + c, LOGO_Y - 2, JP_H + 4, SSD1306_WHITE);
    display.display();
    delay(15);
  }
}

// 2: drop from the top and bounce
void animDrop() {
  float y = -JP_H, v = 0;
  while (true) {
    v += 0.9;
    y += v;
    if (y >= LOGO_Y) {
      y = LOGO_Y;
      v = -v * 0.45;
      if (v > -1.5) break;
    }
    display.clearDisplay();
    drawLogo(SPLASH_X, (int16_t)y);
    display.display();
    delay(15);
  }
  display.clearDisplay();
  drawLogo(SPLASH_X, LOGO_Y);
  display.display();
}

// 3: pixel dissolve in pseudo-random order
void animDissolve() {
  const uint16_t N = JP_W * JP_H;
  const uint16_t STEP = 1009;  // prime, coprime with N, so every pixel is hit once
  display.clearDisplay();
  uint16_t p = 0;
  for (uint16_t i = 0; i < N; i++) {
    p = (p + STEP) % N;
    int16_t x = p % JP_W, y = p / JP_W;
    if (logoPx(x, y)) display.drawPixel(SPLASH_X + x, LOGO_Y + y, SSD1306_WHITE);
    if (i % 40 == 0) display.display();
  }
  display.display();
}

// 4: scanline draws the logo top to bottom
void animScan() {
  for (int16_t r = 0; r <= JP_H; r += 2) {
    display.clearDisplay();
    drawLogo(SPLASH_X, LOGO_Y);
    display.fillRect(SPLASH_X, LOGO_Y + r, JP_W, JP_H - r, SSD1306_BLACK);
    if (r < JP_H) display.drawFastHLine(SPLASH_X - 6, LOGO_Y + r, JP_W + 12, SSD1306_WHITE);
    display.display();
    delay(20);
  }
}

void creditScroll() {
  int16_t w = strlen(CREDIT) * 12;
  display.setTextSize(2);
  for (int16_t x = 128; x > -w; x -= 3) {
    display.fillRect(0, 0, 128, 16, SSD1306_BLACK);
    display.setCursor(x, 0);
    display.print(CREDIT);
    display.display();
    delay(20);
  }
}

void splash(uint8_t n) {
  if (n < 1 || n > 4) n = random(1, 5);
  Serial.printf("Animation %u\n", n);
  switch (n) {
    case 1: animWipe(); break;
    case 2: animDrop(); break;
    case 3: animDissolve(); break;
    case 4: animScan(); break;
  }
  for (uint8_t i = 0; i < 2; i++) {  // flash
    display.invertDisplay(true);  delay(100);
    display.invertDisplay(false); delay(100);
  }
  creditScroll();
}

// ---------- status screen ----------

void drawStatusArea() {
  display.fillRect(0, 16, 128, 48, SSD1306_BLACK);
  drawLogo(STATUS_X, LOGO_Y);
  display.setTextSize(1);
  display.setCursor(54, 22);
  display.print(F("Window"));
  display.setTextSize(2);
  display.setCursor(54, 36);
  display.print(reedStable == LOW ? F("CLOSED") : F("OPEN"));
}

void drawTitle() {
  display.fillRect(0, 0, 128, 16, SSD1306_BLACK);
  display.setTextSize(2);
  display.setCursor(scrollX, 0);
  display.print(title);
  display.display();
}

void showStatus() {
  display.clearDisplay();
  scrollX = 128;
  drawStatusArea();
  drawTitle();
}

void setup() {
  pinMode(REED_PIN, INPUT_PULLUP);
  pinMode(LOCK_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  randomSeed(ESP.random());

  Wire.begin(D2, D1);  // SDA, SCL
  Wire.setClock(400000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("\nSSD1306 not found at 0x3C - check wiring"));
    while (true) delay(1000);
  }
  Serial.println(F("\nCage Lock"));
  display.setTextWrap(false);
  display.setTextColor(SSD1306_WHITE);

  reedStable = reedLast = digitalRead(REED_PIN);
  digitalWrite(LED_PIN, reedStable);
  splash(BOOT_ANIM);
  showStatus();
}

void loop() {
  // Replay an animation on request
  if (Serial.available()) {
    char c = Serial.read();
    if (c >= '1' && c <= '4') {
      splash(c - '0');
      showStatus();
    }
  }

  // Window reed, debounced. LED on (LOW) when closed.
  int r = digitalRead(REED_PIN);
  if (r != reedLast) {
    reedLast = r;
    reedChange = millis();
  }
  if (millis() - reedChange >= DEBOUNCE_MS && r != reedStable) {
    reedStable = r;
    digitalWrite(LED_PIN, reedStable);
    Serial.println(reedStable == LOW ? F("Window CLOSED") : F("Window OPEN"));
    drawStatusArea();
  }

  // Lock state sets the scrolling title
  int s = digitalRead(LOCK_PIN);
  if (s != lockShown) {
    lockShown = s;
    title = (s == LOCKED_LEVEL) ? "Cage Locked" : "Cage Unlocked";
    scrollX = 128;
    Serial.println(title);
  }

  if (millis() - lastStep >= SCROLL_MS) {
    lastStep = millis();
    scrollX -= 2;
    if (scrollX < -(int16_t)(strlen(title) * 12)) scrollX = 128;
    drawTitle();
  }
}
