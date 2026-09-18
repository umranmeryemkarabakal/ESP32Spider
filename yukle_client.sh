#!/usr/bin/env bash
# ============================================================
#  Client ESP32'ye client.ino yükler
#  Kullanım: bash yukle_client.sh [PORT]
#  Örnek:    bash yukle_client.sh /dev/ttyUSB1
# ============================================================
set -e

PORT="${1:-}"
BOARD="esp32:esp32:esp32s3"   # ESP32-S3 için. Klasik ESP32 için: esp32:esp32:esp32

# Port otomatik algılama (son port = genellikle ikinci cihaz)
if [ -z "$PORT" ]; then
    PORTLAR=($(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null))
    if [ ${#PORTLAR[@]} -eq 0 ]; then
        echo "[HATA] Port bulunamadı. Kullanım: bash yukle_client.sh /dev/ttyUSB1"
        exit 1
    fi
    PORT="${PORTLAR[-1]}"   # en son portu al
    echo "[INFO] Port otomatik seçildi: $PORT"
fi

SKETCH_DIR="$(dirname "$0")/client"
echo "[INFO] Client yükleniyor → $PORT ($BOARD)"
echo "[INFO] Sketch: $SKETCH_DIR"
echo ""

arduino-cli compile \
    --fqbn "$BOARD" \
    "$SKETCH_DIR"

arduino-cli upload \
    --fqbn "$BOARD" \
    --port "$PORT" \
    "$SKETCH_DIR"

echo ""
echo "[OK] Client ESP32'ye yüklendi!"
