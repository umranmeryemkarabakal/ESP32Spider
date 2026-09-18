// ============================================================================
//  SPIDER robot istemcisi  —  TUM FAZLAR (smoke + hata enjeksiyon + stres)
// ----------------------------------------------------------------------------
//  Tek ESP32 + RGB LED + ACK protokolu (stop-and-wait ARQ, CRC8, idempotency)
//  Detaylar: PROTOKOL.md
//
//  Seri port test komutlari (Serial monitor'e yaz):
//    T DROP <n>      Sonraki n DATA gonderimini atla        (T09, T10, T11)
//    T CRC  <n>      Sonraki n DATA'yi bozuk CRC ile gonder (T12)
//    T DELAY <ms>    Her gonderime ms gecikme ekle          (T15)
//    T SEQ <n>       sonrakiSeq'i n yap (rollover testi)     (T18)
//    T ID <isim>     Robot kimligini degistir + yeniden bagl (T19)
//    T HEAP          Su anki bos heap'i yazdir               (T17)
//    T HEAP_LOG <s>  Her s saniyede heap yazdir (0=kapat)    (T17)
//    T STATUS        Test ayarlarini goster
//    T OFF           Tum test ayarlarini sifirla
// ============================================================================

#include <WiFi.h>
#include <Adafruit_NeoPixel.h>

const char* ssid       = "YOUR_SSID";
const char* password   = "YOUR_PASSWORD";
const char* server_ip  = "192.168.4.1";
const uint16_t server_port = 5000;

// T19 icin: kalici kimlik istiyorsan burayi degistirip yeniden yukle,
// veya calisirken "T ID SPIDER_1" komutuyla gecici olarak ata.
#define DEFAULT_ROBOT_ID "SPIDER_3"
String ROBOT_ID = DEFAULT_ROBOT_ID;

// ====== Protokol parametreleri ======
const unsigned long ACK_TIMEOUT_MS    = 500;   // ACK bekleme suresi
const uint8_t       MAX_RETRY         = 5;     // Maksimum tekrar
const unsigned long PING_INTERVAL_MS  = 5000;  // PING araligi
const unsigned long DEAD_TIMEOUT_MS   = 10000; // Karsi taraf olu sayilir
const unsigned long DATA_INTERVAL_MS  = 2000;  // Telemetri araligi

// RGB LED
#define LED_PIN 48
#define LED_COUNT 1
Adafruit_NeoPixel pixel(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// Robot durumu
WiFiClient client;
float batarya = 100.0;
float konumX  = -30.0;
float konumY  =  20.0;
bool  telemetriAktif = true;   // DUR/BASLA ile degisir

// ====== Protokol durum makinesi ======
enum GonderimDurumu { BOSTA, ACK_BEKLIYOR };
GonderimDurumu durum = BOSTA;

uint16_t      sonrakiSeq = 1;
String        bekleyenMesaj = "";
uint16_t      bekleyenSeq = 0;
unsigned long gonderimZamani = 0;       // son (re)gonderim ani
unsigned long ilkGonderimZamani = 0;    // ilk gonderim ani -> RTT icin
uint8_t       denemeSayisi = 0;

uint16_t      sonGorulenSeq = 0;        // idempotency
bool          sonSeqGecerli = false;

unsigned long sonGonderimZamani = 0;
unsigned long sonAlimZamani = 0;
unsigned long sonTelemetri = 0;

// LED animasyonu
unsigned long sonLedGuncelleme = 0;
unsigned long gonderimFlashBitis = 0;
uint8_t nefesDeger = 0;
int8_t  nefesYonu = 3;

// ====== TEST: Hata enjeksiyon degiskenleri ======
uint16_t test_drop_count    = 0;
uint16_t test_corrupt_count = 0;
uint32_t test_extra_delay   = 0;
String   test_seriBuffer    = "";

// O2: non-blocking gecikme icin bekleyen mesaj (delay() tum loop'u bloke ediyordu)
String        test_delayedMsg      = "";
unsigned long test_delayedSendTime = 0;
bool          test_delayPending    = false;

String        inBuffer = "";       // Satır tamamlanana kadar tutulan giriş

// ====== TEST: heap log ======
uint32_t      heap_log_interval_ms = 0;   // 0 = kapali
unsigned long sonHeapLog = 0;

// ====== CRC8 (polinom 0x07) ======
uint8_t crc8(const String& s) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < s.length(); i++) {
    crc ^= (uint8_t)s[i];
    for (uint8_t j = 0; j < 8; j++)
      crc = ((crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1)) & 0xFF;  // taşınabilirlik için açık maskeleme
  }
  return crc;
}
String crcByte(uint8_t b) { char buf[3]; sprintf(buf, "%02X", b); return String(buf); }

// ====== LED ======
void setLED(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b)); pixel.show();
}
void ledGuncelle() {
  if (millis() < gonderimFlashBitis) { setLED(80, 0, 80); return; }   // mor flash
  if (millis() - sonLedGuncelleme < 50) return;
  sonLedGuncelleme = millis();
  nefesDeger += nefesYonu;
  if (nefesDeger >= 100 || nefesDeger <= 10) nefesYonu = -nefesYonu;
  if (WiFi.status() != WL_CONNECTED) { setLED(((millis()/300)%2)?100:20, 0, 0); return; } // kirmizi blink
  if (!client.connected())          { setLED(100, 40, 0); return; }                       // turuncu
  if (durum == ACK_BEKLIYOR)         setLED(nefesDeger, nefesDeger, 0);                    // sari nefes
  else                               setLED(0, nefesDeger, 0);                            // yesil nefes
}

// ====== Mesaj kurma ======
String mesajKur(uint16_t seq, const String& type, const String& payload) {
  String govde = ROBOT_ID + String(seq) + type + payload;
  uint8_t c = crc8(govde);
  String m = "{";
  m += "\"id\":\"" + ROBOT_ID + "\",";
  m += "\"seq\":" + String(seq) + ",";
  m += "\"type\":\"" + type + "\",";
  m += "\"crc\":\"" + crcByte(c) + "\"";
  if (payload.length() > 0) m += ",\"payload\":" + payload;
  m += "}";
  return m;
}

// ====== Dusuk seviye gonderim (test enjeksiyonu burada) ======
void hamGonder(const String& mesaj) {
  if (!client.connected()) return;

  // TEST: DATA atla (kayip simulasyonu)
  if (test_drop_count > 0 && mesaj.indexOf("\"type\":\"DATA\"") > 0) {
    test_drop_count--;
    Serial.printf("[TEST] DATA atlandi (kalan drop: %u)\n", test_drop_count);
    sonGonderimZamani = millis();
    gonderimFlashBitis = millis() + 50;
    return;
  }

  // TEST: CRC boz (sadece DATA)
  String gondrilecek = mesaj;
  if (test_corrupt_count > 0 && mesaj.indexOf("\"type\":\"DATA\"") > 0) {
    test_corrupt_count--;
    int crcPos = gondrilecek.indexOf("\"crc\":\"");
    if (crcPos > 0) {
      gondrilecek = gondrilecek.substring(0, crcPos + 7) + "00" +
                    gondrilecek.substring(crcPos + 9);
      Serial.printf("[TEST] CRC bozuldu (kalan corrupt: %u)\n", test_corrupt_count);
    }
  }

  // TEST: yapay gecikme (O2: non-blocking, artik tum loop'u bloke etmiyor)
  if (test_extra_delay > 0) {
    test_delayedMsg = gondrilecek;
    test_delayedSendTime = millis() + test_extra_delay;
    test_delayPending = true;
    return;
  }

  gercekGonder(gondrilecek);
}

void gercekGonder(const String& mesaj) {
  client.println(mesaj);
  client.flush();
  sonGonderimZamani = millis();
  gonderimFlashBitis = millis() + 50;
  Serial.printf("[TX] %s\n", mesaj.c_str());
}

// O2: gecikmeli mesaj vakti geldiyse gonder (loop basinda cagrilir)
void testGecikmeliGonderimKontrol() {
  if (test_delayPending && (long)(millis() - test_delayedSendTime) >= 0) {
    test_delayPending = false;
    gercekGonder(test_delayedMsg);
  }
}

// ====== TEST komut isleyici ======
void testKomutIsle(const String& satir) {
  if (!satir.startsWith("T ") && satir != "T") return;
  String rest = satir.substring(2); rest.trim();

  if (rest == "OFF") {
    test_drop_count = test_corrupt_count = 0;
    test_extra_delay = 0; heap_log_interval_ms = 0;
    Serial.println("[TEST] Tum test ayarlari sifirlandi");
    return;
  }
  if (rest == "STATUS") {
    Serial.printf("[TEST] id=%s drop=%u corrupt=%u delay=%ums seq=%u heaplog=%ums\n",
                  ROBOT_ID.c_str(), test_drop_count, test_corrupt_count,
                  test_extra_delay, sonrakiSeq, heap_log_interval_ms);
    return;
  }
  if (rest == "HEAP") {
    Serial.printf("[HEAP] free=%u bytes  minFree=%u  uptime=%lus\n",
                  ESP.getFreeHeap(), ESP.getMinFreeHeap(), millis()/1000);
    return;
  }

  int sp = rest.indexOf(' ');
  if (sp < 0) {
    Serial.println("[TEST] Format: T <DROP|CRC|DELAY|SEQ|ID|HEAP_LOG> <deger> | T OFF|STATUS|HEAP");
    return;
  }
  String kom = rest.substring(0, sp);
  String arg = rest.substring(sp + 1); arg.trim();
  uint32_t deg = arg.toInt();

  if (kom == "DROP") {
    test_drop_count = deg;
    Serial.printf("[TEST] Sonraki %u DATA atlanacak\n", deg);
  } else if (kom == "CRC") {
    test_corrupt_count = deg;
    Serial.printf("[TEST] Sonraki %u DATA bozuk CRC ile gidecek\n", deg);
  } else if (kom == "DELAY") {
    test_extra_delay = deg;
    Serial.printf("[TEST] Her gonderime %u ms gecikme eklenecek\n", deg);
  } else if (kom == "SEQ") {
    sonrakiSeq = (uint16_t)deg; if (sonrakiSeq == 0) sonrakiSeq = 1;
    Serial.printf("[TEST] sonrakiSeq = %u (rollover testi icin)\n", sonrakiSeq);
  } else if (kom == "ID") {
    ROBOT_ID = arg;
    Serial.printf("[TEST] Robot kimligi -> %s. Yeniden baglaniliyor...\n", ROBOT_ID.c_str());
    client.stop();   // yeni kimlikle taze slot acilsin
  } else if (kom == "HEAP_LOG") {
    heap_log_interval_ms = deg * 1000UL;
    sonHeapLog = millis();
    if (deg) Serial.printf("[TEST] Her %u sn heap yazdirilacak\n", deg);
    else     Serial.println("[TEST] Heap log kapatildi");
  } else {
    Serial.println("[TEST] Bilinmeyen komut");
  }
}

void testSerialOku() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (test_seriBuffer.length() > 0) {
        test_seriBuffer.trim();
        testKomutIsle(test_seriBuffer);
        test_seriBuffer = "";
      }
    } else {
      test_seriBuffer += c;
      if (test_seriBuffer.length() > 50) test_seriBuffer = "";
    }
  }
}

// ACK / NACK / PONG (onay gerektirmez, seq=0)
void onayMesajiGonder(const String& type, uint16_t hedefSeq) {
  String payload = "{\"seq\":" + String(hedefSeq) + "}";
  hamGonder(mesajKur(0, type, payload));
}

// CMD/DATA gonderimi (ACK bekler)
bool guvenliMesajGonder(const String& type, const String& payload) {
  if (durum != BOSTA) {
    Serial.println("[!] Onceki mesaj hala ACK bekliyor, yeni mesaj reddedildi");
    return false;
  }
  bekleyenSeq = sonrakiSeq++;
  if (sonrakiSeq == 0) sonrakiSeq = 1;
  bekleyenMesaj = mesajKur(bekleyenSeq, type, payload);
  durum = ACK_BEKLIYOR;
  denemeSayisi = 1;
  ilkGonderimZamani = millis();           // RTT icin
  hamGonder(bekleyenMesaj);
  gonderimZamani = millis();
  return true;
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
    return (son < 0) ? "" : json.substring(p + 1, son);
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

// ====== Komut isleme (idempotent) ======
void komutUygula(const String& payload) {
  String cmd = alanAl(payload, "cmd");
  Serial.printf("[KOMUT] %s\n", cmd.c_str());
  if      (cmd == "DUR")            telemetriAktif = false;
  else if (cmd == "BASLA")          telemetriAktif = true;
  else if (cmd == "KONUM_SIFIRLA") { konumX = 0; konumY = 0; }
  else if (cmd == "BATARYA_RESET")  batarya = 100.0;
  else Serial.printf("[!] Bilinmeyen komut: %s\n", cmd.c_str());
}

// ====== Gelen mesaji isle ======
void mesajIsle(const String& satir) {
  sonAlimZamani = millis();
  Serial.printf("[RX] %s\n", satir.c_str());

  String type    = alanAl(satir, "type");
  String seqStr  = alanAl(satir, "seq");
  String crcStr  = alanAl(satir, "crc");
  String id      = alanAl(satir, "id");
  String payload = alanAl(satir, "payload");
  uint16_t seq = seqStr.toInt();

  // CRC dogrulamasi
  String govde = id + String(seq) + type + payload;
  String beklenen = crcByte(crc8(govde));
  if (crcStr != beklenen) {
    Serial.printf("[!] CRC hatasi: beklenen=%s gelen=%s -> NACK\n",
                  beklenen.c_str(), crcStr.c_str());
    onayMesajiGonder("NACK", seq);
    return;
  }

  if (type == "ACK") {
    uint16_t hedefSeq = (uint16_t)alanAl(payload, "seq").toInt();
    if (durum == ACK_BEKLIYOR && hedefSeq == bekleyenSeq) {
      unsigned long rtt = millis() - ilkGonderimZamani;
      Serial.printf("[OK] ACK alindi seq=%u (deneme %u) RTT=%lums\n",
                    hedefSeq, denemeSayisi, rtt);
      durum = BOSTA; bekleyenMesaj = ""; denemeSayisi = 0;
    } else {
      Serial.printf("[?] Beklenmeyen ACK seq=%u\n", hedefSeq);
    }
    return;
  }
  if (type == "NACK") {
    uint16_t hedefSeq = (uint16_t)alanAl(payload, "seq").toInt();
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
  if (type == "PING") { onayMesajiGonder("PONG", seq); return; }
  if (type == "PONG") { return; }

  // K5: seq rollover tespiti (65535->1 gibi büyük geriye sıçrama), eski karşılaştırmayı geçersiz kıl
  if (sonSeqGecerli && seq < sonGorulenSeq && (sonGorulenSeq - seq) > 32768) {
    sonSeqGecerli = false;
  }

  // CMD / DATA: idempotency
  if (sonSeqGecerli && seq == sonGorulenSeq) {
    Serial.printf("[~] Tekrar gelen seq=%u, sadece ACK gonderiliyor\n", seq);
    onayMesajiGonder("ACK", seq);
    return;
  }

  if (type == "CMD") {
    komutUygula(payload);
    sonGorulenSeq = seq; sonSeqGecerli = true;
    onayMesajiGonder("ACK", seq);
  } else if (type == "DATA") {
    sonGorulenSeq = seq; sonSeqGecerli = true;
    onayMesajiGonder("ACK", seq);
  } else {
    Serial.printf("[?] Bilinmeyen type: %s\n", type.c_str());
  }
}

// ====== Telemetri payload ======
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

// ====== Baglanti ======
void connectWiFi() {
  Serial.print("WiFi'ye baglaniliyor");
  WiFi.begin(ssid, password);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 30) { delay(500); Serial.print("."); retry++; }
  if (WiFi.status() == WL_CONNECTED) Serial.println("\nWiFi OK | IP: " + WiFi.localIP().toString());
  else Serial.println("\nWiFi HATASI");
}

bool connectRobot() {
  if (client.connect(server_ip, server_port)) {
    Serial.printf("[%s] Sunucuya baglandi\n", ROBOT_ID.c_str());
    durum = BOSTA; bekleyenMesaj = ""; denemeSayisi = 0;
    sonSeqGecerli = false;
    sonAlimZamani = millis(); sonGonderimZamani = millis();
    return true;
  }
  return false;
}

void baglantiyiKopar() {
  Serial.println("[!!!] Maksimum retry asildi, baglanti kopariliyor");
  client.stop();
  durum = BOSTA; bekleyenMesaj = ""; denemeSayisi = 0;
}

// ====== Setup ======
void setup() {
  Serial.begin(115200);
  delay(1000);
  pixel.begin(); pixel.setBrightness(50);
  setLED(50, 0, 50);

  Serial.printf("\n=== %s (ACK protokolu — tum fazlar) ===\n", ROBOT_ID.c_str());
  Serial.println("Test komutlari icin: T STATUS");
  connectWiFi();
  delay(500);
  connectRobot();
}

// ====== Ana dongu ======
void loop() {
  testSerialOku();   // hata enjeksiyon komutlari
  testGecikmeliGonderimKontrol();  // O2: bekleyen gecikmeli mesaji zamani geldiyse gonder

  // Periyodik heap log (T17)
  if (heap_log_interval_ms > 0 && millis() - sonHeapLog >= heap_log_interval_ms) {
    sonHeapLog = millis();
    Serial.printf("[HEAP] free=%u bytes  minFree=%u  uptime=%lus\n",
                  ESP.getFreeHeap(), ESP.getMinFreeHeap(), millis()/1000);
  }

  if (WiFi.status() != WL_CONNECTED) { connectWiFi(); ledGuncelle(); return; }
  if (!client.connected())          { client.stop(); delay(500); connectRobot(); ledGuncelle(); return; }

  // Gelen veri (karakter karakter, K2: readStringUntil() 1sn bloke edebiliyordu)
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

  // ACK timeout
  if (durum == ACK_BEKLIYOR && millis() - gonderimZamani >= ACK_TIMEOUT_MS) {
    if (denemeSayisi >= MAX_RETRY) { baglantiyiKopar(); ledGuncelle(); return; }
    denemeSayisi++;
    Serial.printf("[~] Timeout, tekrar gonderim %u/%u (seq=%u)\n",
                  denemeSayisi, MAX_RETRY, bekleyenSeq);
    hamGonder(bekleyenMesaj);
    gonderimZamani = millis();
  }

  // Periyodik telemetri
  if (durum == BOSTA && telemetriAktif && millis() - sonTelemetri >= DATA_INTERVAL_MS) {
    if (guvenliMesajGonder("DATA", telemetriPayload())) sonTelemetri = millis();
  }

  // PING
  if (durum == BOSTA && millis() - sonGonderimZamani >= PING_INTERVAL_MS)
    onayMesajiGonder("PING", 0);

  // Karsi taraf olu mu?
  if (millis() - sonAlimZamani >= DEAD_TIMEOUT_MS) {
    Serial.println("[!!!] Karsi taraf 10sn'dir sessiz, baglanti kopariliyor");
    client.stop();
    return;
  }

  ledGuncelle();
  delay(5);
}
