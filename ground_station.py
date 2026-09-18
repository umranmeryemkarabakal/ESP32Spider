# ============================================================================
#  Yer istasyonu CLI  —  TUM FAZLAR
# ----------------------------------------------------------------------------
#  - 3+ robot verisini ayri renkte gosterir
#  - Komut gonderir:  SPIDER_3:DUR   /   SPIDER_3:KONUM_SIFIRLA
#  - Test komutlarini server'a iletir:  T DROP_ACK 1   /   T DROP_CMD 1
#  - Tum cikti otomatik olarak log dosyasina da yazilir (ANSI temizlenmis)
#  - Ozetlerde gecen sure ve mesaj/dakika orani (T17 stres testi icin)
#
#  Kullanim:
#    python ground_station.py                -> port otomatik bulunur/sorulur
#    python ground_station.py COM17          -> port elle verilir
#    python ground_station.py COM17 mylog.txt-> log dosya adi da verilir
# ============================================================================

import serial
import serial.tools.list_ports
import json
import time
import threading
import sys
import re
from datetime import datetime
from collections import defaultdict

BAUD_RATE = 115200
PORT = None
LOGFILE = None

COLORS = {
    "SPIDER_01": "\033[96m", "SPIDER_02": "\033[92m", "SPIDER_03": "\033[93m",
    "SPIDER_1":  "\033[96m", "SPIDER_2":  "\033[92m", "SPIDER_3":  "\033[93m",
    "GROUND":    "\033[95m",
}
RESET = "\033[0m"; RED = "\033[91m"; GRAY = "\033[90m"; BOLD = "\033[1m"

# ---- Tee: stdout'u hem ekrana hem dosyaya yaz (dosyada ANSI renk kodu olmasin)
ANSI_RE = re.compile(r"\033\[[0-9;]*m")
class Tee:
    def __init__(self, stream, fileobj):
        self.stream = stream; self.file = fileobj
    def write(self, data):
        self.stream.write(data)
        self.file.write(ANSI_RE.sub("", data))
    def flush(self):
        self.stream.flush(); self.file.flush()

# ---- CRC8 (polinom 0x07) — ESP32 ile ayni
def crc8(data: str) -> int:
    crc = 0
    for ch in data.encode("utf-8", errors="ignore"):
        crc ^= ch
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc

def raw_field(raw: str, field: str) -> str:
    """JSON string'inden bir alanin HAM substring degerini cikarir (ESP32 alanAl ile ayni)."""
    key = '"' + field + '"'
    p = raw.find(key)
    if p < 0: return ""
    p = raw.find(":", p)
    if p < 0: return ""
    p += 1
    while p < len(raw) and raw[p] in " \t": p += 1
    if p >= len(raw): return ""
    if raw[p] == '"':
        end = raw.find('"', p + 1)
        return raw[p + 1:end] if end > 0 else ""
    if raw[p] == "{":
        derinlik = 1; q = p + 1
        while q < len(raw) and derinlik > 0:
            if raw[q] == "{": derinlik += 1
            elif raw[q] == "}": derinlik -= 1
            q += 1
        return raw[p:q]
    end = p
    while end < len(raw) and raw[end] not in ",}": end += 1
    return raw[p:end].strip()

def crc_dogrula(raw: str) -> bool:
    try:
        msg_id   = raw_field(raw, "id")
        seq_str  = raw_field(raw, "seq")
        msg_type = raw_field(raw, "type")
        crc_recvd= raw_field(raw, "crc")
        payload  = raw_field(raw, "payload")
        govde = msg_id + seq_str + msg_type + payload
        beklenen = "%02X" % crc8(govde)
        return beklenen.upper() == crc_recvd.upper()
    except Exception:
        return False

def find_esp32_port():
    for port in serial.tools.list_ports.comports():
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

# D5: pyserial thread-safe degil; komut_thread (yazma) ve ana thread (okuma)
# ayni serial.Serial nesnesini paylastigi icin bir Lock ile korunur.
ser_lock = threading.Lock()

# ===== Komut gonderme thread'i =====
def komut_thread(ser):
    print(f"{GRAY}[Komut girisi aktif. Ornek: SPIDER_3:DUR  veya  T DROP_ACK 1]{RESET}\n")
    while True:
        try:
            satir = sys.stdin.readline()
            if not satir: break
            satir = satir.strip()
            if not satir: continue
            if ":" not in satir and not satir.startswith("T "):
                print(f"{RED}Yanlis format. Ornek: SPIDER_3:DUR  veya  T DROP_ACK 1{RESET}")
                continue
            with ser_lock:
                ser.write((satir + "\n").encode("utf-8"))
                ser.flush()
            print(f"{GRAY}[CLI] gonderildi -> {satir}{RESET}")
        except Exception as e:
            print(f"{RED}Komut hatasi: {e}{RESET}")
            break

def main():
    global PORT, LOGFILE

    # ---- argumanlar: [port] [logfile]
    args = sys.argv[1:]
    if len(args) >= 1: PORT = args[0]
    if len(args) >= 2: LOGFILE = args[1]

    if PORT is None:
        PORT = find_esp32_port()
        if PORT: print(f"ESP32 bulundu: {PORT}")
        else:    PORT = input("Port adi girin (COM3 / /dev/ttyUSB0): ").strip()
    if not PORT:
        print("Port secilmedi."); return

    # ---- log dosyasi (tee)
    if LOGFILE is None:
        LOGFILE = "log_" + datetime.now().strftime("%Y%m%d_%H%M%S") + ".txt"
    try:
        logf = open(LOGFILE, "w", encoding="utf-8")
        sys.stdout = Tee(sys.stdout, logf)
        print(f"{GRAY}[Log dosyasi: {LOGFILE}]{RESET}")
    except Exception as e:
        print(f"{RED}Log dosyasi acilamadi ({e}), sadece ekrana yazilacak{RESET}")

    print(f"\n{PORT} portu aciliyor...")

    # ---- istatistikler
    data_sayaci      = defaultdict(int)
    crc_hata         = defaultdict(int)
    son_seq          = {}
    atlanan_seq      = defaultdict(int)
    son_mesaj_zamani = defaultdict(float)
    diger_mesajlar   = defaultdict(lambda: defaultdict(int))
    baslangic = time.time()

    try:
        ser = serial.Serial(PORT, BAUD_RATE, timeout=1)
        time.sleep(2)
        ser.reset_input_buffer()

        t = threading.Thread(target=komut_thread, args=(ser,), daemon=True)
        t.start()

        print("Baglanti acildi. Veriler bekleniyor...\n")
        print("=" * 110)
        son_ozet = time.time()

        while True:
            try:
                with ser_lock:
                    raw = ser.readline().decode('utf-8', errors='ignore').strip()
                if not raw: continue
                ts = datetime.now().strftime("%H:%M:%S")

                try:
                    msg = json.loads(raw)
                except json.JSONDecodeError:
                    # Server'in kendi log satiri (TX/RX/CLI/HEAP/TEST) — oldugu gibi goster
                    print(f"{GRAY}[{ts}] {raw}{RESET}")
                    continue

                rid   = msg.get("id", "?")
                mtype = msg.get("type", "?")

                if mtype in ("DATA", "CMD") and not crc_dogrula(raw):
                    crc_hata[rid] += 1
                    print(f"{RED}[{ts}] CRC HATA ({rid}) seq={msg.get('seq')}{RESET}")
                    continue

                if mtype == "DATA":
                    data_sayaci[rid] += 1
                    son_mesaj_zamani[rid] = time.time()
                    seq = msg.get("seq")
                    if isinstance(seq, int):
                        if rid in son_seq:
                            beklenen = son_seq[rid] + 1
                            if seq == son_seq[rid]:
                                pass  # idempotency tekrari, atla
                            elif son_seq[rid] > 60000 and seq < 100:
                                pass  # O3: seq rollover (65535->1), normal
                            elif seq != beklenen and seq > son_seq[rid]:
                                atlanan_seq[rid] += (seq - beklenen)
                        son_seq[rid] = seq
                    print(f"[{ts}] {format_data(msg)}")
                else:
                    diger_mesajlar[rid][mtype] += 1
                    color = COLORS.get(rid, "")
                    print(f"{GRAY}[{ts}] {color}{rid}{RESET}{GRAY} <- {mtype} "
                          f"seq={msg.get('seq')} payload={msg.get('payload', {})}{RESET}")

                # 10 saniyede bir ozet
                if time.time() - son_ozet >= 10:
                    gecen_toplam = time.time() - baslangic
                    print("\n" + "-" * 110)
                    print(f"{BOLD}OZET{RESET}  (toplam sure: {gecen_toplam:.0f}s)")
                    tum_idler = set(list(data_sayaci.keys()) + list(diger_mesajlar.keys()))
                    for rid in sorted(tum_idler):
                        gecen = time.time() - son_mesaj_zamani.get(rid, 0)
                        durum = "AKTIF" if gecen < 5 else f"{RED}SESSIZ{RESET}"
                        color = COLORS.get(rid, "")
                        diger = diger_mesajlar[rid]
                        diger_str = " ".join(f"{k}:{v}" for k, v in diger.items()) if diger else "-"
                        oran = data_sayaci[rid] / (gecen_toplam/60) if gecen_toplam > 0 else 0
                        print(f"  {color}{rid:<10}{RESET} "
                              f"DATA:{data_sayaci[rid]:>5} "
                              f"({oran:4.1f}/dk) "
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
        gecen_toplam = time.time() - baslangic
        print(f"\n\n{BOLD}TOPLAM ISTATISTIK{RESET}  (calisma suresi: {gecen_toplam:.0f}s = {gecen_toplam/60:.1f}dk)")
        for rid in sorted(set(list(data_sayaci.keys()) + list(crc_hata.keys()))):
            toplam = data_sayaci[rid] + crc_hata[rid]
            crc_oran = (crc_hata[rid] / toplam * 100) if toplam else 0
            oran = data_sayaci[rid] / (gecen_toplam/60) if gecen_toplam > 0 else 0
            print(f"  {rid}: DATA={data_sayaci[rid]} ({oran:.1f}/dk) "
                  f"CRC_HATA={crc_hata[rid]} ({crc_oran:.2f}%) "
                  f"ATLANAN={atlanan_seq[rid]}")
    finally:
        try: ser.close()
        except Exception: pass

if __name__ == "__main__":
    main()
