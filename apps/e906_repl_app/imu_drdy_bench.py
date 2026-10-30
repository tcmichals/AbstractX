# imu_drdy_bench.py - IMU DRDY auto-DMA consumed from MicroPython ([SPEC-E906-REPL-21]).
# Prints `key value` lines with the same keys as bench 'A' ([SPEC-E906-BENCH-05]).
import abstractx

N = 2000            # samples (bench 'A' default)
TIMEOUT_MS = 500    # first sample, then per sample

buf = bytearray(22)
print("platform micropython")
if not abstractx.imu_drdy_start():
    print("imu_init failed")
    print("end 1")
else:
    print("imu_init ok")
    got = 0
    timeout = 0
    try:
        while got < N:
            if not abstractx.imu_drdy_read_into(buf, TIMEOUT_MS):
                timeout = 1
                break
            got += 1
    finally:
        abstractx.imu_drdy_stop()
    print("imu_timeout", timeout)
    for k, v in abstractx.imu_drdy_stats().items():
        print(k, v)
    print("last_raw", "".join("%02x" % b for b in buf[8:22]) if got else "")
    print("end 1")
