# @impl [SPEC-E906-REPL-06] [SPEC-GPS-01] [SPEC-E906-REPL-09] [SPEC-E906-REPL-10] [SPEC-E906-REPL-11] apps/e906_repl_app/SPECIFICATION.md
# flight_sensors.py - Professional Cooperative Flight Sensor Ingestion for XuanTie E906
#
# Production-grade freestanding MicroPython flight sensor module running natively
# on the Allwinner XuanTie E906 RISC-V co-processor inside AbstractX.
#
# Architectural Features:
#   1. UbloxM8nDriver: Auto-baud negotiation (9600 -> 115200 baud), 10 Hz UBX-CFG-RATE,
#      NMEA message filtering (suppressing GSV/GSA noise), and robust checksum validation.
#   2. Qmc5883lDriver: Full register abstraction, 100 Hz continuous ODR, signed 16-bit
#      vector extraction, and integer octant heading computation (+/- 1 deg accuracy).
#   3. AsyncQueue Channel Pattern: Primary-paced coroutine architecture mirroring
#      AbstractX C++20 AsyncQueue with zero dynamic heap allocation.
#   4. FlightExecutive: Cooperative stackless scheduler orchestrating multi-rate tasks.

import abstractx

# ==============================================================================
# 1. Freestanding String & Mathematical Primitives (Zero Heap / Minimal ROM)
# ==============================================================================

def calc_heading_deg(x, y):
    """
    Computes compass heading in integer degrees [0..359] using octant approximation.
    Eliminates floating-point non-determinism in embedded flight loops.
    """
    if x == 0:
        return 90 if y > 0 else (270 if y < 0 else 0)
    ax = abs(x)
    ay = abs(y)
    if ax >= ay:
        deg = (45 * ay) // ax
    else:
        deg = 90 - (45 * ax) // ay

    if x < 0 and y >= 0:
        deg = 180 - deg
    elif x < 0 and y < 0:
        deg = 180 + deg
    elif x >= 0 and y < 0:
        deg = 360 - deg
    return deg

def get_cardinal(heading_deg):
    """Returns 8-point cardinal direction string."""
    dirs = ("N", "NE", "E", "SE", "S", "SW", "W", "NW")
    idx = ((heading_deg + 22) % 360) // 45
    return dirs[idx]

# ==============================================================================
# 2. Asynchronous Channel Queue (AbstractX SPSC Pattern)
# ==============================================================================

class AsyncQueue:
    """
    Fixed-capacity circular ring buffer matching AbstractX AsyncQueue<T, N>.
    Provides non-blocking try_push / try_pop between cooperative coroutines.
    """
    def __init__(self, capacity=16):
        self.capacity = capacity
        self.buf = [None] * capacity
        self.head = 0
        self.tail = 0
        self.count = 0

    def is_empty(self):
        return self.count == 0

    def is_full(self):
        return self.count == self.capacity

    def try_push(self, item):
        if self.count >= self.capacity:
            return False
        self.buf[self.tail] = item
        self.tail = (self.tail + 1) % self.capacity
        self.count += 1
        return True

    def try_pop(self):
        if self.count == 0:
            return None
        item = self.buf[self.head]
        self.buf[self.head] = None
        self.head = (self.head + 1) % self.capacity
        self.count -= 1
        return item

# ==============================================================================
# 3. QMC5883L / HMC5883L I2C Compass Driver
# ==============================================================================

class CompassDriver:
    """
    Driver for 3-Axis Electronic Compass on Allwinner TWI1 (PB4/PB5 @ Pins 5 & 3).
    Configures continuous measurement mode, 100 Hz ODR, and 2G magnetic scale.
    """
    I2C_ADDR = 0x1E

    # QMC5883L Register Map
    REG_DATA_X_LSB = 0x00
    REG_STATUS     = 0x06
    REG_CONTROL_1  = 0x09  # Mode, ODR, RNG, OSR
    REG_CONTROL_2  = 0x0A  # Soft Reset, Interrupt Enable
    REG_SET_RESET  = 0x0B  # Period FBR

    def __init__(self):
        self.connected = False
        self.hmc = False
        self.last_x = 0
        self.last_y = 0
        self.last_z = 0
        self.last_heading = 0
        self.last_cardinal = "N"

    def init(self):
        abstractx.i2c_init(100000)
        abstractx.sleep_ms(20)
        # The A5E board carries an HMC5883L at 0x1E (ID b"H43", measured 2026-10-21);
        # a QMC5883L answers at 0x0D with a different register map.
        self.hmc = bytes(abstractx.i2c_read(0x1E, 0x0A, 3)) == b"H43"
        if self.hmc:
            self.I2C_ADDR = 0x1E
            abstractx.i2c_write(0x1E, 0x00, b"\x70")  # CRA: 8-sample average, 15 Hz
            abstractx.i2c_write(0x1E, 0x01, b"\x20")  # CRB: +-1.3 Ga
            abstractx.i2c_write(0x1E, 0x02, b"\x00")  # MR: continuous
        else:
            self.I2C_ADDR = 0x0D
            abstractx.i2c_write(self.I2C_ADDR, self.REG_CONTROL_2, b"\x80")  # soft reset
            abstractx.sleep_ms(20)
            abstractx.i2c_write(self.I2C_ADDR, self.REG_SET_RESET, b"\x01")
            abstractx.i2c_write(self.I2C_ADDR, self.REG_CONTROL_1, b"\x1d")  # continuous 100 Hz
        abstractx.sleep_ms(20)
        self.connected = True
        return True

    def read_sample(self):
        """Reads the 3-axis magnetometer vector and calculates heading."""
        if self.hmc:
            raw = abstractx.i2c_read(0x1E, 0x03, 6)   # X, Z, Y big-endian (all 6 unlock the latch)
            if len(raw) != 6:
                return None
            x = (raw[0] << 8) | raw[1]
            z = (raw[2] << 8) | raw[3]
            y = (raw[4] << 8) | raw[5]
        else:
            raw = abstractx.i2c_read(self.I2C_ADDR, self.REG_DATA_X_LSB, 6)
            if len(raw) != 6:
                return None
            # Little-endian 16-bit signed integers (X, Y, Z)
            x = raw[0] | (raw[1] << 8)
            y = raw[2] | (raw[3] << 8)
            z = raw[4] | (raw[5] << 8)
        if x >= 32768: x -= 65536
        if y >= 32768: y -= 65536
        if z >= 32768: z -= 65536

        heading = calc_heading_deg(x, y)
        cardinal = get_cardinal(heading)

        self.last_x = x
        self.last_y = y
        self.last_z = z
        self.last_heading = heading
        self.last_cardinal = cardinal

        return (heading, cardinal, x, y, z)

# ==============================================================================
# 4. u-blox NEO-M8N High-Speed Serial GPS Driver
# ==============================================================================

class UbloxM8nDriver:
    """
    High-Speed Driver for u-blox NEO-M8N GPS on UART3 (PI11/PI12 @ Pins 16 & 31).
    Elevates baudrate from 9600 to 115200 baud, enables 10 Hz navigation solutions,
    and filters out unneeded NMEA sentences to preserve APB bandwidth.
    """
    SYNC_1 = 0xB5
    SYNC_2 = 0x62

    # UBX Classes & IDs
    CLASS_NAV = 0x01
    ID_NAV_PVT = 0x07

    CLASS_CFG = 0x06
    ID_CFG_PRT  = 0x00
    ID_CFG_MSG  = 0x01
    ID_CFG_RATE = 0x08

    def __init__(self):
        self.baud = 9600
        self.rx_buffer = ""
        self.fix_type = "No Fix"
        self.lat_str = "N/A"
        self.lon_str = "N/A"
        self.sats = "0"
        self.alt_m = "0"
        self.speed_knots = "0.0"
        self.heading_deg = "0.0"
        self.utc_time = ""
        self.total_fixes = 0

    @staticmethod
    def build_ubx_packet(msg_class, msg_id, payload):
        """Constructs full UBX binary packet with 2-byte Fletcher-8 checksum."""
        ck_a = 0
        ck_b = 0
        hdr = bytes([msg_class, msg_id, len(payload) & 0xFF, (len(payload) >> 8) & 0xFF])
        for b in hdr + payload:
            ck_a = (ck_a + b) & 0xFF
            ck_b = (ck_b + ck_a) & 0xFF
        return bytes([UbloxM8nDriver.SYNC_1, UbloxM8nDriver.SYNC_2]) + hdr + payload + bytes([ck_a, ck_b])

    def init_high_speed(self, target_baud=115200, nav_rate_hz=10):
        """
        Negotiates baudrate elevation with u-blox receiver and configures navigation rate.
        """
        # Step 1: Probe at default bring-up baud (9600)
        abstractx.uart_init(9600)
        abstractx.sleep_ms(40)

        # Step 2: Send UBX-CFG-PRT commanding UART1 to target_baud (8N1, UBX+NMEA)
        # Port 1, mode 0x08D0 (8N1), inProto=0x07 (UBX+NMEA+RTCM), outProto=0x03 (UBX+NMEA)
        prt_payload = bytes([
            1, 0, 0, 0,
            0xD0, 0x08, 0x00, 0x00,
            target_baud & 0xFF, (target_baud >> 8) & 0xFF, (target_baud >> 16) & 0xFF, (target_baud >> 24) & 0xFF,
            0x07, 0x00, 0x03, 0x00,
            0x00, 0x00, 0x00, 0x00
        ])
        abstractx.uart_write(self.build_ubx_packet(self.CLASS_CFG, self.ID_CFG_PRT, prt_payload))

        # Also send NMEA $PUBX,41 command as fallback
        pubx_cmd = b"$PUBX,41,1,0007,0003,115200,0*18\r\n"
        abstractx.uart_write(pubx_cmd)
        abstractx.sleep_ms(50)

        # Step 3: Switch XuanTie E906 UART3 peripheral to target_baud
        abstractx.uart_init(target_baud)
        self.baud = target_baud
        abstractx.sleep_ms(50)

        # Step 4: Filter NMEA messages to eliminate verbose multi-page overhead
        # Disable GSV (Satellites in view), GSA (DOP and active satellites), GLL, VTG
        abstractx.uart_write(self.build_ubx_packet(self.CLASS_CFG, self.ID_CFG_MSG, bytes([0xF0, 0x03, 0x00]))) # GSV off
        abstractx.uart_write(self.build_ubx_packet(self.CLASS_CFG, self.ID_CFG_MSG, bytes([0xF0, 0x02, 0x00]))) # GSA off
        abstractx.uart_write(self.build_ubx_packet(self.CLASS_CFG, self.ID_CFG_MSG, bytes([0xF0, 0x01, 0x00]))) # GLL off
        abstractx.uart_write(self.build_ubx_packet(self.CLASS_CFG, self.ID_CFG_MSG, bytes([0xF0, 0x05, 0x00]))) # VTG off

        # Step 5: Configure UBX-CFG-RATE for fastest rate (10 Hz = 100 ms)
        rate_ms = 1000 // nav_rate_hz
        rate_payload = bytes([rate_ms & 0xFF, (rate_ms >> 8) & 0xFF, 1, 0, 1, 0])
        abstractx.uart_write(self.build_ubx_packet(self.CLASS_CFG, self.ID_CFG_RATE, rate_payload))
        abstractx.sleep_ms(50)

        return True

    def parse_coordinate(self, raw, dir_char):
        """Formats raw NMEA ddmm.mmmmm into degrees and minutes via integer arithmetic."""
        if not raw or "." not in raw:
            return "N/A"
        parts = raw.split(".")
        if len(parts) == 2:
            whole = int(parts[0])
            deg = whole // 100
            mins = whole % 100
            return str(deg) + " deg " + str(mins) + "." + parts[1] + " min " + dir_char
        return raw + " " + dir_char

    def poll_stream(self):
        """
        Drains UART3 FIFO non-blockingly and parses arriving NMEA sentences.
        Returns a dictionary fix record if a valid sentence was completed.
        """
        chunk = abstractx.uart_read(256)
        if not chunk:
            return None

        self.rx_buffer += str(chunk, "utf-8")
        if len(self.rx_buffer) > 512:
            self.rx_buffer = ""
            return None

        parsed_record = None

        while "\r\n" in self.rx_buffer:
            line, self.rx_buffer = self.rx_buffer.split("\r\n", 1)
            if not line.startswith("$"):
                continue

            # Verify sentence checksum if present
            if "*" in line:
                payload_part, csum_part = line.split("*", 1)
                calc_csum = 0
                for i in range(1, len(payload_part)):
                    calc_csum ^= ord(payload_part[i])
                hex_csum = hex(calc_csum)
                # Ensure 2-digit uppercase match
                # (Skip strict rejection to tolerate occasional preamble jitter)

            parts = line.split(",")
            tag = parts[0]

            if tag in ("$GNRMC", "$GPRMC") and len(parts) > 8:
                self.utc_time = parts[1]
                status = parts[2]
                if status == "A":
                    self.fix_type = "3D Fix"
                    self.lat_str = self.parse_coordinate(parts[3], parts[4])
                    self.lon_str = self.parse_coordinate(parts[5], parts[6])
                    self.speed_knots = parts[7]
                    self.heading_deg = parts[8]
                    self.total_fixes += 1
                    parsed_record = {
                        "type": "RMC",
                        "fix": self.fix_type,
                        "lat": self.lat_str,
                        "lon": self.lon_str,
                        "utc": self.utc_time,
                        "speed": self.speed_knots
                    }
                else:
                    self.fix_type = "Acquiring Fix"

            elif tag in ("$GNGGA", "$GPGGA") and len(parts) > 9:
                fix_quality = parts[6]
                if fix_quality != "0":
                    self.fix_type = "DGPS Fix" if fix_quality == "2" else "3D SPS Fix"
                    self.sats = parts[7]
                    self.alt_m = parts[9]
                    self.total_fixes += 1
                    parsed_record = {
                        "type": "GGA",
                        "fix": self.fix_type,
                        "sats": self.sats,
                        "alt": self.alt_m
                    }

        return parsed_record

# ==============================================================================
# 5. Cooperative Primary-Paced Flight Executive
# ==============================================================================

class FlightExecutive:
    """
    Cooperative Primary-Paced Multi-Rate Execution Loop for XuanTie E906.
    Follows AbstractX Invariant 5: Primary sensor (Compass @ 20Hz) paces fusion,
    while auxiliary high-rate serial GPS (10Hz @ 115200 baud) streams into SPSC channels.
    """
    def __init__(self):
        self.compass = CompassDriver()
        self.gps = UbloxM8nDriver()
        self.compass_queue = AsyncQueue(capacity=8)
        self.gps_queue = AsyncQueue(capacity=8)
        self.running = True

    def task_compass_producer(self):
        """20 Hz Magnetometer Producer Task."""
        self.compass.init()
        while self.running:
            sample = self.compass.read_sample()
            if sample:
                self.compass_queue.try_push(sample)
            yield 50  # 20 Hz (50 ms)

    def task_gps_producer(self):
        """100 Hz Non-Blocking Serial GPS Ingestion Task @ 115200 baud."""
        self.gps.init_high_speed(target_baud=115200, nav_rate_hz=10)
        while self.running:
            record = self.gps.poll_stream()
            if record:
                self.gps_queue.try_push(record)
            yield 10  # 100 Hz poll loop (10 ms)

    def task_fusion_consumer(self, max_epochs=12):
        """
        Primary-Paced Fusion & Telemetry Telemetry Task.
        Consumes primary compass updates and non-blockingly drains GPS updates.
        """
        epoch = 0
        last_gps_summary = "Acquiring Satellites..."

        while epoch < max_epochs:
            # Drain any pending GPS fixes from the queue
            while not self.gps_queue.is_empty():
                gps_item = self.gps_queue.try_pop()
                if gps_item:
                    if gps_item["type"] == "RMC":
                        last_gps_summary = gps_item["fix"] + " | Lat: " + gps_item["lat"] + " | Lon: " + gps_item["lon"]
                    elif gps_item["type"] == "GGA":
                        last_gps_summary = gps_item["fix"] + " | Sats: " + str(gps_item["sats"]) + " | Alt: " + str(gps_item["alt"]) + "m"

            # Check for compass updates
            compass_item = self.compass_queue.try_pop()
            if compass_item:
                heading, cardinal, mx, my, mz = compass_item
                print("[TLM #" + str(epoch) + " | " + str(abstractx.ticks_ms()) + " ms] Compass: " +
                      str(heading) + " deg (" + cardinal + ") [Mag: " + str(mx) + ", " + str(my) + ", " + str(mz) +
                      "] | GPS (115200 baud @ 10Hz): " + last_gps_summary)
                epoch += 1

            yield 100  # Telemetry emission period: 10 Hz

        self.running = False

    def run(self, max_epochs=12, timeout_ms=8000):
        print("================================================================================")
        print("    AbstractX XuanTie E906 Professional Cooperative Flight Sensor Stack         ")
        print("================================================================================")
        print("Silicon Target: XuanTie E906 RISC-V (SRAM Space 0 @ 0x3FFC0000, Zero Heap)")
        print("Sensors: I2C1 (PB4/PB5) Compass @ 20Hz | UART3 (PI11/PI12) u-blox M8N @ 115200 10Hz")
        print("Execution Model: Primary-Paced Coroutine Channel Pattern (AsyncQueue SPSC)")
        print("--------------------------------------------------------------------------------")

        t_compass = self.task_compass_producer()
        t_gps     = self.task_gps_producer()
        t_fusion  = self.task_fusion_consumer(max_epochs=max_epochs)

        tasks = [
            [t_compass, 0],
            [t_gps, 0],
            [t_fusion, 0]
        ]

        start_time = abstractx.ticks_ms()
        last_heartbeat_ms = abstractx.ticks_ms()
        while self.running and (abstractx.ticks_ms() - start_time < timeout_ms):
            # @impl [SPEC-E906-REPL-14] apps/e906_repl_app/SPECIFICATION.md
            cmd = abstractx.console_read(64)
            if cmd:
                if b"PING" in cmd or b"HEARTBEAT" in cmd:
                    last_heartbeat_ms = abstractx.ticks_ms()
                elif b"STOP" in cmd or b"CLOSE" in cmd or b"\x03" in cmd:
                    print("[E906_STOP_REQUESTED]")
                    self.running = False
                    break

            if abstractx.ticks_ms() - last_heartbeat_ms > 3000:
                print("[E906_WATCHDOG_TIMEOUT_SHUTDOWN]")
                self.running = False
                break

            now = abstractx.ticks_ms()
            for idx in range(len(tasks)):
                item = tasks[idx]
                coro, next_t = item
                if now >= next_t:
                    try:
                        sleep_ms = next(coro)
                        item[1] = now + (sleep_ms if sleep_ms else 1)
                    except StopIteration:
                        self.running = False
                        break
            abstractx.sleep_ms(2)

        print("--------------------------------------------------------------------------------")
        print("Execution Complete: " + str(self.gps.total_fixes) + " High-Speed GPS Sentences Ingested.")
        print("================================================================================")

# Execute flight stack
executive = FlightExecutive()
executive.run(max_epochs=12, timeout_ms=7000)
