# Veri Aktarım Protokolü — Durum Makinesi

`PROTOKOL.md`'de tanımlanan haberleşme kurallarının, `server.ino` ve
`client.ino` içindeki gerçek uygulamaya (`enum GonderimDurumu {BOSTA,
ACK_BEKLIYOR}`) birebir karşılık gelen durum makinesi tarifi. Protokol,
iç içe geçmiş üç durum makinesinden oluşur: bağlantı yaşam döngüsü,
gönderim (stop-and-wait) makinesi ve alım (idempotency) makinesi.

## 1. Bağlantı Yaşam Döngüsü (üst seviye)

```
[YOK] --(TCP connect kabul edildi)--> [BAĞLI/AKTİF] --(ACK_BEKLIYOR'da 5 retry tükendi
                                                        VEYA 10sn hiç mesaj gelmedi
                                                        VEYA 5. NACK retry'ı da tükendi)--> [KOPUK]
[KOPUK] --(slot sıfırlanır, yeni connect beklenir)--> [YOK]
```

- **YOK**: Slot boş, `client.connected() == false`.
- **BAĞLI/AKTİF**: TCP bağlantısı kurulu; içinde alt durum makinesi çalışır (bkz. §2).
- **KOPUK**: `client.stop()` çağrılır, `robotId`, `durum`, `bekleyenMesaj` sıfırlanır → tekrar **YOK** durumuna döner.

Çıkış tetikleyicileri (kod: `server.ino:377-391`, `408-412`, `189-196`):
1. `ACK_BEKLIYOR` durumunda `denemeSayisi >= MAX_RETRY(5)` iken timeout tekrar dolarsa.
2. NACK üzerine tekrar gönderimlerde de sayaç 5'e ulaşırsa (K3).
3. Son alım zamanından beri `DEAD_TIMEOUT_MS(10000)` geçtiyse (karşı taraf sessiz).

## 2. Gönderim Durum Makinesi (stop-and-wait) — her yönde bağımsız çalışır

**Durumlar:** `BOSTA` (idle) ve `ACK_BEKLIYOR` (waiting-ack)

```
              guvenliMesajGonder()
              [sadece durum==BOSTA ise izinli]
   ┌─────┐  ─────────────────────────────►  ┌──────────────┐
   │BOSTA│                                  │ACK_BEKLIYOR  │
   └─────┘  ◄─────────────────────────────  └──────────────┘
              ACK geldi VE seq==bekleyenSeq
```

| # | Mevcut durum | Olay (event) | Guard | Aksiyon | Sonraki durum |
|---|---|---|---|---|---|
| 1 | `BOSTA` | Gönderilecek DATA/CMD var | — | `seq` ata, mesajı kur, gönder, `denemeSayisi=1`, `gonderimZamani=now` | `ACK_BEKLIYOR` |
| 2 | `ACK_BEKLIYOR` | `ACK` alındı | `payload.seq == bekleyenSeq` | mesaj temizlenir, sayaç sıfırlanır | `BOSTA` |
| 3 | `ACK_BEKLIYOR` | `ACK` alındı | `payload.seq != bekleyenSeq` (eski/yanlış ACK) | yok say | `ACK_BEKLIYOR` |
| 4 | `ACK_BEKLIYOR` | `NACK` alındı | `payload.seq == bekleyenSeq` ve `denemeSayisi < 5` | aynı mesajı tekrar gönder, `denemeSayisi++` | `ACK_BEKLIYOR` |
| 5 | `ACK_BEKLIYOR` | `NACK` alındı | `denemeSayisi >= 5` | bağlantıyı kopar (§1) | `BOSTA` (yeni bağlantıda) |
| 6 | `ACK_BEKLIYOR` | 500ms doldu, ACK gelmedi | `denemeSayisi < 5` | aynı seq ile tekrar gönder, `denemeSayisi++` | `ACK_BEKLIYOR` |
| 7 | `ACK_BEKLIYOR` | 500ms doldu, ACK gelmedi | `denemeSayisi >= 5` | bağlantıyı kopar | `BOSTA` (yeni bağlantıda) |
| 8 | `BOSTA` | Gönderilecek mesaj yok | — | kuyruktaki komut varsa onu al ve #1'e git | `BOSTA` |

> `PING`/`ACK`/`NACK`/`PONG` bu durum makinesinden **muaf**: `durum` ne olursa olsun her zaman gönderilebilir (kendi ACK'i beklenmez, `seq=0` sabit).

## 3. Alım (idempotency) Durum Makinesi — karşı tarafın gördüğü seq'e göre

**Durumlar:** `sonSeqGecerli=false` (henüz mesaj görmedi) / `sonSeqGecerli=true` (`sonGorulenSeq` dolu)

```
Mesaj geldi (DATA/CMD, CRC doğru)
        │
        ▼
  CRC yanlış? ──Evet──► NACK gönder, dur (idempotency'e dokunma)
        │Hayır
        ▼
  Rollover kontrolü:
  sonSeqGecerli VE seq < sonGorulenSeq VE (sonGorulenSeq - seq) > 32768?
        │Evet → sonSeqGecerli = false (K5: eski karşılaştırma geçersiz)
        ▼
  sonSeqGecerli VE seq == sonGorulenSeq?
    ├─ Evet (tekrar mesaj) → mesajı İŞLEME, sadece ACK gönder → durum aynı kalır
    └─ Hayır (yeni mesaj)  → mesajı işle (DATA: Serial'e yaz / CMD: uygula),
                              sonGorulenSeq=seq, sonSeqGecerli=true, ACK gönder
```

## Özet — bütün akışı tek bakışta

1. Bağlantı kurulur → slot **YOK → BAĞLI**, durum **BOSTA**, `sonSeqGecerli=false`.
2. Gönderen taraf mesaj kuyruğa girince **BOSTA → ACK_BEKLIYOR**.
3. Karşı taraf CRC kontrol eder: bozuksa **NACK**, gönderen aynı seq ile tekrar dener (deneme sayısına dahil).
4. CRC doğruysa idempotency kontrolü: aynı seq ise sadece ACK tekrarlanır, yan etki yok; yeni seq ise işlenir ve `sonGorulenSeq` güncellenir.
5. ACK gelince **ACK_BEKLIYOR → BOSTA**; gelmezse 500ms'de bir tekrar dener (max 5), 5. denemeden sonra bağlantı **KOPUK** olur ve slot **YOK**'a döner.
6. Paralel olarak, 5sn sessizlikte **PING** atılır, 10sn sessizlikte bağlantı **KOPUK** sayılır (bu, yukarıdaki ACK durumundan bağımsız bir zaman aşımı kanalıdır).

## Kaynak

- Protokol kuralları: `PROTOKOL.md`
- Uygulama: `server/server.ino` (`enum GonderimDurumu`, `mesajIsle`, `guvenliMesajGonder`, ana `loop`)
- Client tarafı simetrik uygulama: `client/client.ino`
