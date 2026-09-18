#!/usr/bin/env bash
# ============================================================
#  ESP32SPIDER - Ubuntu 22.04 Kurulum Scripti
#  Çalıştır: bash kurulum_ubuntu.sh
# ============================================================
set -e

BLUE='\033[0;34m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'
info()    { echo -e "${BLUE}[INFO]${NC} $1"; }
success() { echo -e "${GREEN}[OK]${NC} $1"; }
warn()    { echo -e "${YELLOW}[UYARI]${NC} $1"; }
error()   { echo -e "${RED}[HATA]${NC} $1"; exit 1; }

echo ""
echo "=============================================="
echo "  ESP32SPIDER Ubuntu 22.04 Kurulum Scripti"
echo "=============================================="
echo ""

# ----- 1. Python bağımlılıkları -----
info "Python bağımlılıkları kontrol ediliyor..."
if ! command -v python3 &>/dev/null; then
    error "python3 bulunamadı. 'sudo apt install python3' ile kurun."
fi
PYTHON_VER=$(python3 -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')")
info "Python $PYTHON_VER bulundu."

if ! python3 -c "import serial" 2>/dev/null; then
    warn "pyserial bulunamadı, kuruluyor..."
    pip3 install pyserial --break-system-packages 2>/dev/null || pip3 install pyserial
fi
success "pyserial hazır."

# ----- 2. Serial port izinleri -----
info "Serial port grubu (dialout) kontrol ediliyor..."
if groups "$USER" | grep -q "dialout"; then
    success "Kullanıcı '$USER' zaten dialout grubunda."
else
    warn "dialout grubuna ekleniyor (oturum yenilenmesi gerekebilir)..."
    sudo usermod -aG dialout "$USER"
    warn "Değişiklik için oturumu kapatıp açmanız VEYA 'newgrp dialout' çalıştırmanız gerekiyor."
fi

# ----- 3. arduino-cli -----
info "arduino-cli kontrol ediliyor..."
if ! command -v arduino-cli &>/dev/null; then
    warn "arduino-cli bulunamadı, indiriliyor..."
    mkdir -p "$HOME/.local/bin"
    curl -fsSL https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Linux_64bit.tar.gz \
        -o /tmp/arduino-cli.tar.gz
    tar -xzf /tmp/arduino-cli.tar.gz -C "$HOME/.local/bin/" arduino-cli
    rm /tmp/arduino-cli.tar.gz
    # PATH'e ekle
    if ! grep -q '\.local/bin' "$HOME/.bashrc" 2>/dev/null; then
        echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$HOME/.bashrc"
    fi
    export PATH="$HOME/.local/bin:$PATH"
fi
success "arduino-cli $(arduino-cli version | grep -oP 'Version: \K[^ ]+')"

# ----- 4. ESP32 board desteği -----
info "arduino-cli yapılandırılıyor..."
arduino-cli config init --overwrite &>/dev/null || true
arduino-cli config set board_manager.additional_urls \
    https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

info "Board listesi güncelleniyor (internet gerekli, birkaç dakika sürebilir)..."
arduino-cli core update-index

if arduino-cli core list 2>/dev/null | grep -q "esp32:esp32"; then
    success "esp32:esp32 board paketi zaten yüklü."
else
    info "ESP32 board paketi yükleniyor (birkaç dakika sürebilir)..."
    arduino-cli core install esp32:esp32
    success "ESP32 board paketi yüklendi."
fi

# ----- 5. Adafruit NeoPixel kütüphanesi -----
info "Adafruit NeoPixel kütüphanesi kontrol ediliyor..."
if arduino-cli lib list 2>/dev/null | grep -q "Adafruit NeoPixel"; then
    success "Adafruit NeoPixel zaten yüklü."
else
    arduino-cli lib install "Adafruit NeoPixel"
    success "Adafruit NeoPixel yüklendi."
fi

# ----- 6. Özet -----
echo ""
echo "=============================================="
echo -e "  ${GREEN}Kurulum tamamlandı!${NC}"
echo "=============================================="
echo ""
echo "Sıradaki adımlar:"
echo "  1. ESP32'yi USB'ye takın"
echo "  2. Portu bul:    bash port_bul.sh"
echo "  3. Server yükle: bash yukle_server.sh /dev/ttyUSB0"
echo "  4. Client yükle: bash yukle_client.sh /dev/ttyUSB1"
echo "  5. İzle:         python3 ground_station.py"
echo ""
