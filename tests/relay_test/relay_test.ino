// Relay test: IN1 on D5 (GPIO14), active LOW.
// Drives a 12V pulse-type lock: pulse 500 ms max, never hold on.
// Send 'p' in the serial monitor (115200) to fire one pulse.
const uint8_t RELAY_PIN = D5;
const uint8_t RELAY_ON = LOW;
const uint8_t RELAY_OFF = HIGH;
const uint16_t PULSE_MS = 500;        // hard max - do not raise
const uint32_t MIN_GAP_MS = 2000;     // let the solenoid cool between pulses

uint32_t lastPulse = 0;

void pulseRelay() {
  if (millis() - lastPulse < MIN_GAP_MS) {
    Serial.println(F("Too soon, skipped"));
    return;
  }
  Serial.println(F("Pulse"));
  digitalWrite(RELAY_PIN, RELAY_ON);
  delay(PULSE_MS);
  digitalWrite(RELAY_PIN, RELAY_OFF);
  lastPulse = millis();
}

void setup() {
  // Latch HIGH before switching to OUTPUT so the relay doesn't click at boot.
  digitalWrite(RELAY_PIN, RELAY_OFF);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);
  Serial.begin(115200);
  Serial.println(F("\nRelay test: send 'p' to pulse"));
}

void loop() {
  if (Serial.available() && Serial.read() == 'p') pulseRelay();
  // Safety net: never leave the coil energized.
  digitalWrite(RELAY_PIN, RELAY_OFF);
}
