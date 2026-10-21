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
# 1. Compass (HMC5883L or QMC5883L, continuous)
# ==============================================================================
abstractx.i2c_init(400000)
abstractx.sleep_ms(20)
# Compass on TWI1: the A5E board carries an HMC5883L at 0x1E (ID registers 0x0A..0x0C read
# b"H43", measured 2026-10-21); a QMC5883L would sit at 0x0D. mag_init()/mag_read() hide the
# difference; bytes are sent as read and tagged with the chip so the host decodes them.
MAG_HMC, MAG_QMC = 0x1E, 0x0D


def mag_init():
    if bytes(abstractx.i2c_read(MAG_HMC, 0x0A, 3)) == b"H43":
        abstractx.i2c_write(MAG_HMC, 0x00, b"\x70")  # CRA: 8-sample average, 15 Hz
        abstractx.i2c_write(MAG_HMC, 0x01, b"\x20")  # CRB: +-1.3 Ga
        abstractx.i2c_write(MAG_HMC, 0x02, b"\x00")  # MR: continuous
        return MAG_HMC
    abstractx.i2c_write(MAG_QMC, 0x0B, b"\x01")      # QMC5883L set/reset period
    abstractx.i2c_write(MAG_QMC, 0x09, b"\x1D")      # continuous, 200 Hz, 8 G
    return MAG_QMC


def mag_read(addr):
    # Data registers exactly as read; the host decodes them (abstractx_wire.decode_mag).
    if addr == MAG_HMC:
        return abstractx.i2c_read(MAG_HMC, 0x03, 6)  # X, Z, Y big-endian; reading all 6 unlocks
    return abstractx.i2c_read(MAG_QMC, 0x00, 6)      # X, Y, Z little-endian


def mag_chip(addr):
    return "HMC5883L" if addr == MAG_HMC else "QMC5883L"


def mag_xyz(addr, d):
    """Signed X, Y, Z from mag_read() bytes (for headings computed on the E906)."""
    if addr == MAG_HMC:
        v = ((d[0] << 8) | d[1], (d[4] << 8) | d[5], (d[2] << 8) | d[3])
    else:
        v = (d[0] | (d[1] << 8), d[2] | (d[3] << 8), d[4] | (d[5] << 8))
    return tuple(a - 65536 if a > 32767 else a for a in v)


MAG_ADDR = mag_init()
MAG_CHIP = mag_chip(MAG_ADDR)
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
def to_hex(raw_b):
    if not raw_b:
        return "none"
    return "".join(["%02x" % b for b in raw_b])

# Streaming execution loop with Heartbeat Watchdog & Clean Teardown
# @impl [SPEC-E906-REPL-14] apps/e906_repl_app/SPECIFICATION.md
last_heartbeat_ms = abstractx.ticks_ms()
WATCHDOG_TIMEOUT_MS = 3000

# Until stopped (STOP / Ctrl-C) or the host stops pinging; no fixed tick count.
while True:
    # Check for host ping or stop commands over RPMsg
    cmd = abstractx.console_read(64)
    if cmd:
        if b"PING" in cmd or b"HEARTBEAT" in cmd:
            last_heartbeat_ms = abstractx.ticks_ms()
        elif b"STOP" in cmd or b"CLOSE" in cmd or b"\x03" in cmd:
            print("[E906_STOP_REQUESTED]")
            break

    # Watchdog protection: shutdown if host disconnects or stops pinging
    if abstractx.ticks_ms() - last_heartbeat_ms > WATCHDOG_TIMEOUT_MS:
        print("[E906_WATCHDOG_TIMEOUT_SHUTDOWN]")
        break

    raw_mag = mag_read(MAG_ADDR)
    # Drain everything the UART ISR buffered: GPS at 115200 baud / 10 Hz outruns 128 B per
    # 100 ms tick, the RX ring overflowed and NMEA sentences arrived garbled.
    raw_gps = abstractx.uart_read(512)
    while len(raw_gps) and len(raw_gps) % 512 == 0:
        more = abstractx.uart_read(512)
        if not more:
            break
        raw_gps += more

    mag_hex = to_hex(raw_mag) if len(raw_mag) == 6 else "none"

    if raw_gps and len(raw_gps) > 0:
        gps_hex = to_hex(raw_gps)
        print("!RAW:M=" + mag_hex + ":C=" + MAG_CHIP + ":G=" + gps_hex)
    else:
        print("!RAW:M=" + mag_hex + ":C=" + MAG_CHIP)

    abstractx.sleep_ms(100)

print("[E906_RAW_STREAMER_COMPLETE]")
