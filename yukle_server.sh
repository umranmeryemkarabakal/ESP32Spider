#!/usr/bin/env bash
# ============================================================
#  Server ESP32'ye server.ino yükler
#  Kullanım: bash yukle_server.sh [PORT]
#  Örnek:    bash yukle_server.sh /dev/ttyUSB0
# ============================================================
set -e

PORT="${1:-}"
BOARD="esp32:esp32:esp32s3"   # ESP32-S3 kullanıyorsanız. ESP32 için: esp32:esp32:esp32

# Port otomatik algılama
if [ -z "$PORT" ]; then
    PORT=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -1)
    if [ -z "$PORT" ]; then
        echo "[HATA] Port bulunamadı. Kullanım: bash yukle_server.sh /dev/ttyUSB0"
        exit 1
    fi
    echo "[INFO] Port otomatik seçildi: $PORT"
fi

SKETCH_DIR="$(dirname "$0")/server"
echo "[INFO] Server yükleniyor → $PORT ($BOARD)"
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
echo "[OK] Server ESP32'ye yüklendi!"
echo "     İzlemek için: python3 ground_station.py $PORT"
