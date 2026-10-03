// OLED test: SSD1306 128x64 at 0x3C, SCL D1 (GPIO5), SDA D2 (GPIO4).
// Two-color panel: rows 0-15 yellow, 16-63 blue.
// Shows "Cage Lock" title, JP monogram, and lock state from D6.
// Libraries: Adafruit SSD1306, Adafruit GFX Library.
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const uint8_t LOCK_PIN = D6;         // lock status switch to GND, INPUT_PULLUP
const uint8_t LOCKED_LEVEL = LOW;    // flip if the switch reads backwards

Adafruit_SSD1306 display(128, 64, &Wire, -1);
int shownState = -1;

// JP monogram, 32x32, drawn with primitives at (x, y).
void drawJP(int16_t x, int16_t y) {
  const uint16_t W = SSD1306_WHITE, B = SSD1306_BLACK;
  // J hook: ring, then blank its top half
  display.fillCircle(x + 8, y + 23, 8, W);
  display.fillCircle(x + 8, y + 23, 4, B);
  display.fillRect(x, y + 15, 17, 8, B);
  // J top bar + stem
  display.fillRect(x + 2, y, 14, 4, W);
  display.fillRect(x + 12, y, 4, 24, W);
  // P bowl + stem
  display.fillRoundRect(x + 19, y, 13, 18, 6, W);
  display.fillRoundRect(x + 23, y + 4, 5, 10, 2, B);
  display.fillRect(x + 19, y, 4, 32, W);
}

void drawScreen(bool locked) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Title in the yellow band
  display.setTextSize(2);
  display.setCursor(10, 0);  // 9 chars * 12 px = 108 px wide
  display.print(F("Cage Lock"));

  // Icon centered in the blue area
  drawJP(48, 18);

  // State below the icon
  const char *txt = locked ? "LOCKED" : "UNLOCKED";
  display.setTextSize(1);
  display.setCursor((128 - strlen(txt) * 6) / 2, 55);
  display.print(txt);

  display.display();
}

void setup() {
  Serial.begin(115200);
  pinMode(LOCK_PIN, INPUT_PULLUP);
  Wire.begin(D2, D1);  // SDA, SCL
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("\nSSD1306 not found at 0x3C - check wiring"));
    while (true) delay(1000);
  }
  Serial.println(F("\nOLED OK"));
}

void loop() {
  int s = digitalRead(LOCK_PIN);
  if (s != shownState) {
    shownState = s;
    bool locked = (s == LOCKED_LEVEL);
    Serial.println(locked ? F("LOCKED") : F("UNLOCKED"));
    drawScreen(locked);
  }
  delay(50);
}
