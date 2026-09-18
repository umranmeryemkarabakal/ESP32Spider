#!/usr/bin/env bash
# ============================================================
#  ESP32'ye bağlı USB portları listeler
#  Kullanım: bash port_bul.sh
# ============================================================

echo ""
echo "=== Bağlı USB/Serial portlar ==="
echo ""

# Mevcut portlar
PORTLAR=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null)
if [ -z "$PORTLAR" ]; then
    echo "  Hiçbir USB serial port bulunamadı."
    echo "  ESP32'nin USB kablosu takılı mı?"
else
    for PORT in $PORTLAR; do
        INFO=$(udevadm info -a -n "$PORT" 2>/dev/null | grep -oP '(?<=ATTRS{product}==")[^"]+' | head -1)
        VENDOR=$(udevadm info -a -n "$PORT" 2>/dev/null | grep -oP '(?<=ATTRS{manufacturer}==")[^"]+' | head -1)
        echo "  $PORT  |  ${VENDOR:-Bilinmeyen üretici} - ${INFO:-Bilinmeyen ürün}"
    done
fi

# arduino-cli ile de dene
echo ""
echo "=== arduino-cli board listesi ==="
arduino-cli board list 2>/dev/null || echo "  (arduino-cli PATH'de değil veya hata)"
echo ""
