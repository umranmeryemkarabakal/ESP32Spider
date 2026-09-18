#include "secrets.h"  // WIFI_SSID ve WIFI_PASSWORD burada tanımlı (bkz. secrets.example.h)
// Tek ESP32 - Tek robot (SPIDER_3) + RGB LED + ACK protokolü
//
// Protokol: stop-and-wait ARQ, CRC8, 500ms timeout, 5 retry, idempotency
// Detaylar: PROTOKOL.md

#include <WiFi.h>
#include <Adafruit_NeoPixel.h>

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* server_ip = "192.168.4.1";
const uint16_t server_port = 5000;

const char* ROBOT_ID = "SPIDER_3";

// ====== Protokol parametreleri ======
const unsigned long ACK_TIMEOUT_MS    = 500;   // ACK bekleme süresi
const uint8_t       MAX_RETRY         = 5;     // Maksimum tekrar denemesi
const unsigned long PING_INTERVAL_MS  = 5000;  // PING gönderme aralığı
const unsigned long DEAD_TIMEOUT_MS   = 10000; // Karşı taraf ölü sayılır
const unsigned long DATA_INTERVAL_MS  = 2000;  // Telemetri gönderim aralığı

// RGB LED ayarları
#define LED_PIN 48
#define LED_COUNT 1
Adafruit_NeoPixel pixel(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// Robot durumu
WiFiClient client;
float batarya = 100.0;
float konumX = -30.0;
float konumY = 20.0;
bool  telemetriAktif = true;  // DUR/BASLA komutu ile değişir

// ====== Protokol durum makinesi ======
enum GonderimDurumu { BOSTA, ACK_BEKLIYOR };
GonderimDurumu durum = BOSTA;

uint16_t      sonrakiSeq = 1;            // Bizim sırada gönderilecek seq
String        inBuffer = "";              // Satır tamamlanana kadar tutulan giriş
String        bekleyenMesaj = "";        // ACK gelmezse tekrar gönderilecek
uint16_t      bekleyenSeq = 0;           // Beklediğimiz ACK'in seq'i
unsigned long gonderimZamani = 0;        // Bekleyenin son gönderim anı
uint8_t       denemeSayisi = 0;          // Kaç kez denedik

uint16_t      sonGorulenSeq = 0;         // Karşıdan en son aldığımız seq (idempotency)
bool          sonSeqGecerli = false;

unsigned long sonGonderimZamani = 0;     // Son herhangi bir mesaj gönderme zamanı
unsigned long sonAlimZamani = 0;         // Son herhangi bir mesaj alma zamanı
unsigned long sonTelemetri = 0;

// LED animasyonu
unsigned long sonLedGuncelleme = 0;
unsigned long gonderimFlashBitis = 0;
uint8_t nefesDeger = 0;
int8_t nefesYonu = 3;

// ====== CRC8 (polinom 0x07) ======
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
  char buf[3];
  sprintf(buf, "%02X", b);
  return String(buf);
}

// ====== LED ======
void setLED(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

void ledGuncelle() {
  if (millis() < gonderimFlashBitis) {
    setLED(80, 0, 80);  // Mor flash = gönderim
    return;
  }
  if (millis() - sonLedGuncelleme < 50) return;
  sonLedGuncelleme = millis();

  nefesDeger += nefesYonu;
  if (nefesDeger >= 100 || nefesDeger <= 10) nefesYonu = -nefesYonu;

  if (WiFi.status() != WL_CONNECTED) {
    setLED(((millis() / 300) % 2) ? 100 : 20, 0, 0);  // Kırmızı blink
    return;
  }
  if (!client.connected()) {
    setLED(100, 40, 0);  // Turuncu
    return;
  }
  if (durum == ACK_BEKLIYOR) {
    setLED(nefesDeger, nefesDeger, 0);  // Sarı nefes = ACK bekleniyor
  } else {
    setLED(0, nefesDeger, 0);  // Yeşil nefes = bağlı ve boşta
  }
}

// ====== Mesaj kurma ======
String mesajKur(uint16_t seq, const String& type, const String& payload) {
  // CRC için id+seq+type+payload birleşimi
  String govde = String(ROBOT_ID) + String(seq) + type + payload;
  uint8_t c = crc8(govde);

  String m = "{";
  m += "\"id\":\"" + String(ROBOT_ID) + "\",";
  m += "\"seq\":" + String(seq) + ",";
  m += "\"type\":\"" + type + "\",";
  m += "\"crc\":\"" + crcByte(c) + "\"";
  if (payload.length() > 0) {
    m += ",\"payload\":" + payload;
  }
  m += "}";
  return m;
}

// ====== Düşük seviye gönderim ======
void hamGonder(const String& mesaj) {
  if (!client.connected()) return;
  client.println(mesaj);
  client.flush();
  sonGonderimZamani = millis();
  gonderimFlashBitis = millis() + 50;
  Serial.printf("[TX] %s\n", mesaj.c_str());
}

// ACK / NACK / PONG gibi onay gerektirmeyen mesajlar
void onayMesajiGonder(const String& type, uint16_t hedefSeq) {
  String payload = "{\"seq\":" + String(hedefSeq) + "}";
  // ACK/NACK/PONG için kendi seq'imiz 0 (önemsiz, çünkü onaylanmıyorlar)
  String m = mesajKur(0, type, payload);
  hamGonder(m);
}

// CMD/DATA gönderimi (ACK bekleyecek)
bool guvenliMesajGonder(const String& type, const String& payload) {
  if (durum != BOSTA) {
    Serial.println("[!] Önceki mesaj hala ACK bekliyor, yeni mesaj reddedildi");
    return false;
  }
  bekleyenSeq = sonrakiSeq++;
  if (sonrakiSeq == 0) sonrakiSeq = 1;  // 0'ı kullanmıyoruz
  bekleyenMesaj = mesajKur(bekleyenSeq, type, payload);
  durum = ACK_BEKLIYOR;
  denemeSayisi = 1;
  hamGonder(bekleyenMesaj);
  gonderimZamani = millis();
  return true;
}

// ====== Mesaj parse (basit, kütüphane bağımlılığı yok) ======
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
  // Sayı veya bool
  int son = p;
  while (son < (int)json.length() && json[son] != ',' && json[son] != '}') son++;
  String v = json.substring(p, son);
  v.trim();
  return v;
}

// ====== Komut işleme (idempotent) ======
void komutUygula(const String& payload) {
  String cmd = alanAl(payload, "cmd");
  Serial.printf("[KOMUT] %s\n", cmd.c_str());

  if (cmd == "DUR") {
    telemetriAktif = false;
  } else if (cmd == "BASLA") {
    telemetriAktif = true;
  } else if (cmd == "KONUM_SIFIRLA") {
    konumX = 0;
    konumY = 0;
  } else if (cmd == "BATARYA_RESET") {
    batarya = 100.0;
  } else {
    Serial.printf("[!] Bilinmeyen komut: %s\n", cmd.c_str());
  }
}

// ====== Gelen mesajı işle ======
void mesajIsle(const String& satir) {
  sonAlimZamani = millis();
  Serial.printf("[RX] %s\n", satir.c_str());

  String type = alanAl(satir, "type");
  String seqStr = alanAl(satir, "seq");
  String crcStr = alanAl(satir, "crc");
  String id = alanAl(satir, "id");
  String payload = alanAl(satir, "payload");
  uint16_t seq = seqStr.toInt();

  // CRC doğrulaması
  String govde = id + String(seq) + type + payload;
  uint8_t hesaplanan = crc8(govde);
  String beklenen = crcByte(hesaplanan);
  if (crcStr != beklenen) {
    Serial.printf("[!] CRC hatası: beklenen=%s gelen=%s -> NACK\n",
                  beklenen.c_str(), crcStr.c_str());
    onayMesajiGonder("NACK", seq);
    return;
  }

  // ACK / NACK işleme
  if (type == "ACK") {
    String hedefSeqStr = alanAl(payload, "seq");
    uint16_t hedefSeq = hedefSeqStr.toInt();
    if (durum == ACK_BEKLIYOR && hedefSeq == bekleyenSeq) {
      Serial.printf("[OK] ACK alindi seq=%u (deneme %u)\n", hedefSeq, denemeSayisi);
      durum = BOSTA;
      bekleyenMesaj = "";
      denemeSayisi = 0;
    } else {
      Serial.printf("[?] Beklenmeyen ACK seq=%u\n", hedefSeq);
    }
    return;
  }
  if (type == "NACK") {
    String hedefSeqStr = alanAl(payload, "seq");
    uint16_t hedefSeq = hedefSeqStr.toInt();
    if (durum == ACK_BEKLIYOR && hedefSeq == bekleyenSeq) {
      denemeSayisi++;  // NACK retry'ını da say (K3: sonsuz döngü koruması)
      if (denemeSayisi >= MAX_RETRY) {
        baglantiyiKopar();
        return;
      }
      Serial.printf("[!] NACK alindi, hemen tekrar gonderiliyor (deneme %u/%u)\n",
                    denemeSayisi, MAX_RETRY);
      hamGonder(bekleyenMesaj);
      gonderimZamani = millis();
    }
    return;
  }
  if (type == "PING") {
    onayMesajiGonder("PONG", seq);
    return;
  }
  if (type == "PONG") {
    return;  // sadece canlılık göstergesi, ek işlem yok
  }

  // K5: seq rollover tespiti (65535->1 gibi büyük geriye sıçrama), eski karşılaştırmayı geçersiz kıl
  if (sonSeqGecerli && seq < sonGorulenSeq && (sonGorulenSeq - seq) > 32768) {
    sonSeqGecerli = false;
  }

  // CMD veya DATA: idempotency kontrolü
  if (sonSeqGecerli && seq == sonGorulenSeq) {
    Serial.printf("[~] Tekrar gelen seq=%u, sadece ACK gonderiliyor\n", seq);
    onayMesajiGonder("ACK", seq);
    return;
  }

  if (type == "CMD") {
    komutUygula(payload);
    sonGorulenSeq = seq;
    sonSeqGecerli = true;
    onayMesajiGonder("ACK", seq);
  } else if (type == "DATA") {
    // Robot tarafı normalde DATA almaz, yine de ACK'la
    sonGorulenSeq = seq;
    sonSeqGecerli = true;
    onayMesajiGonder("ACK", seq);
  } else {
    Serial.printf("[?] Bilinmeyen type: %s\n", type.c_str());
  }
}

// ====== Telemetri payload oluştur ======
String telemetriPayload() {
  konumX += random(-10, 11) / 10.0;
  konumY += random(-10, 11) / 10.0;
  batarya -= 0.05;
  if (batarya < 20) batarya = 100;
  float sicaklik = 20.0 + random(0, 150) / 10.0;
  int sinyal = WiFi.RSSI();

  String p = "{";
  p += "\"batarya\":" + String(batarya, 1) + ",";
  p += "\"x\":" + String(konumX, 2) + ",";
  p += "\"y\":" + String(konumY, 2) + ",";
  p += "\"sicaklik\":" + String(sicaklik, 1) + ",";
  p += "\"rssi\":" + String(sinyal) + ",";
  p += "\"uptime\":" + String(millis());
  p += "}";
  return p;
}

// ====== Bağlantı ======
void connectWiFi() {
  Serial.print("WiFi'ye baglaniliyor");
  WiFi.begin(ssid, password);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 30) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK | IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi HATASI");
  }
}

bool connectRobot() {
  if (client.connect(server_ip, server_port)) {
    Serial.printf("[%s] Sunucuya baglandi\n", ROBOT_ID);
    // Bağlantı sıfırlandığında protokol durumunu da sıfırla
    durum = BOSTA;
    bekleyenMesaj = "";
    denemeSayisi = 0;
    sonSeqGecerli = false;
    sonAlimZamani = millis();
    sonGonderimZamani = millis();
    return true;
  }
  return false;
}

void baglantiyiKopar() {
  Serial.println("[!!!] Maksimum retry asildi, baglanti kopariliyor");
  client.stop();
  durum = BOSTA;
  bekleyenMesaj = "";
  denemeSayisi = 0;
}

// ====== Setup ======
void setup() {
  Serial.begin(115200);
  delay(1000);
  pixel.begin();
  pixel.setBrightness(50);
  setLED(50, 0, 50);

  Serial.printf("\n=== %s (ACK protokolu) ===\n", ROBOT_ID);
  connectWiFi();
  delay(500);
  connectRobot();
}

// ====== Ana döngü ======
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    ledGuncelle();
    return;
  }
  if (!client.connected()) {
    client.stop();
    delay(500);
    connectRobot();
    ledGuncelle();
    return;
  }

  // Gelen veriyi oku (karakter karakter, K2: readStringUntil() 1sn bloke edebiliyordu)
  while (client.available()) {
    char c = client.read();
    if (c == '\n') {
      inBuffer.trim();
      if (inBuffer.length() > 0) mesajIsle(inBuffer);
      inBuffer = "";
    } else if (c != '\r') {
      inBuffer += c;
      if (inBuffer.length() > 512) inBuffer = "";  // taşma koruması
    }
  }

  // ACK timeout kontrolü
  if (durum == ACK_BEKLIYOR && millis() - gonderimZamani >= ACK_TIMEOUT_MS) {
    if (denemeSayisi >= MAX_RETRY) {
      baglantiyiKopar();
      ledGuncelle();
      return;
    }
    denemeSayisi++;
    Serial.printf("[~] Timeout, tekrar gonderim %u/%u (seq=%u)\n",
                  denemeSayisi, MAX_RETRY, bekleyenSeq);
    hamGonder(bekleyenMesaj);
    gonderimZamani = millis();
  }

  // Periyodik telemetri gönder (boştaysak)
  if (durum == BOSTA && telemetriAktif &&
      millis() - sonTelemetri >= DATA_INTERVAL_MS) {
    if (guvenliMesajGonder("DATA", telemetriPayload())) {
      sonTelemetri = millis();
    }
  }

  // PING (uzun süredir hiç mesaj göndermediysek)
  if (durum == BOSTA && millis() - sonGonderimZamani >= PING_INTERVAL_MS) {
    onayMesajiGonder("PING", 0);
  }

  // Karşı taraf ölü mü?
  if (millis() - sonAlimZamani >= DEAD_TIMEOUT_MS) {
    Serial.println("[!!!] Karsi taraf 10sn'dir sessiz, baglanti kopariliyor");
    client.stop();
    return;
  }

  ledGuncelle();
  delay(5);
}
