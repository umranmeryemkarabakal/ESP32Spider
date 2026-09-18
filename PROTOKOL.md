# Haberleşme Protokolü (TCP üzerinde ACK katmanı)

## Mesaj formatı

Her mesaj tek satır JSON, `\n` ile biter. Zorunlu alanlar:

```json
{"id":"SPIDER_3","seq":42,"type":"DATA","crc":"A7","payload":{...}}
```

| Alan        | Açıklama                                                    |
| ----------- | ------------------------------------------------------------- |
| `id`      | Robot/istasyon kimliği (örn.`SPIDER_3`, `GROUND`)       |
| `seq`     | 0–65535 arası mesaj sayacı, her gönderende artar          |
| `type`    | `DATA` / `CMD` / `ACK` / `NACK` / `PING` / `PONG` |
| `crc`     | CRC8 hex (id + seq + type + payload üzerinden)               |
| `payload` | Tip-spesifik içerik (opsiyonel)                              |

> **Not (D1):** `ACK` / `NACK` / `PONG` mesajları onay gerektirmediği için kendi
> `seq` alanlarını her zaman sabit `0` olarak gönderir — bu bir hata değil,
> kasıtlı bir tasarımdır. Onayladıkları asıl mesajın seq'i `payload.seq` içindedir.

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

   > **Not (K4):** Yalnızca *son görülen* `seq` hatırlanır — bu, stop-and-wait
   > modelinde (aynı anda tek mesaj uçuşta) yeterlidir. İleride sliding window
   > gibi birden fazla mesajın aynı anda uçuşta olduğu bir modele geçilirse bu
   > mekanizma yetersiz kalır ve birden çok son görülen `seq` takip edilmelidir.
   >
   > **Not (K5):** `seq` 65535'ten 1'e döndüğünde (`rollover`), alıcı taraf
   > büyük bir geriye sıçrama (`seq < son görülen seq - 32768`) tespit ettiğinde
   > idempotency karşılaştırmasını geçersiz sayar; böylece rollover sonrası ilk
   > mesaj yanlışlıkla tekrar sanılmaz.
5. **CRC hatası**: NACK gönderilir, gönderen aynı seq ile tekrar dener. NACK'e
   yanıt olarak yapılan tekrar gönderimler de `deneme sayısı`na dahildir —
   yani sürekli CRC hatası veren bir hat da 5 denemeden sonra bağlantının
   koparılmasını tetikler (K3).
6. **Canlılık**: Son mesajdan 5 sn geçerse PING gönderilir. 10 sn boyunca
   hiç mesaj gelmezse bağlantı ölü sayılır.

## Komut listesi (yer istasyonu → robot)

| Komut             | Etki                                        |
| ----------------- | ------------------------------------------- |
| `DUR`           | Robotu durdur (telemetri göndermeyi keser) |
| `BASLA`         | Telemetriye devam et                        |
| `KONUM_SIFIRLA` | x=0, y=0 yap                                |
| `BATARYA_RESET` | Bataryayı %100'e çek (test)               |
