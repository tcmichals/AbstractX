#!/usr/bin/env python3
"""
AbstractX Linux TUN over SPI Bridge (asp_tun_driver.py)
Implements testing bridge for the ASP protocol using spidev and /dev/net/tun.
"""

import os
import struct
import spidev
import time
from fcntl import ioctl

# TUN/TAP ioctl constants
TUNSETIFF = 0x400454ca
IFF_TUN   = 0x0001
IFF_NO_PI = 0x1000

# ASP SPI Commands
CMD_WRITE_DATA  = 0x80
CMD_READ_STATUS = 0x01
CMD_READ_DATA   = 0x02

class AbstractXTUNDriver:
    def __init__(self, spi_bus=0, spi_device=0, tun_name="tun0"):
        print(f"Initializing SPI bus {spi_bus}.{spi_device} at 5MHz...")
        self.spi = spidev.SpiDev()
        try:
            self.spi.open(spi_bus, spi_device)
            self.spi.max_speed_hz = 5000000
            self.spi.mode = 0
        except FileNotFoundError:
            print(f"Warning: /dev/spidev{spi_bus}.{spi_device} not found. Running in dry-run/mock mode.")
            self.spi = None

        print(f"Creating TUN interface {tun_name}...")
        self.tun_fd = os.open("/dev/net/tun", os.O_RDWR | os.O_NONBLOCK)
        ifr = struct.pack('16sH', tun_name.encode('utf-8'), IFF_TUN | IFF_NO_PI)
        ioctl(self.tun_fd, TUNSETIFF, ifr)
        
        # Bring interface up via system command
        os.system(f"ip link set {tun_name} up")
        os.system(f"ip addr add 10.0.0.1/24 dev {tun_name}")
        
        print(f"AbstractX TUN driver running. Interface {tun_name} is up (10.0.0.1).")

    def read_status(self):
        if not self.spi: return 1, 0, 0
        
        # Request status: [CMD] + 4 dummy clocks for [VER, STATUS, LEN_H, LEN_L]
        resp = self.spi.xfer2([CMD_READ_STATUS, 0x00, 0x00, 0x00, 0x00])
        version = resp[1]
        status = resp[2]
        rx_len = (resp[3] << 8) | resp[4]
        return version, status, rx_len

    def read_data(self, rx_len):
        if rx_len == 0 or not self.spi:
            return b""
        # Request data: [CMD] + dummy clocks for payload
        req = [CMD_READ_DATA] + [0x00] * rx_len
        resp = self.spi.xfer2(req)
        return bytes(resp[1:])

    def write_data(self, payload):
        if not self.spi: return
        req = [CMD_WRITE_DATA] + list(payload)
        self.spi.xfer2(req)

    def run(self):
        seq = 0
        while True:
            # 1. Read from TUN (Linux -> FPGA)
            try:
                packet = os.read(self.tun_fd, 2048)
                if packet:
                    # Wrap IP packet in ASP Frame
                    # Format: Sync(0xA5), Ver(1), Flags(0), AXID(2=TUN), Seq, PayloadLen, Payload, CRC16
                    payload_len = len(packet)
                    asp_header = struct.pack(">BBBBHH", 0xA5, 1, 0, 2, seq, payload_len)
                    # Mock CRC for now (0x0000)
                    asp_frame = asp_header + packet + b"\x00\x00"
                    self.write_data(asp_frame)
                    seq = (seq + 1) & 0xFFFF
            except BlockingIOError:
                pass
            
            # 2. Read from SPI Status (FPGA -> Linux)
            ver, status, rx_len = self.read_status()
            
            if rx_len > 0:
                data = self.read_data(rx_len)
                # Validate ASP Frame: expect Sync == 0xA5
                if len(data) >= 10 and data[0] == 0xA5:
                    # Extract payload (skip 8 byte header, ignore 2 byte trailing CRC)
                    payload_len = (data[6] << 8) | data[7]
                    payload = data[8:8+payload_len]
                    
                    # Dump into Linux networking stack
                    os.write(self.tun_fd, payload)
            
            # Prevent 100% CPU lock in test script
            time.sleep(0.005)

if __name__ == "__main__":
    driver = AbstractXTUNDriver()
    try:
        driver.run()
    except KeyboardInterrupt:
        print("\nExiting TUN driver.")
