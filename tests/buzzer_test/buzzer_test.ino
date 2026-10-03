// Buzzer test: passive piezo on D8 (GPIO15) through 100 ohm.
// D8 is a boot strap pin (must be LOW at boot) - the board's pulldown handles it.
const uint8_t BUZZER_PIN = D8;

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.println(F("\nBuzzer test: chirp every 3 s"));
}

void loop() {
  tone(BUZZER_PIN, 2000, 150);
  delay(250);
  tone(BUZZER_PIN, 2600, 150);
  delay(250);
  noTone(BUZZER_PIN);
  digitalWrite(BUZZER_PIN, LOW);
  delay(3000);
}
