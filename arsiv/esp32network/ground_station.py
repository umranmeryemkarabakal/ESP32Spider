# 3 robotun verisini ayrı ayrı gösterir

import serial
import serial.tools.list_ports
import json
import time
from datetime import datetime
from collections import defaultdict

BAUD_RATE = 115200
PORT = None  # Otomatik bulunacak

# Renkler (terminal için)
COLORS = {
    "SPIDER_01": "\033[96m",  # Cyan
    "SPIDER_02": "\033[92m",  # Green
    "SPIDER_03": "\033[93m",  # Yellow
    "SPIDER_04": "\033[95m",
}
RESET = "\033[0m"
RED = "\033[91m"

def find_esp32_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        desc = (port.description or "").lower()
        hwid = (port.hwid or "").lower()
        if any(c in desc or c in hwid for c in 
               ['cp210', 'ch340', 'ch341', 'ftdi', 'silicon labs', 'usb-serial']):
            return port.device
    return None

def format_robot_data(data):
    """JSON verisini okunabilir formatta döndürür"""
    robot_id = data.get("id", "BILINMIYOR")
    color = COLORS.get(robot_id, "")
    
    line = (
        f"{color}[{robot_id}]{RESET} "
        f"Konum: ({data.get('x', 0):+6.2f}, {data.get('y', 0):+6.2f}) | "
        f"Batarya: {data.get('batarya', 0):5.1f}% | "
        f"Sicaklik: {data.get('sicaklik', 0):4.1f}°C | "
        f"RSSI: {data.get('rssi', 0):4d} dBm | "
        f"Uptime: {data.get('uptime', 0)/1000:.1f}s"
    )
    return line

def main():
    global PORT
    
    if PORT is None:
        PORT = find_esp32_port()
        if PORT:
            print(f"ESP32 bulundu: {PORT}")
        else:
            PORT = input("Port adi girin (COM3 veya /dev/ttyUSB0): ").strip()
    
    if not PORT:
        print("Port secilmedi.")
        return
    
    print(f"\n{PORT} portu aciliyor...")
    
    # İstatistik sayaçları
    mesaj_sayaci = defaultdict(int)
    son_mesaj_zamani = defaultdict(float)
    
    try:
        ser = serial.Serial(PORT, BAUD_RATE, timeout=1)
        time.sleep(2)
        ser.reset_input_buffer()
        
        print("Baglanti acildi. Örümcek verileri bekleniyor...\n")
        print("=" * 100)
        
        son_ozet_zamani = time.time()
        
        while True:
            try:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if not line:
                    continue
                
                timestamp = datetime.now().strftime("%H:%M:%S")
                
                # JSON parse dene
                try:
                    data = json.loads(line)
                    robot_id = data.get("id", "?")
                    mesaj_sayaci[robot_id] += 1
                    son_mesaj_zamani[robot_id] = time.time()
                    
                    print(f"[{timestamp}] {format_robot_data(data)}")
                    
                except json.JSONDecodeError:
                    # JSON değilse ham veriyi göster
                    print(f"[{timestamp}] HAM: {line}")
                
                # 10 saniyede bir özet yaz
                if time.time() - son_ozet_zamani >= 10:
                    print("\n" + "-" * 100)
                    print("OZET (son 10 saniye):")
                    for robot_id, sayi in sorted(mesaj_sayaci.items()):
                        gecen = time.time() - son_mesaj_zamani[robot_id]
                        durum = "AKTIF" if gecen < 5 else f"{RED}SESSIZ{RESET}"
                        color = COLORS.get(robot_id, "")
                        print(f"  {color}{robot_id}{RESET}: {sayi} mesaj | Son: {gecen:.1f}s once | {durum}")
                    print("-" * 100 + "\n")
                    son_ozet_zamani = time.time()
                
            except UnicodeDecodeError:
                pass
            except serial.SerialException as e:
                print(f"{RED}Seri port hatasi: {e}{RESET}")
                break
                
    except serial.SerialException as e:
        print(f"{RED}Port acilamadi: {e}{RESET}")
    except KeyboardInterrupt:
        print("\n\nCikiliyor...")
        print("\nTOPLAM ISTATISTIK:")
        for robot_id, sayi in sorted(mesaj_sayaci.items()):
            print(f"  {robot_id}: {sayi} mesaj alindi")
    finally:
        try:
            ser.close()
        except:
            pass

if __name__ == "__main__":
    main()