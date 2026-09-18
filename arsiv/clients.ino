// Tek ESP32 üzerinde 3 sanal robot calistirir + RGB LED durum

#include <WiFi.h>
#include <Adafruit_NeoPixel.h>

const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";
const char* server_ip = "192.168.4.1";
const uint16_t server_port = 5000;

#define ROBOT_SAYISI 3

// RGB LED ayarları
#define LED_PIN 48
#define LED_COUNT 1
Adafruit_NeoPixel pixel(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

struct Robot {
  const char* id;
  WiFiClient client;
  unsigned long sonGonderim;
  unsigned long gonderimAraligi;
  float batarya;
  float konumX;
  float konumY;
};

Robot robotlar[ROBOT_SAYISI] = {
  {"SPIDER_01", WiFiClient(), 0, 1000, 100.0, 0.0, 0.0},
  {"SPIDER_02", WiFiClient(), 0, 1500, 100.0, 50.0, 50.0},
  {"SPIDER_03", WiFiClient(), 0, 2000, 100.0, -30.0, 20.0}
};

// LED animasyonu
unsigned long sonLedGuncelleme = 0;
unsigned long gonderimFlashBitis = 0;
uint8_t nefesDeger = 0;
int8_t nefesYonu = 3;

void setLED(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

int bagliRobotSayisi() {
  int sayi = 0;
  for (int i = 0; i < ROBOT_SAYISI; i++) {
    if (robotlar[i].client.connected()) sayi++;
  }
  return sayi;
}

void ledGuncelle() {
  // Gönderim flash (mor) öncelikli
  if (millis() < gonderimFlashBitis) {
    setLED(80, 0, 80);
    return;
  }
  
  if (millis() - sonLedGuncelleme < 50) return;
  sonLedGuncelleme = millis();
  
  nefesDeger += nefesYonu;
  if (nefesDeger >= 100 || nefesDeger <= 10) nefesYonu = -nefesYonu;
  
  // 1. WiFi yok = kırmızı yanıp sönen
  if (WiFi.status() != WL_CONNECTED) {
    if ((millis() / 300) % 2 == 0) {
      setLED(100, 0, 0);
    } else {
      setLED(20, 0, 0);
    }
    return;
  }
  
  int bagli = bagliRobotSayisi();
  
  if (bagli == 0) {
    // Turuncu - WiFi var, sunucuya baglanamiyor
    setLED(100, 40, 0);
  } else if (bagli < ROBOT_SAYISI) {
    // Sari - kismen bagli
    setLED(80, 60, 0);
  } else {
    // Yesil nefes - hepsi bagli
    setLED(0, nefesDeger, 0);
  }
}

void connectWiFi() {
  Serial.print("Ağ Atılıyor");
  setLED(100, 40, 0);  // Turuncu = baglanma denemesi
  
  WiFi.begin(ssid, password);
  
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 30) {
    delay(500);
    Serial.print(".");
    // Baglanma sirasinda kirmizi-turuncu gecis
    setLED(100, (retry % 2) * 40, 0);
    retry++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK | IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi HATASI - yeniden deneniyor...");
  }
}

bool connectRobot(int index) {
  if (robotlar[index].client.connect(server_ip, server_port)) {
    Serial.printf("[%s] Sunucuya baglandi\n", robotlar[index].id);
    return true;
  }
  Serial.printf("[%s] Baglanti hatasi\n", robotlar[index].id);
  return false;
}

void robotVerisiGonder(int index) {
  Robot& r = robotlar[index];
  
  r.konumX += random(-10, 11) / 10.0;
  r.konumY += random(-10, 11) / 10.0;
  
  r.batarya -= 0.05;
  if (r.batarya < 20) r.batarya = 100;
  
  float sicaklik = 20.0 + random(0, 150) / 10.0;
  int sinyal = WiFi.RSSI();
  
  String mesaj = "{";
  mesaj += "\"id\":\"" + String(r.id) + "\",";
  mesaj += "\"batarya\":" + String(r.batarya, 1) + ",";
  mesaj += "\"x\":" + String(r.konumX, 2) + ",";
  mesaj += "\"y\":" + String(r.konumY, 2) + ",";
  mesaj += "\"sicaklik\":" + String(sicaklik, 1) + ",";
  mesaj += "\"rssi\":" + String(sinyal) + ",";
  mesaj += "\"uptime\":" + String(millis());
  mesaj += "}";
  
  r.client.println(mesaj);
  Serial.printf("[%s] Gonderildi\n", r.id);
  
  gonderimFlashBitis = millis() + 50;  // 50ms mor flash
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  pixel.begin();
  pixel.setBrightness(50);
  setLED(50, 0, 50);  // Mor = baslatiliyor
  
  Serial.println("\n=== Örümcek Simulatoru (3 Örümcek) ===");
  
  connectWiFi();
  delay(1000);
  
  for (int i = 0; i < ROBOT_SAYISI; i++) {
    connectRobot(i);
    delay(300);
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    return;
  }
  
  for (int i = 0; i < ROBOT_SAYISI; i++) {
    Robot& r = robotlar[i];
    
    if (!r.client.connected()) {
      r.client.stop();
      Serial.printf("[%s] Baglanti koptu, yeniden baglaniyor...\n", r.id);
      delay(500);
      connectRobot(i);
      continue;
    }
    
    if (millis() - r.sonGonderim >= r.gonderimAraligi) {
      robotVerisiGonder(i);
      r.sonGonderim = millis();
    }
  }
  
  ledGuncelle();
  delay(10);
}