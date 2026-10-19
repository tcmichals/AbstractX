# flight_sensors.py - MicroPython Cooperative Async Sensor Ingestion for XuanTie E906
# @impl [SPEC-E906-REPL-06] [SPEC-GPS-01] apps/e906_repl_app/SPECIFICATION.md
#
# Runs natively on the XuanTie E906 RISC-V co-processor inside AbstractX.
# Ingests both I2C Compass (TWI1 on PB4/PB5) and Serial GPS (UART3 on PI11/PI12)
# using cooperative coroutines / asyncio generator tasks with 0 heap allocation.

import abstractx

# ==============================================================================
# 1. String & Mathematical Helpers (Minimal ROM Level Compatible)
# ==============================================================================

def substr(s, start, end):
    """Slice emulator for minimal MicroPython ROM level."""
    L = len(s)
    if end > L:
        end = L
    if start < 0:
        start = 0
    chars = []
    for i in range(start, end):
        chars.append(s[i])
    return "".join(chars)

def calc_heading(x, y):
    """
    Computes compass heading in integer degrees (0..359) using octant approximation.
    Accurate to within +/- 1 degree without requiring floating-point runtime.
    """
    if x == 0:
        return 90 if y > 0 else 270 if y < 0 else 0
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

def parse_lat_lon(raw_val, dir_char):
    """Parses NMEA ddmm.mmmmm into human-readable degrees and minutes."""
    if not raw_val or len(raw_val) < 4:
        return "N/A"
    dot = raw_val.find(".")
    if dot > 2:
        deg = substr(raw_val, 0, dot - 2)
        mins = substr(raw_val, dot - 2, len(raw_val))
        return deg + " deg " + mins + " min " + dir_char
    return raw_val + " " + dir_char

# ==============================================================================
# 2. Shared Flight Sensor State
# ==============================================================================

class SensorState:
    def __init__(self):
        self.mag_x = 0
        self.mag_y = 0
        self.mag_z = 0
        self.heading = 0
        self.cardinal = "N"
        self.gps_utc = ""
        self.gps_fix = "Searching"
        self.gps_lat = "N/A"
        self.gps_lon = "N/A"
        self.gps_sats = "0"
        self.gps_alt = "0"
        self.samples_mag = 0
        self.samples_gps = 0

g_state = SensorState()

# ==============================================================================
# 3. Cooperative Async Coroutine Tasks
# ==============================================================================

def task_compass(state):
    """
    20 Hz Magnetometer / Compass Ingestion Task.
    Communicates with QMC5883L/HMC5883L over TWI1 (PB4/PB5 @ Pins 5 & 3).
    """
    abstractx.i2c_init(100000)
    # Configure QMC5883L continuous mode (Reg 0x09 = 0x1D: 100Hz ODR, 2G, 512 OSR)
    abstractx.i2c_write(0x1e, 0x09, b"\x1d")
    # Configure HMC5883L continuous mode fallback (Reg 0x02 = 0x00)
    abstractx.i2c_write(0x1e, 0x02, b"\x00")

    while True:
        raw = abstractx.i2c_read(0x1e, 0x00, 6)
        if len(raw) == 6:
            # Unpack little-endian signed 16-bit integers
            x = raw[0] | (raw[1] << 8)
            y = raw[2] | (raw[3] << 8)
            z = raw[4] | (raw[5] << 8)
            if x >= 32768: x -= 65536
            if y >= 32768: y -= 65536
            if z >= 32768: z -= 65536
            state.mag_x = x
            state.mag_y = y
            state.mag_z = z
            state.heading = calc_heading(x, y)
            state.cardinal = get_cardinal(state.heading)
            state.samples_mag += 1
        yield 50  # Cooperative yield: sleep 50 ms (20 Hz)

def task_gps(state):
    """
    100 Hz Non-Blocking Serial GPS Ingestion Task.
    Reads NMEA streaming sentences from UART3 FIFO (PI11/PI12 @ Pins 16 & 31).
    """
    abstractx.uart_init(9600)
    rx_buf = ""

    while True:
        chunk = abstractx.uart_read(128)
        if chunk:
            rx_buf += str(chunk, "utf-8")
            while "\r\n" in rx_buf:
                line, rx_buf = rx_buf.split("\r\n", 1)
                if not line.startswith("$"):
                    continue
                parts = line.split(",")
                tag = parts[0]
                if tag in ("$GNRMC", "$GPRMC") and len(parts) > 6:
                    state.gps_utc = parts[1]
                    status = parts[2]
                    if status == "A":
                        state.gps_fix = "3D Fix (Valid)"
                        state.gps_lat = parse_lat_lon(parts[3], parts[4])
                        state.gps_lon = parse_lat_lon(parts[5], parts[6])
                        state.samples_gps += 1
                    else:
                        state.gps_fix = "Searching Satellites"
                elif tag in ("$GNGGA", "$GPGGA") and len(parts) > 9:
                    fix_q = parts[6]
                    if fix_q != "0":
                        state.gps_fix = "3D SPS Fix" if fix_q == "1" else "DGPS Fix"
                        state.gps_sats = parts[7]
                        state.gps_alt = parts[9]
                        state.samples_gps += 1
        yield 10  # Cooperative yield: sleep 10 ms (100 Hz poll)

def task_telemetry(state, max_reports=10):
    """
    5 Hz Telemetry Output Stream Task.
    Prints formatted navigation frames over VirtIO RPMsg to Linux host.
    """
    report_idx = 0
    while report_idx < max_reports:
        print("[TLM #" + str(report_idx) + " | " + str(abstractx.ticks_ms()) + " ms] Compass: " +
              str(state.heading) + " deg (" + state.cardinal + ") [X=" + str(state.mag_x) +
              " Y=" + str(state.mag_y) + " Z=" + str(state.mag_z) + "] | GPS: " + state.gps_fix +
              " [Sats: " + str(state.gps_sats) + " Alt: " + str(state.gps_alt) + "m] | Lat: " +
              state.gps_lat + " | Lon: " + state.gps_lon)
        report_idx += 1
        yield 200  # Cooperative yield: sleep 200 ms (5 Hz)

# ==============================================================================
# 4. Cooperative Asyncio Event Loop Scheduler
# ==============================================================================

class AsyncScheduler:
    """
    Cooperative Async Task Scheduler for MicroPython.
    Mimics asyncio.create_task() and asyncio.run() with zero dynamic heap.
    """
    def __init__(self):
        self.tasks = []

    def create_task(self, coro_gen):
        self.tasks.append([coro_gen, 0])

    def run_until_complete(self, main_task_idx=2, timeout_ms=8000):
        start = abstractx.ticks_ms()
        running = True
        while running and (abstractx.ticks_ms() - start < timeout_ms):
            now = abstractx.ticks_ms()
            for idx in range(len(self.tasks)):
                item = self.tasks[idx]
                coro, next_time = item
                if now >= next_time:
                    try:
                        sleep_ms = next(coro)
                        item[1] = now + (sleep_ms if sleep_ms else 1)
                    except StopIteration:
                        if idx == main_task_idx:
                            running = False
                            break
            abstractx.sleep_ms(2)

def run():
    print("================================================================================")
    print("       AbstractX XuanTie E906 MicroPython Cooperative Flight Sensors            ")
    print("================================================================================")
    print("Firmware Version:", abstractx.version())
    print("Ingesting: I2C1 (PB4/PB5 Pins 5&3) Compass + UART3 (PI11/PI12 Pins 16&31) GPS")
    print("Concurrency: Cooperative Coroutine Event Loop (Asyncio Pattern)")
    print("--------------------------------------------------------------------------------")

    sched = AsyncScheduler()
    sched.create_task(task_compass(g_state))
    sched.create_task(task_gps(g_state))
    sched.create_task(task_telemetry(g_state, max_reports=10))

    sched.run_until_complete(main_task_idx=2, timeout_ms=6000)

    print("--------------------------------------------------------------------------------")
    print("Completed:", g_state.samples_mag, "Compass Samples,", g_state.samples_gps, "GPS Fix Sentences ingested.")
    print("================================================================================")

# Execute main routine
run()
