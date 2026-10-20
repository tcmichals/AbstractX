# @impl [SPEC-USER-PLUGIN-01] [SPEC-USER-PLUGIN-02] [SPEC-E906-REPL-06] apps/e906_repl_app/SPECIFICATION.md
# raw_sensor_streamer.py - Pure Hardware Raw Sensor Streamer for XuanTie E906
#
# Production-grade freestanding MicroPython raw sensor streamer running natively
# on the Allwinner XuanTie E906 RISC-V co-processor inside AbstractX.
#
# Key Features:
#   1. Zero float math, zero string tokenization, zero regex parsing on E906.
#   2. Elevates QMC5883L I2C1 to 200 Hz continuous ODR (REG_CONTROL_1 = 0x1D).
#   3. Elevates u-blox NEO-M8N UART3 to 115200 baud & 10 Hz rate via UBX-CFG-PRT & RATE.
#   4. Emits raw hex-encoded packets over VirtIO RPMsg to Linux host / Studio bridge.

import abstractx

# ==============================================================================
# 1. Initialize & Maximize QMC5883L Compass (200 Hz Continuous ODR)
# ==============================================================================
abstractx.i2c_init(400000)
abstractx.sleep_ms(20)

# Soft Reset QMC5883L (0x0A = 0x80)
abstractx.i2c_write(0x1E, 0x0A, b"\x80")
abstractx.sleep_ms(20)

# Set Period Register (0x0B = 0x01)
abstractx.i2c_write(0x1E, 0x0B, b"\x01")

# Configure Control 1: Mode=Continuous (0b01), ODR=200Hz (0b11<<2),
# RNG=8G (0b01<<4), OSR=512 (0b00<<6) -> 0x1D
abstractx.i2c_write(0x1E, 0x09, b"\x1D")

# Fallback for HMC5883L continuous measurement (0x02 = 0x00)
abstractx.i2c_write(0x1E, 0x02, b"\x00")
abstractx.sleep_ms(20)

# ==============================================================================
# 2. Initialize & Maximize u-blox NEO-M8N GPS (115200 Baud @ 10 Hz)
# ==============================================================================
abstractx.uart_init(9600)
abstractx.sleep_ms(40)

# UBX-CFG-PRT: Elevate UART to 115200 baud (0x0001C200)
prt_cmd = bytes([
    0xB5, 0x62, 0x06, 0x00, 0x14, 0x00,
    0x01, 0x00, 0x00, 0x00,
    0xD0, 0x08, 0x00, 0x00,
    0x00, 0xC2, 0x01, 0x00,
    0x07, 0x00, 0x03, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x64, 0x7A
])
abstractx.uart_write(prt_cmd)
abstractx.sleep_ms(50)

# Switch XuanTie E906 UART3 divisor to 115200 baud
abstractx.uart_init(115200)
abstractx.sleep_ms(50)

# UBX-CFG-MSG: Suppress unneeded NMEA sentences to preserve bus bandwidth
abstractx.uart_write(bytes([0xB5, 0x62, 0x06, 0x01, 0x03, 0x00, 0xF0, 0x03, 0x00, 0xFD, 0x15])) # GSV off
abstractx.uart_write(bytes([0xB5, 0x62, 0x06, 0x01, 0x03, 0x00, 0xF0, 0x02, 0x00, 0xFC, 0x13])) # GSA off

# UBX-CFG-RATE: Set 10 Hz solution rate (100 ms period)
abstractx.uart_write(bytes([0xB5, 0x62, 0x06, 0x08, 0x06, 0x00, 0x64, 0x00, 0x01, 0x00, 0x01, 0x00, 0x7A, 0x12]))
abstractx.sleep_ms(50)

print("[E906_RAW_STREAMER_READY: I2C=400kHz/200Hz, UART=115200/10Hz]")

# ==============================================================================
# 3. Fast Streaming Loop: Non-blocking raw I/O forwarding
# ==============================================================================
hex_chars = "0123456789abcdef"

def to_hex(raw_b):
    if not raw_b:
        return "none"
    out = ""
    for i in range(len(raw_b)):
        b = raw_b[i]
        out += hex_chars[(b >> 4) & 0x0F] + hex_chars[b & 0x0F]
    return out

# Streaming execution loop (emits 30 samples)
for tick in range(30):
    raw_mag = abstractx.i2c_read(0x1E, 0x00, 6)
    raw_gps = abstractx.uart_read(128)

    mag_hex = to_hex(raw_mag) if len(raw_mag) == 6 else "none"

    if raw_gps and len(raw_gps) > 0:
        gps_hex = to_hex(raw_gps)
        print("!RAW:M=" + mag_hex + ":G=" + gps_hex)
    else:
        print("!RAW:M=" + mag_hex)

    abstractx.sleep_ms(100)

print("[E906_RAW_STREAMER_COMPLETE]")
