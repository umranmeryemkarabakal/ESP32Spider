# Haberleşme Protokolü (TCP üzerinde ACK katmanı)

## Mesaj formatı

Her mesaj tek satır JSON, `\n` ile biter. Zorunlu alanlar:

```json
{"id":"SPIDER_3","seq":42,"type":"DATA","crc":"A7","payload":{...}}
```

| Alan | Açıklama |
|------|----------|
| `id` | Robot/istasyon kimliği (örn. `SPIDER_3`, `GROUND`) |
| `seq` | 0–65535 arası mesaj sayacı, her gönderende artar |
| `type` | `DATA` / `CMD` / `ACK` / `NACK` / `PING` / `PONG` |
| `crc` | CRC8 hex (id + seq + type + payload üzerinden) |
| `payload` | Tip-spesifik içerik (opsiyonel) |

## Mesaj tipleri

- **DATA** — Robottan telemetri. ACK beklenir.
- **CMD** — Yer istasyonundan komut. ACK beklenir.
- **ACK** — `{"seq":N}` payload'lı, "N numaralı mesajını aldım" onayı.
- **NACK** — CRC veya JSON bozuk; karşı taraf yeniden göndermeli.
- **PING / PONG** — 5 saniyelik canlılık kontrolü, ACK gerektirmez.

## Akış kuralları

1. **Stop-and-wait**: Bir taraf CMD/DATA gönderir, ACK gelene kadar yeni 
   CMD/DATA göndermez (PING/ACK/NACK gönderebilir).
2. **Timeout**: 500 ms içinde ACK gelmezse aynı `seq` ile tekrar gönderilir.
3. **Max retry**: 5 deneme. Hepsi başarısızsa bağlantı koparılır ve sıfırdan kurulur.
4. **Idempotency**: Karşı taraf, son aldığı `seq`'i hatırlar. Aynı `seq` ikinci kez gelirse:
   - Komut/data **yeniden işlenmez** (yan etki yapmaz).
   - Sadece ACK tekrar gönderilir (önceki ACK'imiz kayıp olmuş olabilir).
5. **CRC hatası**: NACK gönderilir, gönderen aynı seq ile tekrar dener.
6. **Canlılık**: Son mesajdan 5 sn geçerse PING gönderilir. 10 sn boyunca 
   hiç mesaj gelmezse bağlantı ölü sayılır.

## Komut listesi (yer istasyonu → robot)

| Komut | Etki |
|-------|------|
| `DUR` | Robotu durdur (telemetri göndermeyi keser) |
| `BASLA` | Telemetriye devam et |
| `KONUM_SIFIRLA` | x=0, y=0 yap |
| `BATARYA_RESET` | Bataryayı %100'e çek (test) |
