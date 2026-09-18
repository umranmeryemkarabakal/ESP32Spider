---

## ESP-01 Upload Süreci

**Başlangıç sorunu:** `Failed to connect to ESP8266: Timed out waiting for packet header`

**Bağlantı (Arduino-as-bridge):**
- Arduino RESET → GND (ATmega bypass)
- Arduino TX(0) → ESP-01 TX (düz, çapraz değil)
- Arduino RX(1) → ESP-01 RX
- ESP-01 GPIO0 → GND (programlama modu)
- ESP-01 VCC + CH_PD → ESP32 3V3
- ESP-01 GND → ortak GND
- 47µF kapasitör ESP-01 VCC-GND arasına (yakın)

**Ardışık hatalar ve çözümler:**

1. **"Timed out waiting for packet header"** → Çapraz bağlamıştın, düze çevirdin (TX→TX, RX→RX). ESP cevap vermeye başladı.

2. **"Invalid head of packet (0x73)"** → Sinyal bozulması/besleme çökmesi. 47µF kapasitör eklendi.

3. **"Failed to write compressed data to flash after seq 1 (result was C100)"** → Flash size yanlıştı. esptool "Auto-detected: 512KB" diyordu ama IDE'de "1M (256K SPIFFS)" seçiliydi. **Tools → Flash Size → 512K (FS:none OTA:~246KB)** ile düzeldi.

4. **Sonuç:** "Hash of data verified, Hard resetting" — ESP-01 başarıyla yüklendi ✅

**ESP-01 ayarları (final):**
- Board: Generic ESP8266 Module
- Flash Size: 512K (FS:none OTA:~246KB)
- Upload Speed: 115200
- Port: /dev/ttyUSB0
- Crystal: 26MHz, MAC: 18:fe:34:a0:2b:4a

---

## Arduino Uno Upload Süreci

**Bağlantı (USB direkt):**
- USB → bilgisayar
- Pin 0 ve 1 ESP'den **ayrı** olmalı
- RESET-GND köprüsü **olmamalı**

**Yapılan düzeltmeler kodda:**
- Yorum düzeltildi (RX=2, TX=3)
- `randomSeed(analogRead(A0))` eklendi
- `espHazir = false` ise loop hiç göndermiyor

**Sonuç:** "Done uploading" — Arduino'ya yüklendi ✅
- Sketch: 9410 / 32256 byte (29%)
- RAM: 514 / 2048 byte (25%)

---

## Çalışma Modu (Arduino + ESP-01 birlikte)

**Bağlantı:**
- Arduino pin 2 ← ESP-01 TX (direkt, voltaj bölücü yok)
- Arduino pin 3 → ESP-01 RX (3'lü 1kΩ voltaj bölücü ile, 5V→3.3V)
  - Pin 3 → 1kΩ → orta nokta (ESP RX'e gider) → 1kΩ → 1kΩ → GND
- Arduino GND ↔ ESP32 GND (ortak)
- ESP-01 VCC + CH_PD → ESP32 3V3
- 47µF kapasitör hâlâ takılı
- ESP-01 GPIO0 boşta (programlama modu kapalı)
- ESP-01 RST boşta

---

## Karşılaştığımız Sorunlar (Çalışma Modu)

**Sorun 1: ESP-01 mavi LED Arduino açılınca sönüyor**
- Sebep: Arduino pille, ESP32 USB'den besleniyordu → toprak loop
- Çözüm: Arduino'yu da USB'ye taktın, tek bilgisayar üzerinden besleme

**Sorun 2: "ESP-01 yanit vermedi, gonderim yapilmayacak."**
- Arduino READY beklemesi 10 saniye dolduruyor, ESP-01 hiç READY göndermiyor

**Sorun 3: Voltaj bölücüde 1.66V (beklenen 3.33V)**
- Pin 3 idle'da 5V vermeli, ama ortalama düşük geldi
- Tam sebebi netleşmedi (multimetre AC modu, kontak gevşekliği, ya da pin 3 salınımı olabilir)

**Sorun 4: Pin 0 üzerinden dinleme — port kayboluyor**
- "not in sync: resp=0x00" — Arduino'ya kod yüklerken pin 0'da ESP TX bağlıydı, çakıştı
- "could not open port /dev/ttyUSB0" — RESET-GND köprüsü takılıyken port kayboldu

**Sorun 5: ESP-01 dinlenince çöp karakter geliyor (her baud'da)**
- 9600'de çöp, 74880'de çöp
- Anlamı: ESP boot ediyor (TX hattı çalışıyor) ama anlamlı veri yok
- ESP-01 muhtemelen WiFi connect döngüsünde takılı

---

## Şu An Bilinenler

✅ ESP-01 hayatta, boot ediyor (TX'te veri akıyor)
✅ Arduino kodu çalışıyor, doğru pinleri dinliyor
✅ Bağlantılar doğru (TX→pin2, pin3→bölücü→RX)
✅ Besleme tek kaynaktan, 3.28V VCC stabil
✅ Kapasitör takılı

❌ ESP-01'den anlamlı (Latin) veri gelmiyor
❌ READY mesajı hiç ulaşmadı
❌ Mavi LED Arduino açılınca yanmıyor (önceki sorun, çözüldü mü tam belli değil)

---

## Test Edilmemiş Hipotezler

1. **WiFi "YOUR_SSID" gerçekten var mı, şifre doğru mu?**
2. **Server (192.168.4.1:5000) açık mı?**
3. **ESP-01 boot loop'ta mı yoksa connect döngüsünde mi takılı?**
4. **Mavi LED durumu Arduino tek USB'deyken nasıl?**

---

## Sonraki Adımlar (Devam edersen)

1. ESP-01'e debug'lı kodu yükle (`Serial.begin(115200)`, her adımda Türkçe mesaj)
2. ESP-01 TX'i Arduino pin 0'a bağla, Arduino'ya boş kod yükle
3. Serial Monitor 115200 → ESP'nin çıktısını oku
4. Hangi adımda takıldığı netleşir:
   - "WiFi'ya BAGLANAMADI!" → SSID/şifre/sinyal sorunu
   - "Server'a BAGLANAMADI!" → server tarafı çalışmıyor
   - "READY" → her şey çalışıyor, Arduino tarafına dön

---