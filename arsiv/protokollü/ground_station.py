# Yer istasyonu CLI - 3 robot verisini ayrı gösterir + komut gönderme
#
# Kullanım:
#   - Çalıştır, ESP32'yi otomatik bulur
#   - Telemetri ekrana akar
#   - Komut göndermek için: SPIDER_3:DUR  veya  SPIDER_3:KONUM_SIFIRLA
#     (komut yer istasyonu ESP32'sinin seri portuna gönderilir)

import serial
import serial.tools.list_ports
import json
import time
import threading
import sys
from datetime import datetime
from collections import defaultdict

BAUD_RATE = 115200
PORT = None

COLORS = {
    "SPIDER_01": "\033[96m",
    "SPIDER_02": "\033[92m",
    "SPIDER_03": "\033[93m",
    "SPIDER_1":  "\033[96m",
    "SPIDER_2":  "\033[92m",
    "SPIDER_3":  "\033[93m",
    "GROUND":    "\033[95m",
}
RESET = "\033[0m"
RED   = "\033[91m"
GRAY  = "\033[90m"
BOLD  = "\033[1m"

# CRC8 (polinom 0x07) — ESP32 ile aynı
def crc8(data: str) -> int:
    crc = 0
    for ch in data.encode("utf-8", errors="ignore"):
        crc ^= ch
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc

def crc_dogrula(msg: dict) -> bool:
    try:
        govde = (
            str(msg.get("id", ""))
            + str(msg.get("seq", ""))
            + str(msg.get("type", ""))
        )
        payload = msg.get("payload")
        if payload is not None:
            # ESP32 tarafında payload tam JSON string olarak alınıyor.
            # Python tarafında dict’e çevriliyor, tekrar string'e çevirirken
            # boşluksuz (separators) olmalı ki CRC eşleşsin.
            govde += json.dumps(payload, separators=(",", ":"), ensure_ascii=False)
        beklenen = "%02X" % crc8(govde)
        return beklenen == msg.get("crc", "").upper()
    except Exception:
        return False

def find_esp32_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        desc = (port.description or "").lower()
        hwid = (port.hwid or "").lower()
        if any(c in desc or c in hwid for c in
               ['cp210', 'ch340', 'ch341', 'ftdi', 'silicon labs', 'usb-serial']):
            return port.device
    return None

def format_data(msg):
    p = msg.get("payload", {}) or {}
    robot_id = msg.get("id", "?")
    color = COLORS.get(robot_id, "")
    seq = msg.get("seq", "?")
    return (
        f"{color}[{robot_id} #{seq:>4}]{RESET} "
        f"Konum:({p.get('x', 0):+6.2f},{p.get('y', 0):+6.2f}) | "
        f"Bat:{p.get('batarya', 0):5.1f}% | "
        f"T:{p.get('sicaklik', 0):4.1f}°C | "
        f"RSSI:{p.get('rssi', 0):4d} | "
        f"Up:{p.get('uptime', 0)/1000:6.1f}s"
    )

# ===== Komut gönderme thread'i =====
def komut_thread(ser):
    """Kullanıcıdan stdin okur, ESP32'ye gönderir.
    Format: ROBOT_ID:KOMUT  (örn: SPIDER_3:DUR)
    """
    print(f"{GRAY}[Komut girisi aktif. Format: ROBOT_ID:KOMUT  (orn: SPIDER_3:DUR)]{RESET}\n")
    while True:
        try:
            satir = sys.stdin.readline()
            if not satir:
                break
            satir = satir.strip()
            if not satir:
                continue
            if ":" not in satir:
                print(f"{RED}Yanlis format. Ornek: SPIDER_3:DUR{RESET}")
                continue
            ser.write((satir + "\n").encode("utf-8"))
            ser.flush()
            print(f"{GRAY}[CLI] gonderildi -> {satir}{RESET}")
        except Exception as e:
            print(f"{RED}Komut hatasi: {e}{RESET}")
            break

def main():
    global PORT
    if PORT is None:
        PORT = find_esp32_port()
        if PORT:
            print(f"ESP32 bulundu: {PORT}")
        else:
            PORT = input("Port adi girin (COM3 / /dev/ttyUSB0): ").strip()
    if not PORT:
        print("Port secilmedi.")
        return

    print(f"\n{PORT} portu aciliyor...")

    # İstatistikler
    data_sayaci   = defaultdict(int)
    crc_hata      = defaultdict(int)
    son_seq       = {}                     # robot_id -> son seq
    atlanan_seq   = defaultdict(int)       # robot_id -> kayıp/atlanmış sayısı
    son_mesaj_zamani = defaultdict(float)
    diger_mesajlar = defaultdict(lambda: defaultdict(int))  # id -> {type -> sayı}

    try:
        ser = serial.Serial(PORT, BAUD_RATE, timeout=1)
        time.sleep(2)
        ser.reset_input_buffer()

        # Komut thread'ini başlat
        t = threading.Thread(target=komut_thread, args=(ser,), daemon=True)
        t.start()

        print("Baglanti acildi. Veriler bekleniyor...\n")
        print("=" * 110)

        son_ozet = time.time()

        while True:
            try:
                raw = ser.readline().decode('utf-8', errors='ignore').strip()
                if not raw:
                    continue

                ts = datetime.now().strftime("%H:%M:%S")

                # JSON parse dene
                try:
                    msg = json.loads(raw)
                except json.JSONDecodeError:
                    # Yer istasyonunun kendi log satırı (TX/RX/CLI) — olduğu gibi göster
                    print(f"{GRAY}[{ts}] {raw}{RESET}")
                    continue

                rid = msg.get("id", "?")
                mtype = msg.get("type", "?")

                # CRC kontrolü (telemetri için)
                if mtype in ("DATA", "CMD") and not crc_dogrula(msg):
                    crc_hata[rid] += 1
                    print(f"{RED}[{ts}] CRC HATA ({rid}) seq={msg.get('seq')}{RESET}")
                    continue

                if mtype == "DATA":
                    data_sayaci[rid] += 1
                    son_mesaj_zamani[rid] = time.time()

                    # Atlanan seq tespiti
                    seq = msg.get("seq")
                    if isinstance(seq, int):
                        if rid in son_seq:
                            beklenen = son_seq[rid] + 1
                            # Tekrar gelen aynı seq idempotency'den geliyor olabilir, atla
                            if seq == son_seq[rid]:
                                pass
                            elif seq != beklenen and seq > son_seq[rid]:
                                atlanan_seq[rid] += (seq - beklenen)
                        son_seq[rid] = seq

                    print(f"[{ts}] {format_data(msg)}")
                else:
                    # ACK/NACK/PING/PONG sayaçları (debug için)
                    diger_mesajlar[rid][mtype] += 1
                    color = COLORS.get(rid, "")
                    print(f"{GRAY}[{ts}] {color}{rid}{RESET}{GRAY} <- {mtype} "
                          f"seq={msg.get('seq')} payload={msg.get('payload', {})}{RESET}")

                # 10 saniyede bir özet
                if time.time() - son_ozet >= 10:
                    print("\n" + "-" * 110)
                    print(f"{BOLD}OZET (son 10 sn):{RESET}")
                    tum_idler = set(list(data_sayaci.keys()) + list(diger_mesajlar.keys()))
                    for rid in sorted(tum_idler):
                        gecen = time.time() - son_mesaj_zamani.get(rid, 0)
                        durum = "AKTIF" if gecen < 5 else f"{RED}SESSIZ{RESET}"
                        color = COLORS.get(rid, "")
                        diger = diger_mesajlar[rid]
                        diger_str = " ".join(f"{k}:{v}" for k, v in diger.items()) if diger else "-"
                        print(f"  {color}{rid:<10}{RESET} "
                              f"DATA:{data_sayaci[rid]:>4} "
                              f"CRC_HATA:{crc_hata[rid]:>2} "
                              f"ATLANAN:{atlanan_seq[rid]:>3} "
                              f"DIGER:[{diger_str}] "
                              f"Son:{gecen:>4.1f}s | {durum}")
                    print("-" * 110 + "\n")
                    son_ozet = time.time()

            except UnicodeDecodeError:
                pass
            except serial.SerialException as e:
                print(f"{RED}Seri port hatasi: {e}{RESET}")
                break

    except serial.SerialException as e:
        print(f"{RED}Port acilamadi: {e}{RESET}")
    except KeyboardInterrupt:
        print(f"\n\n{BOLD}TOPLAM ISTATISTIK:{RESET}")
        for rid in sorted(set(list(data_sayaci.keys()) + list(crc_hata.keys()))):
            print(f"  {rid}: DATA={data_sayaci[rid]} "
                  f"CRC_HATA={crc_hata[rid]} ATLANAN={atlanan_seq[rid]}")
    finally:
        try:
            ser.close()
        except Exception:
            pass

if __name__ == "__main__":
    main()
