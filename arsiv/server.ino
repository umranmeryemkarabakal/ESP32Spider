// Yer istasyonu - birden fazla robotu aynı anda dinler + RGB LED durum

#include <WiFi.h>
#include <Adafruit_NeoPixel.h>

const char* ap_ssid = "YOUR_SSID";
const char* ap_password = "YOUR_PASSWORD";

#define TCP_PORT 5000
#define MAX_CLIENTS 5

// RGB LED ayarları (ESP32-S3 DevKitC-1: GPIO 48)
#define LED_PIN 48
#define LED_COUNT 1
Adafruit_NeoPixel pixel(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

WiFiServer server(TCP_PORT);
WiFiClient clients[MAX_CLIENTS];

// LED animasyonu için
unsigned long sonLedGuncelleme = 0;
unsigned long mesajFlashBitis = 0;
uint8_t nefesDeger = 0;
int8_t nefesYonu = 5;

void setLED(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

int bagliSayisi() {
  int sayi = 0;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i] && clients[i].connected()) sayi++;
  }
  return sayi;
}

void ledGuncelle() {
  // Mesaj flash (beyaz) öncelikli
  if (millis() < mesajFlashBitis) {
    setLED(100, 100, 100);
    return;
  }
  
  int sayi = bagliSayisi();
  
  // 50ms'de bir animasyon güncelle
  if (millis() - sonLedGuncelleme < 50) return;
  sonLedGuncelleme = millis();
  
  // Nefes alma efekti için parlaklık değeri
  nefesDeger += nefesYonu;
  if (nefesDeger >= 100 || nefesDeger <= 10) nefesYonu = -nefesYonu;
  
  if (sayi == 0) {
    // Mavi nefes - kimse yok
    setLED(0, 0, nefesDeger);
  } else if (sayi == 1) {
    // Sarı - 1 robot
    setLED(80, 60, 0);
  } else {
    // Yeşil - 2+ robot
    setLED(0, 80, 0);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // LED başlat
  pixel.begin();
  pixel.setBrightness(50);  // Çok parlak olmasın
  setLED(50, 0, 50);  // Mor = başlatılıyor
  
  Serial.println("\n=== Bina Kuruluyor ===");
  
  WiFi.softAP(ap_ssid, ap_password);
  IPAddress IP = WiFi.softAPIP();
  
  Serial.print("AP IP: ");
  Serial.println(IP);
  Serial.printf("SSID: %s | Port: %d\n", ap_ssid, TCP_PORT);
  Serial.printf("Max istemci: %d\n", MAX_CLIENTS);
  
  server.begin();
  Serial.println("Örümcekler bekleniyor...\n");
  
  setLED(0, 0, 50);  // Mavi = hazır
}

void loop() {
  // Yeni bağlantı var mı?
  if (server.hasClient()) {
    bool slotBulundu = false;
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (!clients[i] || !clients[i].connected()) {
        if (clients[i]) clients[i].stop();
        clients[i] = server.available();
        
        Serial.printf(">>> Slot %d'e yeni baglanti | IP: %s\n", 
                     i, clients[i].remoteIP().toString().c_str());
        slotBulundu = true;
        break;
      }
    }
    
    if (!slotBulundu) {
      WiFiClient reddedilen = server.available();
      reddedilen.stop();
      Serial.println("!!! Max istemci doldu, baglanti reddedildi");
    }
  }
  
  // Tüm istemcilerden veri oku
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i] && clients[i].connected() && clients[i].available()) {
      String data = clients[i].readStringUntil('\n');
      data.trim();
      if (data.length() > 0) {
        Serial.println(data);
        mesajFlashBitis = millis() + 80;  // 80ms beyaz flash
      }
    }
    
    if (clients[i] && !clients[i].connected()) {
      Serial.printf("<<< Slot %d baglantisi koptu\n", i);
      clients[i].stop();
    }
  }
  
  ledGuncelle();
  delay(5);
}