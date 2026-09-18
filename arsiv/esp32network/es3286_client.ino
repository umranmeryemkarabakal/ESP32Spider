// esp8266_robot.ino
// ESP8266'yı GroundStation ağına tek robot olarak bağlar

#include <ESP8266WiFi.h>

const char* ssid     = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";
const char* server_ip   = "192.168.4.1";  // ESP32 AP IP
const uint16_t server_port = 5000;

const char* ROBOT_ID = "SPIDER_04";  // Yeni, benzersiz ID

WiFiClient client;

float batarya  = 100.0;
float konumX   = 10.0;
float konumY   = 10.0;
unsigned long sonGonderim = 0;
const unsigned long GONDERIM_ARALIGI = 1200; // ms

void connectWiFi() {
  Serial.print("WiFi baglaniyor");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi OK | IP: " + WiFi.localIP().toString());
}

bool connectServer() {
  Serial.printf("[%s] Sunucuya baglaniliyor...\n", ROBOT_ID);
  if (client.connect(server_ip, server_port)) {
    Serial.printf("[%s] Baglandi!\n", ROBOT_ID);
    return true;
  }
  Serial.printf("[%s] Baglanti hatasi\n", ROBOT_ID);
  return false;
}

void gonderVeri() {
  // Konum simülasyonu
  konumX += (random(-10, 11)) / 10.0;
  konumY += (random(-10, 11)) / 10.0;
  batarya -= 0.05;
  if (batarya < 20) batarya = 100.0;

  float sicaklik = 20.0 + random(0, 150) / 10.0;
  int   rssi     = WiFi.RSSI();

  // clients.ino ile aynı JSON formatı
  String msg = "{";
  msg += "\"id\":\"" + String(ROBOT_ID) + "\",";
  msg += "\"batarya\":"  + String(batarya,  1) + ",";
  msg += "\"x\":"        + String(konumX,   2) + ",";
  msg += "\"y\":"        + String(konumY,   2) + ",";
  msg += "\"sicaklik\":" + String(sicaklik, 1) + ",";
  msg += "\"rssi\":"     + String(rssi)        + ",";
  msg += "\"uptime\":"   + String(millis());
  msg += "}";

  client.println(msg);
  Serial.println("[GONDERILDI] " + msg);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP8266 Robot Node ===");
  connectWiFi();
  delay(500);
  connectServer();
}

void loop() {
  // WiFi kopmuşsa yeniden bağlan
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    return;
  }

  // TCP kopmuşsa yeniden bağlan
  if (!client.connected()) {
    client.stop();
    Serial.printf("[%s] Koptu, yeniden baglaniyor...\n", ROBOT_ID);
    delay(1000);
    connectServer();
    return;
  }

  // Periyodik veri gönder
  if (millis() - sonGonderim >= GONDERIM_ARALIGI) {
    gonderVeri();
    sonGonderim = millis();
  }
}