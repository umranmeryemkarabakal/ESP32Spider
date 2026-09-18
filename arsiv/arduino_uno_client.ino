// Arduino Uno: SPIDER_05 verisini üretir, ESP-01'e Serial ile gönderir

#include <SoftwareSerial.h>

// ESP-01 ile haberleşme pinleri (TX=2, RX=3)
SoftwareSerial espSerial(2, 3);  // RX, TX

const char* ROBOT_ID = "SPIDER_05";

float batarya  = 100.0;
float konumX   = 20.0;
float konumY   = 20.0;
unsigned long sonGonderim = 0;
const unsigned long GONDERIM_ARALIGI = 1500;

bool espHazir = false;

void setup() {
  Serial.begin(9600);      // Debug için
  espSerial.begin(9600);   // ESP-01 ile

  Serial.println("ESP-01 bekleniyor...");

  // ESP-01'den READY sinyali bekle
  unsigned long baslangic = millis();
  while (millis() - baslangic < 10000) {  // 10 saniye bekle
    if (espSerial.available()) {
      String msg = espSerial.readStringUntil('\n');
      msg.trim();
      if (msg == "READY") {
        espHazir = true;
        Serial.println("ESP-01 hazir!");
        break;
      }
    }
  }

  if (!espHazir) {
    Serial.println("ESP-01 yanit vermedi, yine de devam ediliyor...");
  }
}

void loop() {
  if (millis() - sonGonderim >= GONDERIM_ARALIGI) {
    
    // Konum simülasyonu
    konumX += (random(-10, 11)) / 10.0;
    konumY += (random(-10, 11)) / 10.0;
    batarya -= 0.05;
    if (batarya < 20) batarya = 100.0;

    float sicaklik = 20.0 + random(0, 150) / 10.0;
    int   rssi     = -60;  // Uno WiFi RSSI okuyamaz, sabit değer

    // JSON oluştur
    String mesaj = "{";
    mesaj += "\"id\":\"";
    mesaj += ROBOT_ID;
    mesaj += "\",";
    mesaj += "\"batarya\":";
    mesaj += String(batarya, 1);
    mesaj += ",\"x\":";
    mesaj += String(konumX, 2);
    mesaj += ",\"y\":";
    mesaj += String(konumY, 2);
    mesaj += ",\"sicaklik\":";
    mesaj += String(sicaklik, 1);
    mesaj += ",\"rssi\":";
    mesaj += String(rssi);
    mesaj += ",\"uptime\":";
    mesaj += String(millis());
    mesaj += "}";

    // ESP-01'e gönder
    espSerial.println(mesaj);
    Serial.println("Gonderildi: " + mesaj);  // Debug

    sonGonderim = millis();
  }
}