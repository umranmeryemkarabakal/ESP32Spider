# Haberleşme Protokolü Test Planı

ESP32 client (SPIDER_3) ↔ ESP32 server (yer istasyonu) ↔ Python ground station
sistemi için sistematik test planı.

## Test ortamı

| Bileşen | Donanım | Yazılım |
|---------|---------|---------|
| Robot   | ESP32-S3 | `client_testler/client_testler.ino` |
| Sunucu  | ESP32 | `server_testler/server_testler.ino` |
| Yer istasyonu | PC + USB serial | `ground_station.py` |

**Önerilen kurulum:** Her ESP32 için ayrı Serial monitor pencereleri açın.
Üç pencereyi de aynı anda görebilmek için ekranı bölün:
- Sol: robot Serial (TX/RX log'u, test komut girişi)
- Orta: server Serial (kullanılmıyorsa kapalı, çünkü Python aynı portu açıyor)
- Sağ: Python ground_station.py çıktısı

Eğer server'a doğrudan test komutu vermek istiyorsanız Python'dan
`T DROP_CMD 3` yazarak gönderebilirsiniz (Python o satırı seri porta yansıtır).

---

## Faz 1: Smoke testleri (mutlu yol)

### T01 — Robot bağlanır ve telemetri akmaya başlar
- **Amaç:** Temel TCP bağlantısı + ilk DATA + ACK döngüsü çalışıyor mu?
- **Kurulum:** Server boot edilmiş, robot kapalı.
- **Tetikleme:** Robotu aç.
- **Beklenen:**
  - Server log: `>>> Slot 0: yeni baglanti`
  - Robot log: `Sunucuya baglandi`
  - 2 sn içinde robot log: `[TX] {"type":"DATA"...}`
  - Robot log: `[OK] ACK alindi seq=1 (deneme 1)`
  - Python log: `[SPIDER_3 #   1] Konum:...`
- **Geçti:** İlk 5 saniye içinde Python'da en az 2 başarılı DATA satırı, CRC hatası yok.

### T02 — Periyodik telemetri istikrarlı
- **Amaç:** 2 sn aralıkla DATA gönderiliyor, hepsi ACK'leniyor.
- **Kurulum:** T01 başarılı.
- **Tetikleme:** 1 dakika bekle.
- **Beklenen:** ~30 başarılı DATA, hiç retry yok, "deneme 1"in dışında ACK görünmüyor.
- **Geçti:** Python özet log'unda `CRC_HATA=0`, `ATLANAN=0`, `DATA≥28`.

### T03 — Yer istasyonundan komut: DUR
- **Amaç:** Python → server → robot CMD akışı + ACK geri dönüşü.
- **Kurulum:** T02 çalışıyor.
- **Tetikleme:** Python'da yaz: `SPIDER_3:DUR` + Enter.
- **Beklenen:**
  - Server log: `[CLI] SPIDER_3 -> DUR gonderildi`
  - Server log: `[TX->SPIDER_3] {"type":"CMD"...}`
  - Robot log: `[KOMUT] DUR`, sonra `[TX] {"type":"ACK"...}`
  - 2 sn içinde robot artık DATA göndermiyor.
- **Geçti:** Komut sonrası 10 sn boyunca yeni DATA gelmiyor; ama PING'ler hala atılıyor (bağlantı canlı).

### T04 — Komut: BASLA
- **Amaç:** Telemetri yeniden başlatılabiliyor.
- **Tetikleme:** Python'da `SPIDER_3:BASLA`.
- **Geçti:** 4 sn içinde DATA satırları tekrar akmaya başlıyor.

### T05 — Komut: KONUM_SIFIRLA
- **Amaç:** Komut parametresi robot durumunu değiştiriyor.
- **Tetikleme:** Python'da `SPIDER_3:KONUM_SIFIRLA`.
- **Geçti:** Sonraki DATA satırında x ve y değerleri 0'a yakın çıkıyor (random walk ile sapacaklardır ama başlangıç ~0).

### T06 — PING/PONG canlılık
- **Amaç:** Boşta bile bağlantı sürüyor mu?
- **Tetikleme:** T03 ile telemetriyi DUR'la. 30 sn bekle.
- **Beklenen:** Server ve robot loglarında 5 sn aralıkla `PING`/`PONG` çiftleri.
- **Geçti:** Bağlantı kopmadı, server `SESSIZ` etiketi vermedi.

### T07 — Aynı seq tekrar ile idempotency
> **Not (O1):** T07 bağımsız olarak tetiklenemez; T13'e bağımlıdır ve yalnızca T13 çalıştırılırken
> yan etki olarak gözlemlenir. Bu testi ayrı bir adım olarak yürütmeyin — T13'ü çalıştırıp
> aşağıdaki "Geçti" kriterini orada kontrol edin.
- **Amaç:** Aynı CMD ikinci kez gelirse iki kez uygulanmamalı.
- **Tetikleme:** Bağımsız tetikleme yok — sadece T13'te (server `T DROP_ACK` ile) doğal olarak görülür.
- **Geçti:** T13 çalıştırılırken bu satır görünmeli: `[~] Tekrar gelen seq=N, sadece ACK gonderiliyor`

### T08 — CRC8 doğru hesaplanıyor
- **Amaç:** İki taraf da CRC üzerinde anlaşıyor.
- **Tetikleme:** T01-T05 boyunca hiçbir yerde `CRC hatasi` veya `NACK` görmemek.
- **Geçti:** İlk dakikada hiç NACK gönderilmedi.

---

## Faz 2: Hata enjeksiyon testleri

> Bu testler için `client_testler/` ve `server_testler/` altındaki test enjeksiyonlu
> `.ino` dosyalarının yüklenmiş olması gerekli (bkz. üstteki test ortamı tablosu).

### T09 — Tekil DATA kaybı (robot tarafı)
- **Amaç:** 1 paket atılınca timeout → retry → başarılı ACK döngüsü çalışıyor mu?
- **Tetikleme:** Robot Serial'a: `T DROP 1`
- **Beklenen sıra:**
  1. Robot log: `[TEST] DATA atlandi`
  2. 500 ms sonra: `[~] Timeout, tekrar gonderim 2/5 (seq=N)`
  3. Robot log: `[TX] {...}` (aynı seq)
  4. Robot log: `[OK] ACK alindi seq=N (deneme 2)`
- **Geçti:** Aynı seq tam 2 denemede başarılı oldu. Python'da `ATLANAN` artmadı (çünkü sonunda geldi).

### T10 — Birden fazla ardışık DATA kaybı
- **Tetikleme:** Robot Serial'a: `T DROP 3`
- **Beklenen:** 1 tek seq için en fazla 4 deneme (3 drop + 1 başarılı), `deneme 4` ile ACK gelmeli.
- **Geçti:** İşlem tamamlandı, bağlantı kopmadı.

### T11 — Max retry aşımı: bağlantı kopması
- **Amaç:** 5 deneme tükenince bağlantı kopuyor ve yeniden kuruluyor mu?
- **Tetikleme:** Robot Serial'a: `T DROP 6` (5'ten fazla)
- **Beklenen sıra:**
  1. `Timeout, tekrar gonderim 2/5` ... `5/5`
  2. `[!!!] Maksimum retry asildi, baglanti kopariliyor`
  3. Birkaç saniye içinde: `Sunucuya baglandi` (yeniden bağlandı)
  4. Yeni slot oluştu (server log: `>>> Slot N: yeni baglanti`)
- **Geçti:** Bağlantı resetlendi, sistem kendi kendine toparlandı.

### T12 — Bozuk CRC → NACK → tekrar gönderim
- **Amaç:** CRC bozulursa karşı taraf NACK yolluyor, gönderen tekrar gönderiyor mu?
- **Tetikleme:** Robot Serial'a: `T CRC 1`
- **Beklenen sıra:**
  1. Robot log: `[TEST] CRC bozuldu`, `[TX] {...crc:"00"...}`
  2. Server log: `[!] CRC hatasi (SPIDER_3) -> NACK`
  3. Server log: `[TX->SPIDER_3] {...type:"NACK"...}`
  4. Robot log: `[!] NACK alindi, hemen tekrar gonderiliyor`
  5. Robot log: `[TX] {...crc:"XX"...}` (doğru CRC ile)
  6. Robot log: `[OK] ACK alindi`
- **Geçti:** Bozuk mesaj telemetri akışına yansımadı, sadece bir gecikme oldu.

### T13 — Sunucudan kaybolan ACK → idempotency
- **Amaç:** ACK kaybolursa robot retry yapar, server aynı seq'i ikinci kez görür → komutu tekrar uygulamaz, sadece ACK'i tekrar gönderir.
- **Tetikleme:** Server Serial'a (veya Python'dan): `T DROP_ACK 1`
- **Beklenen sıra:**
  1. Robot DATA gönderir, server alır ve işler.
  2. Server log: `[TEST] ACK atlandi`
  3. 500 ms sonra robot log: `[~] Timeout, tekrar gonderim 2/5`
  4. Server log: `[~] Tekrar gelen seq=N, sadece ACK gonderiliyor`
  5. Robot log: `[OK] ACK alindi seq=N (deneme 2)`
- **Geçti:** Server'da aynı DATA iki kez işlenmedi (Python'da DATA tek satır, çift değil).

### T14 — CMD kaybı → server retry'ı
- **Amaç:** Server'dan giden CMD kaybolursa server tekrar denemeli.
- **Tetikleme:**
  1. Server Serial'a: `T DROP_CMD 1`
  2. Python'da: `SPIDER_3:DUR`
- **Beklenen:**
  1. Server: `[TEST] CMD atlandi`
  2. 500 ms sonra: `[~] SPIDER_3: timeout, retry 2/5 (seq=N)`
  3. Aynı seq ile tekrar gönderim
  4. Robot ACK döner, server: `[OK<-SPIDER_3] ACK seq=N (deneme 2)`
- **Geçti:** Komut sonunda robotta etkili oldu (DATA durdu).

### T15 — Yapay gecikme ile sınır testi
- **Amaç:** ACK gelir ama timeout sınırına yakın → false-positive retry olmamalı.
- **Tetikleme:** Robot Serial'a: `T DELAY 450` (timeout 500 ms, ACK 450 ms gecikecek)
- **Beklenen:** Tüm DATA "deneme 1"de başarılı, retry yok. Çünkü 450 < 500.
- **Sonra:** `T DELAY 600` → her DATA timeout olur, sürekli retry. 5'i geçince bağlantı kopar.
- **Geçti:** Sistem timeout sınırına saygı duyuyor.

### T16 — WiFi koparma (fiziksel test)
- **Amaç:** WiFi AP kaybolursa robot dürüstçe yeniden bağlanıyor mu?
- **Tetikleme:** Server ESP32'yi 5 saniye için resetleyin (boot tuşu).
- **Beklenen:**
  - Robot log: `[!!!] Karsi taraf 10sn'dir sessiz, baglanti kopariliyor` (veya WiFi kaybı algılanır)
  - Robot LED kırmızı blink (WiFi yok) ya da turuncu (TCP yok)
  - Server tekrar açıldığında robot otomatik bağlanır
- **Geçti:** Manuel müdahale gerekmedi, sistem 30 sn içinde geri toparlandı.

---

## Faz 3: Stres ve uzun süre testleri

### T17 — Saatlerce çalışma
- **Amaç:** RAM sızıntısı, sayaç taşması, yavaş bozulma var mı?
- **Tetikleme:** Sistemi 4-8 saat çalışır halde bırak.
- **Ölçüm:**
  - Başta ve sonda `ESP.getFreeHeap()` (loop'a ekleyin geçici olarak)
  - Toplam DATA sayısı (Python özet)
  - Toplam retry / CRC hatası sayısı
- **Geçti:**
  - Heap düşüşü <%5
  - CRC_HATA / toplam < %0.1
  - Bağlantı koparsa bile her seferinde geri toparlandı

### T18 — Seq rollover (uint16_t taşma)
- **Amaç:** seq 65535'e ulaşınca düzgün 1'e dönüyor mu?
- **Sorun:** Doğal olarak ulaşması ~36 saat sürer (2 sn aralıkla).
- **Hızlı yöntem:** client.ino'da geçici olarak `sonrakiSeq = 65530;` yapıp test edin.
- **Geçti:** 65535'ten sonra seq=1 ile devam, idempotency sorunu yok.

### T19 — Çoklu robot (3 robot eş zamanlı)
- **Amaç:** Server 3+ slot'u eş zamanlı yönetebiliyor mu?
- **Kurulum:** 3 farklı ESP32 (her birinin ROBOT_ID'si farklı: SPIDER_1, SPIDER_2, SPIDER_3).
- **Tetikleme:** Hepsini aynı anda aç.
- **Beklenen:**
  - Server her birine ayrı slot atar
  - Python her ID için ayrı renkli satır gösterir
  - Tek bir robota komut göndermek diğerlerini etkilemez
- **Test:** Sadece SPIDER_2'ye DUR gönder → SPIDER_1 ve SPIDER_3 telemetri akmaya devam etmeli.
- **Geçti:** Bağımsızlık korunuyor.

### T20 — Slot dolduğunda yeni bağlantı reddi
- **Amaç:** MAX_CLIENTS=5 doluyken 6. robot bağlanmayı denerse ne olur?
- **Tetikleme:** 6 robot açın (veya MAX_CLIENTS'ı 2'ye düşürüp 3. robotu deneyin).
- **Beklenen:** Server log: `!!! Slot dolu, baglanti reddedildi`. Robotlar yeniden denerken biri kapanırsa o anda boşalan slot dolar.
- **Geçti:** Server çökmedi, kalanlar düzgün çalışmaya devam etti.

---

## Test sonuç kayıt formatı

Her test için bu satırı doldurun:

```
T<NN> | <Tarih saat> | <GECTI/KALDI> | Süre: <s> | Not: <varsa>
```

Örnek:
```
T01 | 2025-12-05 11:25 | GECTI | Süre: 4s | İlk DATA 3.2sn'de geldi
T11 | 2025-12-05 11:42 | GECTI | Süre: 9s | 5 retry sonrası bağlantı 4s'de geri kuruldu
T13 | 2025-12-05 11:55 | KALDI | Süre: --  | Server retry yaptı ama idempotency log'u görünmedi
```

## Önerilen ölçümler (rapor için)

| Metrik | Ne anlatır |
|--------|------------|
| Mesaj başarı oranı | Başarılı ACK / toplam gönderim |
| Ortalama retry sayısı | (Toplam denemeler - başarılı tek-deneme) / toplam |
| Bağlantı kurtarma süresi | Kopuştan yeni bağlantıya kadar geçen süre (saniye) |
| Ortalama RTT | DATA gönderimi → ACK alımı arası geçen ms |
| CRC hata oranı | NACK sayısı / toplam mesaj |
| Heap kullanımı | Boot anı vs. saat sonra free heap farkı (byte) |

Python `ground_station.py` zaten DATA / CRC_HATA / ATLANAN sayaçları tutuyor.
RTT ölçümü için `client.ino`'ya `millis()` farkı ekleyebilirsiniz
(gonderimZamani ile ACK geldiği an arasında).

## Test sırası önerisi

1. **İlk gün:** T01-T08 (Smoke). Tek robotla, 30 dakika.
2. **İkinci gün:** T09-T16 (Hata enjeksiyonu). Test yamalarını ekledikten sonra.
3. **Üçüncü gün:** T17-T20 (Stres). Uzun çalıştırma + çoklu robot.
4. **Dördüncü gün:** Eksiklikleri kapatma, raporlama.

Her testten önce tüm log'ları temizleyin (Python'u kapatıp açın) — sayaçlar
sıfırdan başlasın. Test log'larını dosyaya kaydetmek için Ubuntu'da:
```bash
# Otomatik log: ground_station.py zaten log_TARIH_SAAT.txt dosyası oluşturur
python3 ground_station.py /dev/ttyUSB0

# Elle log dosyası belirtmek isterseniz:
python3 ground_station.py /dev/ttyUSB0 T09_log.txt
```
