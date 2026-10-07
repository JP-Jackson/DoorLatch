// MQTT test: proves the ESP8266 can hold a verified TLS MQTT connection to EMQX
// Cloud with enough memory left over, and receive messages fast.
// Serial 115200 prints: free heap before/after TLS, connect time, MFLN support,
// every message on cage/cage-01/cmd, and free heap every 30 s.
// The OLED shows connection state and the last message.
// Publish a test message from the EMQX console (Online Test / WebSocket client)
// to topic cage/cage-01/cmd and watch it arrive.
// Libraries: PubSubClient (Nick O'Leary), Adafruit SSD1306, Adafruit GFX.
// Needs secrets.h (copy secrets.h.example).
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
#include "secrets.h"

const char TOPIC_CMD[] = "cage/cage-01/cmd";
const char TOPIC_STATUS[] = "cage/cage-01/status";

Adafruit_SSD1306 display(128, 64, &Wire, -1);
BearSSL::WiFiClientSecure net;
BearSSL::X509List ca(MQTT_CA);
PubSubClient mqtt(net);

uint32_t lastHeap = 0, lastTry = 0, msgCount = 0;
char lastMsg[64] = "(none yet)";

void show(const char *l1, const char *l2 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("MQTT test"));
  display.println(l1);
  display.println();
  display.println(F("Last msg:"));
  display.println(lastMsg);
  display.setCursor(0, 56);
  display.print(l2);
  display.display();
}

void onMessage(char *topic, byte *payload, unsigned int len) {
  msgCount++;
  uint8_t n = len < sizeof(lastMsg) - 1 ? len : sizeof(lastMsg) - 1;
  memcpy(lastMsg, payload, n);
  lastMsg[n] = 0;
  Serial.printf("[%lu ms] MSG #%lu on %s: %.*s\n", millis(), msgCount, topic, (int)len, (char *)payload);
  show("connected", "msg received");
}

void waitForTime() {
  // TLS certificate checks need the real date.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print(F("Waiting for NTP"));
  uint32_t t0 = millis();
  while (time(nullptr) < 1700000000 && millis() - t0 < 20000) {
    delay(250);
    Serial.print('.');
  }
  Serial.printf(" %s\n", time(nullptr) > 1700000000 ? "ok" : "FAILED (TLS will fail)");
  net.setX509Time(time(nullptr));
}

void connectMqtt() {
  Serial.printf("Free heap before TLS: %u\n", ESP.getFreeHeap());
  bool mfln = net.probeMaxFragmentLength(MQTT_HOST, MQTT_PORT, 1024);
  Serial.printf("Server supports small TLS buffers (MFLN): %s\n", mfln ? "yes" : "no");
  if (mfln) net.setBufferSizes(1024, 1024);  // saves ~20 KB if supported

  show("connecting...");
  uint32_t t0 = millis();
  // Clean session (PubSubClient default): nothing queues while we're offline.
  bool ok = mqtt.connect(MQTT_USER, MQTT_USER, MQTT_PASS, TOPIC_STATUS, 1, true, "offline");
  uint32_t dt = millis() - t0;
  if (!ok) {
    char err[64];
    net.getLastSSLError(err, sizeof(err));
    Serial.printf("MQTT connect FAILED after %lu ms, state %d, TLS: %s\n", dt, mqtt.state(), err);
    show("connect FAILED", "see serial");
    return;
  }
  Serial.printf("MQTT connected in %lu ms. Free heap after TLS: %u\n", dt, ESP.getFreeHeap());
  mqtt.publish(TOPIC_STATUS, "online", true);
  mqtt.subscribe(TOPIC_CMD, 1);
  Serial.printf("Subscribed to %s - publish a test message from the EMQX console.\n", TOPIC_CMD);
  show("connected", "waiting for msg");
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("\nMQTT test"));
  Wire.begin(D2, D1);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  show("WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  Serial.printf("WiFi ok, IP %s, RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());

  waitForTime();
  net.setTrustAnchors(&ca);  // verify EMQX's certificate - no setInsecure()
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(60);
  connectMqtt();
}

void loop() {
  if (!mqtt.connected() && millis() - lastTry > 10000) {
    lastTry = millis();
    Serial.println(F("Reconnecting..."));
    connectMqtt();
  }
  mqtt.loop();
  if (millis() - lastHeap > 30000) {
    lastHeap = millis();
    Serial.printf("[%lu s] connected=%d free heap=%u max block=%u msgs=%lu\n", millis() / 1000,
                  mqtt.connected(), ESP.getFreeHeap(), ESP.getMaxFreeBlockSize(), msgCount);
  }
}
