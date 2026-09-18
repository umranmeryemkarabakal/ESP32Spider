#include "secrets.h"  // WIFI_SSID ve WIFI_PASSWORD burada tanımlı (bkz. secrets.example.h)
// Yer istasyonu - çoklu robot + ACK protokolü
//
// Her robot için ayrı protokol durumu tutar.
// Seri porttan komut girilebilir: SPIDER_3:DUR  /  SPIDER_3:BATARYA_RESET vb.

#include <WiFi.h>
#include <Adafruit_NeoPixel.h>

const char* ap_ssid = WIFI_SSID;
const char* ap_password = WIFI_PASSWORD;
const char* STATION_ID = "GROUND";

#define TCP_PORT 5000
#define MAX_CLIENTS 5

// Protokol parametreleri
const unsigned long ACK_TIMEOUT_MS    = 500;
const uint8_t       MAX_RETRY         = 5;
const unsigned long PING_INTERVAL_MS  = 5000;
const unsigned long DEAD_TIMEOUT_MS   = 10000;

// LED
#define LED_PIN 48
#define LED_COUNT 1
Adafruit_NeoPixel pixel(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

WiFiServer server(TCP_PORT);

// Her slot için protokol durumu
enum GonderimDurumu { BOSTA, ACK_BEKLIYOR };

struct RobotBaglanti {
  WiFiClient client;
  String robotId;              // İlk DATA gelince öğrenilir
  String inBuffer;             // Satır tamamlanana kadar tutulan giriş

  // Gönderim tarafı (yer istasyonu -> robot)
  GonderimDurumu durum = BOSTA;
  uint16_t       sonrakiSeq = 1;
  String         bekleyenMesaj = "";
  uint16_t       bekleyenSeq = 0;
  unsigned long  gonderimZamani = 0;
  uint8_t        denemeSayisi = 0;

  // Komut kuyruğu (seri porttan girilen komutlar)
  String         kuyruktakiCmd = "";

  // Alım tarafı (idempotency)
  uint16_t       sonGorulenSeq = 0;
  bool           sonSeqGecerli = false;

  unsigned long  sonGonderimZamani = 0;
  unsigned long  sonAlimZamani = 0;
};

RobotBaglanti slots[MAX_CLIENTS];

unsigned long sonLedGuncelleme = 0;
unsigned long mesajFlashBitis = 0;
uint8_t nefesDeger = 0;
int8_t nefesYonu = 5;

// ====== CRC8 ======
uint8_t crc8(const String& s) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < s.length(); i++) {
    crc ^= (uint8_t)s[i];
    for (uint8_t j = 0; j < 8; j++) {
      crc = ((crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1)) & 0xFF;  // taşınabilirlik için açık maskeleme
    }
  }
  return crc;
}
String crcByte(uint8_t b) {
  char buf[3]; sprintf(buf, "%02X", b); return String(buf);
}

// ====== JSON alan okuma ======
String alanAl(const String& json, const String& alan) {
  String anahtar = "\"" + alan + "\"";
  int p = json.indexOf(anahtar);
  if (p < 0) return "";
  p = json.indexOf(':', p);
  if (p < 0) return "";
  p++;
  while (p < (int)json.length() && (json[p] == ' ' || json[p] == '\t')) p++;
  if (json[p] == '"') {
    int son = json.indexOf('"', p + 1);
    if (son < 0) return "";
    return json.substring(p + 1, son);
  }
  if (json[p] == '{') {
    int derinlik = 1, q = p + 1;
    while (q < (int)json.length() && derinlik > 0) {
      if (json[q] == '{') derinlik++;
      else if (json[q] == '}') derinlik--;
      q++;
    }
    return json.substring(p, q);
  }
  int son = p;
  while (son < (int)json.length() && json[son] != ',' && json[son] != '}') son++;
  String v = json.substring(p, son); v.trim(); return v;
}

// ====== Mesaj kurma ======
String mesajKur(const String& fromId, uint16_t seq, const String& type, const String& payload) {
  String govde = fromId + String(seq) + type + payload;
  uint8_t c = crc8(govde);
  String m = "{";
  m += "\"id\":\"" + fromId + "\",";
  m += "\"seq\":" + String(seq) + ",";
  m += "\"type\":\"" + type + "\",";
  m += "\"crc\":\"" + crcByte(c) + "\"";
  if (payload.length() > 0) m += ",\"payload\":" + payload;
  m += "}";
  return m;
}

// ====== Düşük seviye gönderim ======
void hamGonder(RobotBaglanti& r, const String& mesaj) {
  if (!r.client.connected()) return;
  r.client.println(mesaj);
  r.client.flush();
  r.sonGonderimZamani = millis();
  mesajFlashBitis = millis() + 60;
  Serial.printf("[TX->%s] %s\n",
                r.robotId.length() ? r.robotId.c_str() : "?", mesaj.c_str());
}

void onayMesajiGonder(RobotBaglanti& r, const String& type, uint16_t hedefSeq) {
  String payload = "{\"seq\":" + String(hedefSeq) + "}";
  String m = mesajKur(STATION_ID, 0, type, payload);
  hamGonder(r, m);
}

bool guvenliMesajGonder(RobotBaglanti& r, const String& type, const String& payload) {
  if (r.durum != BOSTA) return false;
  r.bekleyenSeq = r.sonrakiSeq++;
  if (r.sonrakiSeq == 0) r.sonrakiSeq = 1;
  r.bekleyenMesaj = mesajKur(STATION_ID, r.bekleyenSeq, type, payload);
  r.durum = ACK_BEKLIYOR;
  r.denemeSayisi = 1;
  hamGonder(r, r.bekleyenMesaj);
  r.gonderimZamani = millis();
  return true;
}

// ====== Mesaj işleme ======
void mesajIsle(RobotBaglanti& r, const String& satir) {
  r.sonAlimZamani = millis();

  String type = alanAl(satir, "type");
  String seqStr = alanAl(satir, "seq");
  String crcStr = alanAl(satir, "crc");
  String id = alanAl(satir, "id");
  String payload = alanAl(satir, "payload");
  uint16_t seq = seqStr.toInt();

  // Robot ID öğren
  if (r.robotId.length() == 0 && id.length() > 0) {
    r.robotId = id;
    Serial.printf(">>> Slot tanindi: %s\n", id.c_str());
  }

  // CRC doğrula
  String govde = id + String(seq) + type + payload;
  String beklenen = crcByte(crc8(govde));
  if (crcStr != beklenen) {
    Serial.printf("[!] CRC hatasi (%s) -> NACK\n", id.c_str());
    onayMesajiGonder(r, "NACK", seq);
    return;
  }

  if (type == "ACK") {
    uint16_t hedefSeq = (uint16_t)alanAl(payload, "seq").toInt();
    if (r.durum == ACK_BEKLIYOR && hedefSeq == r.bekleyenSeq) {
      Serial.printf("[OK<-%s] ACK seq=%u (deneme %u)\n",
                    r.robotId.c_str(), hedefSeq, r.denemeSayisi);
      r.durum = BOSTA;
      r.bekleyenMesaj = "";
      r.denemeSayisi = 0;
    }
    return;
  }
  if (type == "NACK") {
    uint16_t hedefSeq = (uint16_t)alanAl(payload, "seq").toInt();
    if (r.durum == ACK_BEKLIYOR && hedefSeq == r.bekleyenSeq) {
      r.denemeSayisi++;  // NACK retry'ını da say (K3: sonsuz döngü koruması)
      if (r.denemeSayisi >= MAX_RETRY) {
        Serial.printf("[!!!] %s: NACK retry ile max retry asildi, baglanti kopariliyor\n",
                      r.robotId.c_str());
        r.client.stop();
        r.durum = BOSTA;
        r.bekleyenMesaj = "";
        return;
      }
      Serial.printf("[!] NACK alindi (%s), tekrar gonderim (deneme %u/%u)\n",
                    r.robotId.c_str(), r.denemeSayisi, MAX_RETRY);
      hamGonder(r, r.bekleyenMesaj);
      r.gonderimZamani = millis();
    }
    return;
  }
  if (type == "PING") {
    onayMesajiGonder(r, "PONG", seq);
    return;
  }
  if (type == "PONG") {
    return;
  }

  // K5: seq rollover tespiti (65535->1 gibi büyük geriye sıçrama), eski karşılaştırmayı geçersiz kıl
  if (r.sonSeqGecerli && seq < r.sonGorulenSeq && (r.sonGorulenSeq - seq) > 32768) {
    r.sonSeqGecerli = false;
  }

  // DATA / CMD: idempotency
  if (r.sonSeqGecerli && seq == r.sonGorulenSeq) {
    onayMesajiGonder(r, "ACK", seq);  // Tekrar geleni yine ACK'la
    return;
  }

  if (type == "DATA") {
    // Yer istasyonu Python'a iletiyor (Serial üzerinden)
    Serial.println(satir);
    r.sonGorulenSeq = seq;
    r.sonSeqGecerli = true;
    onayMesajiGonder(r, "ACK", seq);
  } else if (type == "CMD") {
    // Yer istasyonu komut almaz normalde, yine de ACK'la
    r.sonGorulenSeq = seq;
    r.sonSeqGecerli = true;
    onayMesajiGonder(r, "ACK", seq);
  }
}

// ====== Seri porttan komut alma ======
String seriBuffer = "";

void seriPortKomutKontrol() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (seriBuffer.length() == 0) continue;
      seriBuffer.trim();
      // Format: "SPIDER_3:DUR" veya "SPIDER_3:KONUM_SIFIRLA"
      int p = seriBuffer.indexOf(':');
      if (p > 0) {
        String hedefId = seriBuffer.substring(0, p);
        String cmd = seriBuffer.substring(p + 1);
        hedefId.trim(); cmd.trim();

        bool bulundu = false;
        for (int i = 0; i < MAX_CLIENTS; i++) {
          if (slots[i].robotId == hedefId && slots[i].client.connected()) {
            bulundu = true;
            String payload = "{\"cmd\":\"" + cmd + "\"}";
            if (slots[i].durum == BOSTA) {
              guvenliMesajGonder(slots[i], "CMD", payload);
              Serial.printf("[CLI] %s -> %s gonderildi\n", hedefId.c_str(), cmd.c_str());
            } else {
              // Meşgulse kuyruğa al (sadece bir slot)
              slots[i].kuyruktakiCmd = cmd;
              Serial.printf("[CLI] %s mesgul, %s kuyruga alindi\n",
                            hedefId.c_str(), cmd.c_str());
            }
            break;
          }
        }
        if (!bulundu) {
          Serial.printf("[CLI] Robot bulunamadi: %s\n", hedefId.c_str());
        }
      } else {
        Serial.println("[CLI] Format: ROBOT_ID:KOMUT  (orn: SPIDER_3:DUR)");
      }
      seriBuffer = "";
    } else {
      seriBuffer += c;
      if (seriBuffer.length() > 100) seriBuffer = "";  // taşma koruması
    }
  }
}

// ====== LED ======
void setLED(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b)); pixel.show();
}
int bagliSayisi() {
  int s = 0;
  for (int i = 0; i < MAX_CLIENTS; i++)
    if (slots[i].client && slots[i].client.connected()) s++;
  return s;
}
void ledGuncelle() {
  if (millis() < mesajFlashBitis) { setLED(100, 100, 100); return; }
  if (millis() - sonLedGuncelleme < 50) return;
  sonLedGuncelleme = millis();
  nefesDeger += nefesYonu;
  if (nefesDeger >= 100 || nefesDeger <= 10) nefesYonu = -nefesYonu;
  int sayi = bagliSayisi();
  if (sayi == 0) setLED(0, 0, nefesDeger);
  else if (sayi == 1) setLED(80, 60, 0);
  else setLED(0, 80, 0);
}

// ====== Setup ======
void setup() {
  Serial.begin(115200);
  delay(1000);
  pixel.begin(); pixel.setBrightness(50);
  setLED(50, 0, 50);

  Serial.println("\n=== Yer Istasyonu (ACK protokolu) ===");
  WiFi.softAP(ap_ssid, ap_password);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
  Serial.printf("Port: %d | Max istemci: %d\n", TCP_PORT, MAX_CLIENTS);
  Serial.println("CLI komut formati: ROBOT_ID:KOMUT  (orn: SPIDER_3:DUR)");

  server.begin();
  setLED(0, 0, 50);
}

// ====== Ana döngü ======
void loop() {
  // Yeni bağlantı
  if (server.hasClient()) {
    bool yerleştirildi = false;
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (!slots[i].client || !slots[i].client.connected()) {
        if (slots[i].client) slots[i].client.stop();
        slots[i] = RobotBaglanti();  // Tüm durumu sıfırla
        slots[i].client = server.available();
        slots[i].sonAlimZamani = millis();
        slots[i].sonGonderimZamani = millis();
        Serial.printf(">>> Slot %d: yeni baglanti (%s)\n",
                      i, slots[i].client.remoteIP().toString().c_str());
        yerleştirildi = true;
        break;
      }
    }
    if (!yerleştirildi) {
      WiFiClient red = server.available();
      red.stop();
      Serial.println("!!! Slot dolu, baglanti reddedildi");
    }
  }

  // Her slot için işlem
  for (int i = 0; i < MAX_CLIENTS; i++) {
    RobotBaglanti& r = slots[i];

    if (!r.client) continue;
    if (!r.client.connected()) {
      if (r.robotId.length()) Serial.printf("<<< %s baglantisi koptu\n", r.robotId.c_str());
      r.client.stop();
      r.robotId = "";
      r.durum = BOSTA;
      r.bekleyenMesaj = "";
      continue;
    }

    // Gelen veri (satır birleştirme — readStringUntil eksik veri çağırırsa kayıp olabilirdi)
    while (r.client.available()) {
      char c = r.client.read();
      if (c == '\n') {
        r.inBuffer.trim();
        if (r.inBuffer.length() > 0) mesajIsle(r, r.inBuffer);
        r.inBuffer = "";
      } else if (c != '\r') {
        r.inBuffer += c;
        if (r.inBuffer.length() > 512) r.inBuffer = "";  // taşma
      }
    }

    // ACK timeout
    if (r.durum == ACK_BEKLIYOR && millis() - r.gonderimZamani >= ACK_TIMEOUT_MS) {
      if (r.denemeSayisi >= MAX_RETRY) {
        Serial.printf("[!!!] %s: max retry asildi, baglanti kopariliyor\n",
                      r.robotId.c_str());
        r.client.stop();
        r.durum = BOSTA;
        r.bekleyenMesaj = "";
        continue;
      }
      r.denemeSayisi++;
      Serial.printf("[~] %s: timeout, retry %u/%u (seq=%u)\n",
                    r.robotId.c_str(), r.denemeSayisi, MAX_RETRY, r.bekleyenSeq);
      hamGonder(r, r.bekleyenMesaj);
      r.gonderimZamani = millis();
    }

    // Kuyrukta bekleyen komut varsa ve boştaysak gönder
    if (r.durum == BOSTA && r.kuyruktakiCmd.length() > 0) {
      String payload = "{\"cmd\":\"" + r.kuyruktakiCmd + "\"}";
      guvenliMesajGonder(r, "CMD", payload);
      Serial.printf("[CLI] %s kuyruktan gonderildi: %s\n",
                    r.robotId.c_str(), r.kuyruktakiCmd.c_str());
      r.kuyruktakiCmd = "";
    }

    // PING
    if (r.durum == BOSTA && millis() - r.sonGonderimZamani >= PING_INTERVAL_MS) {
      onayMesajiGonder(r, "PING", 0);
    }

    // Karşı taraf ölü mü?
    if (millis() - r.sonAlimZamani >= DEAD_TIMEOUT_MS) {
      Serial.printf("[!!!] %s: 10sn sessizlik, baglanti kopariliyor\n",
                    r.robotId.c_str());
      r.client.stop();
    }
  }

  seriPortKomutKontrol();
  ledGuncelle();
  delay(2);
}
