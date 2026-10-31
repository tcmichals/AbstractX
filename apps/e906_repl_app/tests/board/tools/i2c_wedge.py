"""@verifies [SPEC-E906-HAL-04] targets/allwinner_e906/SPECIFICATION.md (I2C bus recovery)

Leave the I2C1 bus stuck the way a cut transfer does: start a read from the compass and stop
clocking mid-byte while it drives SDA low. Runs on the A5E with the E906 stopped. Prints the result."""
import mmap, os, time
fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
m = memoryview(mmap.mmap(fd, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE, offset=0x02000000)).cast("I")
CFG, DAT = 0x30 // 4, 0x40 // 4
cfg0 = m[CFG]; base = cfg0 & ~(0xFF << 16)
def pin(p, low):  # open drain: low = output 0, else input
    sh = 16 + 4 * (p - 4)
    if low:
        m[DAT] = m[DAT] & ~(1 << p); m[CFG] = (m[CFG] & ~(0xF << sh)) | (1 << sh)
    else:
        m[CFG] = m[CFG] & ~(0xF << sh)
    time.sleep(0.00002)
sda = lambda: (m[DAT] >> 5) & 1
m[CFG] = base
for addr in (0x0D, 0x1E):
    pin(5, True); pin(4, True)                     # START
    for i in range(8):                             # address + read bit
        bit = ((addr << 1) | 1) >> (7 - i) & 1
        pin(5, not bit); pin(4, False); pin(4, True)
    pin(5, False); pin(4, False); acked = not sda(); pin(4, True)   # ACK clock
    if not acked:
        pin(5, True); pin(4, False); pin(5, False); continue          # STOP, try next address
    for _ in range(16):                             # clock data bits until the device drives 0
        pin(4, False)
        if not sda():
            break
        pin(4, True)
    stuck = (not sda())
    m[CFG] = (m[CFG] & ~(0xFF << 16)) | (0x22 << 16)                 # pins back to TWI, bus left stuck
    print(f"wedged at 0x{addr:02x}: SDA={'LOW (stuck)' if stuck else 'high'}")
    break
else:
    m[CFG] = cfg0
    print("no device acked; not wedged")
