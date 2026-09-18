// esp01_bridge.ino
// ESP-01: Arduino'dan Serial'den veri alır, WiFi TCP ile gönderir

#include <ESP8266WiFi.h>

const char* ssid       = "YOUR_SSID";
const char* password   = "YOUR_PASSWORD";
const char* server_ip  = "192.168.4.1";
const uint16_t server_port = 5000;

WiFiClient client;

void connectWiFi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
}

void connectServer() {
  while (!client.connect(server_ip, server_port)) {
    delay(1000);
  }
}

void setup() {
  Serial.begin(9600);  // Arduino ile haberleşme
  connectWiFi();
  connectServer();
  Serial.println("READY");  // Arduino'ya hazır sinyali
}

void loop() {
  // WiFi kopmuşsa yeniden bağlan
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    connectServer();
  }

  // TCP kopmuşsa yeniden bağlan
  if (!client.connected()) {
    client.stop();
    connectServer();
  }

  // Arduino'dan veri geldiyse TCP'ye ilet
  if (Serial.available()) {
    String mesaj = Serial.readStringUntil('\n');
    mesaj.trim();
    if (mesaj.length() > 0) {
      client.println(mesaj);
    }
  }
}