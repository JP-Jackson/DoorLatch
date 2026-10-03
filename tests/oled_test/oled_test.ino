// OLED test: SSD1306 128x64 at 0x3C, SCL D1 (GPIO5), SDA D2 (GPIO4).
// Libraries: Adafruit SSD1306, Adafruit GFX Library.
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

Adafruit_SSD1306 display(128, 64, &Wire, -1);

void setup() {
  Serial.begin(115200);
  Wire.begin(D2, D1);  // SDA, SCL
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("\nSSD1306 not found at 0x3C - check wiring"));
    while (true) delay(1000);
  }
  Serial.println(F("\nOLED OK"));
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println(F("Door Lock"));
  display.setTextSize(1);
  display.println(F("OLED test"));
  display.display();
}

void loop() {
  display.fillRect(0, 40, 128, 24, SSD1306_BLACK);
  display.setCursor(0, 48);
  display.print(F("Uptime: "));
  display.print(millis() / 1000);
  display.print(F(" s"));
  display.display();
  delay(1000);
}
