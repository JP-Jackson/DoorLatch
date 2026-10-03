// Reed switch test: window reed on D7 (GPIO13) to GND, INPUT_PULLUP.
// LOW = closed (magnet present), HIGH = open.
const uint8_t REED_PIN = D7;
const uint16_t DEBOUNCE_MS = 50;

int stableState = -1;
int lastRead = -1;
uint32_t lastChange = 0;

void setup() {
  Serial.begin(115200);
  pinMode(REED_PIN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);  // on-board LED mirrors state (LOW = lit)
  Serial.println(F("\nReed test: move the magnet"));
}

void loop() {
  int r = digitalRead(REED_PIN);
  if (r != lastRead) {
    lastRead = r;
    lastChange = millis();
  }
  if (millis() - lastChange >= DEBOUNCE_MS && r != stableState) {
    stableState = r;
    Serial.println(stableState == LOW ? F("Window CLOSED") : F("Window OPEN"));
    digitalWrite(LED_BUILTIN, stableState);
  }
}
