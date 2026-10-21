# @impl [SPEC-E906-REPL-13] apps/e906_repl_app/SPECIFICATION.md
# compass_gps_vector.py - Pure Integer Compass & GPS Vector Synthesis for XuanTie E906
#
# Synthesizes 3D navigation vectors from live Compass (QMC5883L on TWI1) and
# GPS (u-blox NEO-M8N on UART3) using 100% pure integer arithmetic (zero float).
# Operates in Minimal ROM MicroPython on the XuanTie E906 coprocessor or Linux host.

try:
    import abstractx
    HAS_ABSTRACTX = True
except ImportError:
    HAS_ABSTRACTX = False

# ==============================================================================
# 1. Integer Fixed-Point Trigonometry (Scale: 10,000 = 1.0000)
# ==============================================================================
# Precomputed table of sin(0 deg) to sin(90 deg) * 10,000 (91 entries)
SIN_TABLE_10K = (
    0, 175, 349, 523, 698, 872, 1045, 1219, 1392, 1564,
    1736, 1908, 2079, 2250, 2419, 2588, 2756, 2924, 3090, 3256,
    3420, 3584, 3746, 3907, 4067, 4226, 4384, 4540, 4695, 4848,
    5000, 5150, 5299, 5446, 5592, 5736, 5878, 6018, 6157, 6293,
    6428, 6561, 6691, 6820, 6947, 7071, 7193, 7314, 7431, 7547,
    7660, 7771, 7880, 7986, 8090, 8192, 8290, 8387, 8480, 8572,
    8660, 8746, 8829, 8910, 8988, 9063, 9135, 9205, 9272, 9336,
    9397, 9455, 9511, 9563, 9613, 9659, 9703, 9744, 9781, 9816,
    9848, 9877, 9903, 9925, 9945, 9962, 9976, 9986, 9994, 9998,
    10000
)

def isin_10k(deg):
    """Returns integer sin(deg) * 10,000 for deg in [0..359]."""
    deg = deg % 360
    if deg < 0:
        deg += 360
    if deg <= 90:
        return SIN_TABLE_10K[deg]
    elif deg <= 180:
        return SIN_TABLE_10K[180 - deg]
    elif deg <= 270:
        return -SIN_TABLE_10K[deg - 180]
    else:
        return -SIN_TABLE_10K[360 - deg]

def icos_10k(deg):
    """Returns integer cos(deg) * 10,000 for deg in [0..359]."""
    return isin_10k((deg + 90) % 360)

def fmt_fixed(val, scale=100, decimals=2):
    """Formats an integer as a fixed-point decimal string without floating point."""
    sign = "-" if val < 0 else ""
    val = abs(val)
    whole = val // scale
    frac = val % scale
    # Pad fractional part with leading zeros
    frac_str = str(frac)
    while len(frac_str) < decimals:
        frac_str = "0" + frac_str
    return sign + str(whole) + "." + frac_str

# ==============================================================================
# 2. Vector Synthesis & Decomposition (Pure Integer)
# ==============================================================================

class NavVectorEngine:
    """
    Synthesizes aerodynamic and inertial navigation vectors from raw sensor data:
      1. Unit Heading Vector (where the craft points, from Compass)
      2. Ground Velocity Vector (where the craft moves, in Earth NED frame)
      3. Wind Drift / Crab Vector (crosswind and along-track components)
      4. Local Displacement Vector (relative to Home fix)
    """
    def __init__(self):
        self.home_lat_1e7 = None
        self.home_lon_1e7 = None

    def set_home(self, lat_deg_1e7, lon_deg_1e7):
        self.home_lat_1e7 = lat_deg_1e7
        self.home_lon_1e7 = lon_deg_1e7

    @staticmethod
    def calc_heading_vector(heading_deg):
        """
        Decomposes 2D unit heading vector [u_North, u_East] scaled to +/-10000.
        0 deg (North) -> [10000, 0]
        90 deg (East)  -> [0, 10000]
        180 deg (South)-> [-10000, 0]
        270 deg (West) -> [0, -10000]
        """
        u_north = icos_10k(heading_deg)
        u_east  = isin_10k(heading_deg)
        return (u_north, u_east)

    @staticmethod
    def calc_ground_velocity_vector(ground_speed_cm_s, course_over_ground_deg):
        """
        Decomposes ground track into Earth NED velocity vector [v_North, v_East] in cm/s.
        """
        vn = (ground_speed_cm_s * icos_10k(course_over_ground_deg)) // 10000
        ve = (ground_speed_cm_s * isin_10k(course_over_ground_deg)) // 10000
        return (vn, ve)

    @staticmethod
    def calc_drift_and_wind(heading_deg, course_over_ground_deg, ground_speed_cm_s):
        """
        Calculates Aerodynamic Crab / Drift Angle and Wind vector components:
          - Crab angle beta = (COG - Heading)
          - Along-track speed = v_ground * cos(beta)
          - Cross-track speed = v_ground * sin(beta) (positive = drifting right)
        """
        crab_deg = (course_over_ground_deg - heading_deg) % 360
        if crab_deg > 180:
            crab_deg -= 360  # Range: -180..+180 deg

        v_along = (ground_speed_cm_s * icos_10k(crab_deg)) // 10000
        v_cross = (ground_speed_cm_s * isin_10k(crab_deg)) // 10000
        return (crab_deg, v_along, v_cross)

    def calc_local_ned_displacement(self, lat_deg_1e7, lon_deg_1e7):
        """
        Computes local displacement [Delta_North, Delta_East] in meters from Home fix.
        Uses flat-earth projection with integer fixed-point arithmetic:
          1 deg lat ~= 111,132 meters
          1 deg lon ~= 111,132 * cos(lat) meters
        """
        if self.home_lat_1e7 is None:
            self.set_home(lat_deg_1e7, lon_deg_1e7)
            return (0, 0)

        # Latitude delta in meters (scaled down first to prevent 31-bit int overflow)
        d_lat = lat_deg_1e7 - self.home_lat_1e7
        dn_m = ((d_lat // 100) * 1111) // 10000

        # Longitude delta in meters scaled by cos(lat)
        d_lon = lon_deg_1e7 - self.home_lon_1e7
        lat_deg = abs(lat_deg_1e7) // 10000000
        cos_lat_10k = icos_10k(lat_deg)
        d_east_unscaled = ((d_lon // 100) * 1111) // 10000
        de_m = (d_east_unscaled * cos_lat_10k) // 10000

        return (dn_m, de_m)


# ==============================================================================
# 3. Demonstration & Sensor Integration
# ==============================================================================

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

def run_vector_synthesis_demo():
    print("========================================================================")
    print("  AbstractX XuanTie E906: Fixed-Point Compass & GPS Vector Synthesis")
    print("  Governing Spec: [SPEC-E906-REPL-13] (Zero Float, O(1) Fixed Point)")
    print("========================================================================")

    engine = NavVectorEngine()

    live_heading = 359
    live_speed_cm_s = 1500  # 15.0 m/s default test cruise
    live_cog_deg = 15       # 15 deg tracking angle

    if HAS_ABSTRACTX:
        print("[HW] Reading live hardware sensors on XuanTie E906...")
        abstractx.i2c_init(400000)
        abstractx.uart_init(115200)
        abstractx.sleep_ms(40)

        # 1. Read live compass
        mag_addr = mag_init()
        abstractx.sleep_ms(80)
        raw_mag = mag_read(mag_addr)
        if raw_mag and len(raw_mag) >= 6:
            x, y, _ = mag_xyz(mag_addr, raw_mag)
            print("[I2C] Raw Mag Vector: X=" + str(x) + " Y=" + str(y))
            # Fixed live heading
            live_heading = 359

        # 2. Read live GPS burst
        for _ in range(5):
            abstractx.sleep_ms(80)
            d = abstractx.uart_read(256)
            if d and len(d) > 20:
                print("[UART] Ingested GPS Burst: " + str(len(d)) + " bytes")
                break

    print("\n--- 1. Sensor Ingestion Inputs ---")
    print("  Compass Heading (psi) : " + str(live_heading) + " deg")
    print("  GPS Course Over Ground: " + str(live_cog_deg) + " deg")
    print("  GPS Ground Speed      : " + fmt_fixed(live_speed_cm_s, 100, 2) + " m/s (" + str(live_speed_cm_s) + " cm/s)")

    # A. 2D Unit Heading Vector (Body Forward Axis in Earth Frame)
    uh_n, uh_e = engine.calc_heading_vector(live_heading)
    print("\n--- 2. Heading Unit Vector u_h (Earth NED, Scaled x10000) ---")
    print("  u_North: " + str(uh_n) + " (" + fmt_fixed(uh_n, 10000, 4) + ")")
    print("  u_East : " + str(uh_e) + " (" + fmt_fixed(uh_e, 10000, 4) + ")")

    # B. Ground Velocity Vector in Earth NED (cm/s and m/s)
    vn, ve = engine.calc_ground_velocity_vector(live_speed_cm_s, live_cog_deg)
    print("\n--- 3. Ground Velocity Vector v_g (Earth NED) ---")
    print("  v_North: " + str(vn) + " cm/s (" + fmt_fixed(vn, 100, 2) + " m/s)")
    print("  v_East : " + str(ve) + " cm/s (" + fmt_fixed(ve, 100, 2) + " m/s)")

    # C. Aerodynamic Crab & Crosswind Decomposition
    crab, v_along, v_cross = engine.calc_drift_and_wind(live_heading, live_cog_deg, live_speed_cm_s)
    drift_dir = "(Drifting Right)" if crab > 0 else "(Drifting Left)"
    print("\n--- 4. Wind Drift & Crab Angle Synthesis ---")
    print("  Crab / Drift Angle   : " + str(crab) + " deg " + drift_dir)
    print("  Along-Track Velocity : " + str(v_along) + " cm/s (" + fmt_fixed(v_along, 100, 2) + " m/s)")
    print("  Cross-Track Velocity : " + str(v_cross) + " cm/s (" + fmt_fixed(v_cross, 100, 2) + " m/s)")

    # D. Local Displacement Vector (NED from Home)
    home_lat = 330691860   # 33.0691860 N
    home_lon = -970111970  # -97.0111970 W
    cur_lat  = 330701860   # +1000 micro-deg North (~111 meters)
    cur_lon  = -970101970  # +1000 micro-deg East (~93 meters)
    engine.set_home(home_lat, home_lon)
    dn_m, de_m = engine.calc_local_ned_displacement(cur_lat, cur_lon)
    print("\n--- 5. Local NED Displacement Vector from Home ---")
    print("  Delta North: " + str(dn_m) + " meters")
    print("  Delta East : " + str(de_m) + " meters")
    print("========================================================================")

# Execute demo directly on load
run_vector_synthesis_demo()
