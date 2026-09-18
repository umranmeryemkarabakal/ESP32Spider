# Arşiv

Bu dizindeki dosyalar Faz 1 öncesi eski kodlardır ve mevcut ACK/NACK/CRC
protokolüyle (bkz. `PROTOKOL.md`) uyumlu değildir. Referans amaçlı saklanmıştır,
aktif geliştirmede kullanılmamalıdır.

- `server.ino`, `clients.ino` — protokolsüz eski sürüm (ACK/timeout/CRC yok,
  `clients.ino` ayrıca eski `SPIDER_01/02/03` ID formatını kullanıyor).
- `es01_bridge.ino`, `esp8266_client.ino`, `arduino_uno_client.ino` — farklı
  donanımlar (ESP8266, Arduino Uno) için eski kodlar.
- `protokollü/` — kök dizindeki `PROTOKOL.md`/`client.ino`/`server.ino`/
  `ground_station.py` dosyalarının 7 Mayıs tarihli, Temmuz 2026 protokol
  düzeltmelerinden (K1-K5, D1, D5) önceki eski kopyaları.
- `upload_sureci.md` — ESP-01 + Arduino Uno köprü kurulumunun (yukarıdaki
  `es01_bridge.ino`/`arduino_uno_client.ino` donanımı) upload sürecini anlatan
  eski not; artık kullanılmayan mimariye ait.
- `client_testler_1/`, `server_testler_1/` — `client_testler/` ve
  `server_testler/` ile aynı özelliklere (T ID, T SEQ, T HEAP, T HEAP_LOG dahil)
  sahip oldukları için birleştirildi; kalan tek fark yorum/biçim stiliydi.
  Fonksiyonel bir değişiklik gerekirse artık sadece `client_testler/` ve
  `server_testler/` güncellenmeli.

- `esp32network/` — projenin ilk prototipi (Nisan–Mayıs 2026, ayrı `ESP32NETWORK`
  klasöründe geliştirildi). `ground_station.py`: birden çok robotun (SPIDER_01–04)
  JSON verisini renkli olarak ayrı ayrı gösteren ilk yer istasyonu;
  `es3286_client.ino`: ESP8266 robot istemcisi. Aynı prototipin server ve client
  kodları yukarıdaki `server.ino` ve `clients.ino` ile birebir aynıdır.

Arşivdeki Wi-Fi ağ adı ve şifresi `YOUR_SSID` / `YOUR_PASSWORD` ile değiştirildi.

Güncel protokol implementasyonu için `client/`, `server/`,
`client_testler/`, `server_testler/` dizinlerine bakın.
