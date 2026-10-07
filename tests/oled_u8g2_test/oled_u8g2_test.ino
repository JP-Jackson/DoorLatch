// SSD1309 128x64 I2C test (U8g2), address 0x3C, SCL D1 (GPIO5), SDA D2 (GPIO4).
// Compares pre-charge (0xD9) / VCOMH (0xDB) settings on a full-white screen.
// Per combo (3 s): solid white screen with the combo letter in large black text.
// Loops 0,A,B,C,D.
// "0" is the baseline: re-init with defaults (no 0xD9/0xDB), contrast 128.
// Library: U8g2.
#include <Wire.h>
#include <U8g2lib.h>

U8G2_SSD1309_128X64_NONAME0_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

struct Combo { char id; uint8_t precharge, vcomh; };  // precharge == 0: defaults
const Combo COMBOS[] = {{'0', 0, 0}, {'A', 0xF1, 0x34}, {'B', 0xF1, 0x20}, {'C', 0x22, 0x34}, {'D', 0x22, 0x20}};
const uint8_t NCOMBO = sizeof(COMBOS) / sizeof(COMBOS[0]);
const uint16_t SHOW_MS = 3000;
uint8_t combo = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin(D2, D1);  // SDA, SCL
  oled.setI2CAddress(0x3C << 1);
  Serial.println(F("\nU8g2 SSD1309 started"));
}

void loop() {
  const Combo &c = COMBOS[combo];
  oled.begin();  // re-init so each combo starts from the panel defaults
  oled.setContrast(128);
  if (c.precharge) {
    oled.sendF("ca", 0xD9, c.precharge);
    oled.sendF("ca", 0xDB, c.vcomh);
  }
  Serial.printf("%c: D9=%s DB=%s\n", c.id, c.precharge ? String(c.precharge, HEX).c_str() : "default",
                c.precharge ? String(c.vcomh, HEX).c_str() : "default");

  oled.clearBuffer();
  oled.drawBox(0, 0, 128, 64);
  oled.setDrawColor(0);
  oled.setFont(u8g2_font_logisoso42_tr);
  oled.drawStr((128 - oled.getStrWidth(String(c.id).c_str())) / 2, 54, String(c.id).c_str());
  oled.setDrawColor(1);
  oled.sendBuffer();
  delay(SHOW_MS);

  combo = (combo + 1) % NCOMBO;
}
