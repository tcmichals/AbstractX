#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-GPS-01] [SPEC-E906-ARCH-02] docs/tier3_targets/hardware/CUBIE_A5E_NEO_M8N_GPS_GUIDE.md
"""
Sensor Test Script for Radxa Cubie A5E
Tests:
  1. Serial GPS on /dev/ttyAS3 (9600 baud, PI11/PI12 - Pins 16 and 31)
  2. Compass/DDC I2C on /dev/i2c-1 (PB5/PB4 - Pins 3 and 5)
"""

import sys
import os
import time
import subprocess
import select

SERIAL_PORT = "/dev/ttyAS3" if os.path.exists("/dev/ttyAS3") else "/dev/ttyS3"
DEFAULT_BAUD = 9600
I2C_BUS = 1

COMMON_I2C_DEVICES = {
    0x0C: "AK09916 / AK09918 Magnetometer",
    0x0D: "QMC5883L Magnetometer",
    0x1E: "HMC5883L / LIS3MDL Magnetometer",
    0x42: "u-blox NEO-M8N DDC Port",
    0x68: "MPU6050 / ICM-20602 IMU",
    0x69: "ICM-42688-P IMU (alternate)",
    0x76: "BMP280 / BME280 Barometer (0x76)",
    0x77: "BMP280 / MS5611 Barometer (0x77)",
}

def print_header(title):
    print("\n" + "=" * 60)
    print(f"  {title}")
    print("=" * 60)

def read_hw_regs():
    """Read PIO registers via devmem to inspect pin states."""
    regs = {}
    for addr, name in [
        (0x02000030, "PB_CFG0"),
        (0x02000054, "PB_PULL0"),
        (0x02000124, "PI_CFG1"),
        (0x02000134, "PI_DAT")
    ]:
        try:
            out = subprocess.check_output(["devmem", hex(addr)], text=True).strip()
            val = int(out, 16)
            regs[name] = val
        except Exception:
            regs[name] = None
    return regs

def test_hardware_pins():
    print_header("Hardware Pinmux & Level Diagnostics")
    regs = read_hw_regs()
    
    cfg_pb = regs.get("PB_CFG0")
    if cfg_pb is not None:
        pb4_mux = (cfg_pb >> 16) & 0xF
        pb5_mux = (cfg_pb >> 20) & 0xF
        print(f"[*] PB_CFG0: 0x{cfg_pb:08X}")
        print(f"    - PB4 (Pin 5  / I2C1-SCL):  Mux {pb4_mux}")
        print(f"    - PB5 (Pin 3  / I2C1-SDA):  Mux {pb5_mux}")

    cfg_pi = regs.get("PI_CFG1")
    if cfg_pi is not None:
        pi11_mux = (cfg_pi >> 12) & 0xF
        pi12_mux = (cfg_pi >> 16) & 0xF
        print(f"[*] PI_CFG1: 0x{cfg_pi:08X}")
        print(f"    - PI11 (Pin 16 / UART3-TX): Mux {pi11_mux}")
        print(f"    - PI12 (Pin 31 / UART3-RX): Mux {pi12_mux}")

    pull = regs.get("PB_PULL0")
    if pull is not None:
        print(f"[*] PB_PULL0: 0x{pull:08X} (Internal Pull-Ups active)")

    print("\nWiring Reference for 40-Pin Header:")
    print("  [Pin 2 ] 5V DC    -> GPS Red (VCC)")
    print("  [Pin 6 ] GND      -> GPS Black (GND)")
    print("  [Pin 16] Cubie TX -> GPS Yellow (GPS RX)")
    print("  [Pin 31] Cubie RX -> GPS Green (GPS TX)")
    print("  [Pin 3 ] I2C SDA  -> Compass/GPS SDA (PB5)")
    print("  [Pin 5 ] I2C SCL  -> Compass/GPS SCL (PB4)")

def test_serial(timeout_sec=10, baud=DEFAULT_BAUD):
    print_header(f"Testing Serial GPS ({SERIAL_PORT} @ {baud} baud)")
    if not os.path.exists(SERIAL_PORT):
        print(f"[ERROR] Serial port {SERIAL_PORT} does not exist!")
        return False

    # Configure port via stty
    subprocess.run(["stty", "-F", SERIAL_PORT, str(baud), "raw", "-echo"], check=False)

    try:
        import serial
        ser = serial.Serial(SERIAL_PORT, baud, timeout=1.0)
    except Exception as e:
        print(f"[ERROR] Failed to open {SERIAL_PORT}: {e}")
        return False

    print(f"[*] Listening for incoming GPS NMEA sentences (timeout: {timeout_sec}s)...")
    print("    (Press Ctrl-C to stop)\n")

    start_time = time.time()
    packet_count = 0
    raw_bytes = 0

    try:
        while time.time() - start_time < timeout_sec:
            line = ser.readline()
            if line:
                raw_bytes += len(line)
                decoded = line.decode('latin1', errors='replace').strip()
                if decoded:
                    packet_count += 1
                    print(f"[{packet_count:03d}] {decoded}")
                    if packet_count >= 15:
                        print("\n[SUCCESS] Successfully ingested 15 GPS packets!")
                        break
            else:
                # Print liveness dot
                sys.stdout.write(".")
                sys.stdout.flush()
    except KeyboardInterrupt:
        print("\n[*] Stopped by user.")
    finally:
        ser.close()

    print(f"\n[*] Total received: {packet_count} sentences ({raw_bytes} bytes).")
    if packet_count == 0:
        print("[!] No data received. Check:")
        print("    1. Is GPS Yellow plugged into Pin 13 (Cubie RX)?")
        print("    2. Is GPS Green plugged into Pin 11 (Cubie TX)?")
        print("    3. Is GPS getting 5V power on Pin 2 and GND on Pin 6?")
        return False
    return True

def test_i2c():
    print_header(f"Testing I2C Bus ({I2C_BUS} / Pins 3 & 5)")
    dev_path = f"/dev/i2c-{I2C_BUS}"
    if not os.path.exists(dev_path):
        print(f"[ERROR] I2C device {dev_path} not found!")
        return False

    print(f"[*] Running i2cdetect on bus {I2C_BUS}...")
    try:
        proc = subprocess.run(["i2cdetect", "-y", str(I2C_BUS)], capture_output=True, text=True, timeout=15)
        print(proc.stdout)
        if proc.stderr:
            print("[STDERR]:", proc.stderr.strip())

        # Check for detected devices (numbers like 0c, 1e, 68)
        detected = []
        for line in proc.stdout.splitlines()[1:]:
            parts = line.split(":", 1)
            if len(parts) == 2:
                cells = parts[1].split()
                for cell in cells:
                    if cell not in ("--", "UU", ""):
                        try:
                            addr = int(cell, 16)
                            detected.append(addr)
                        except ValueError:
                            pass

        if detected:
            print("[SUCCESS] Found I2C devices:")
            for addr in detected:
                desc = COMMON_I2C_DEVICES.get(addr, "Unknown I2C Device")
                print(f"  -> 0x{addr:02X} ({desc})")
            return True
        else:
            print("[!] No devices responded on I2C bus.")
            # Check dmesg for bus lock
            dmesg = subprocess.check_output(["dmesg"], text=True)
            if "I2C bus locked" in dmesg:
                print("    [WARNING] 'I2C bus locked' detected in dmesg.")
                print("    Check: Pins 3 (SDA) and 5 (SCL) might be held at 0V or shorted to GND.")
            return False

    except subprocess.TimeoutExpired:
        print("[TIMEOUT] i2cdetect timed out. Lines may be held LOW.")
        return False
    except Exception as e:
        print(f"[ERROR] Failed to run i2cdetect: {e}")
        return False

def main():
    test_hardware_pins()
    
    print("\nSelect test:")
    print("  1) Test Serial GPS only")
    print("  2) Test I2C Compass only")
    print("  3) Test Both (Serial + I2C)")
    print("  q) Quit")

    if len(sys.argv) > 1:
        choice = sys.argv[1].strip("-")
    else:
        try:
            choice = input("\nEnter choice [1/2/3/q]: ").strip()
        except (KeyboardInterrupt, EOFError):
            print()
            return

    if choice in ("1", "serial", "s"):
        test_serial()
    elif choice in ("2", "i2c", "i"):
        test_i2c()
    elif choice in ("3", "both", "b"):
        print("\n--- Running Serial Test ---")
        test_serial(timeout_sec=5)
        print("\n--- Running I2C Test ---")
        test_i2c()
    else:
        print("Exiting.")

if __name__ == "__main__":
    main()
