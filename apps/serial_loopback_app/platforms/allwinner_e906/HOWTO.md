# Allwinner XuanTie E906 Serial Loopback & CPU Profiler

## Overview
This application verifies the AbstractX co-processor framework on the
**Radxa Cubie A5E (Allwinner T527 / A523)**:
* **UART2 Loopback**: Receives and echoes bytes across Port B pins (`PB0`/`PB1`).
* **Threshold Balancing**: Bursts `<= 32` bytes use CPU FIFO; bursts `> 32` bytes
  engage the dedicated co-processor DMA controller.
* **CPU Profiler**: Tracks active cycles vs. `wfi` sleep states.
* **Live Trace Output**: Emits CTF 1.8 telemetry to the Linux RemoteProc trace buffer.

---

## Building

```bash
# From AbstractX repository root:
cmake --build --preset e906
```

Target artifacts generated:
* `build_e906/apps/serial_loopback_app/platforms/allwinner_e906/e906_serial_loopback.elf`
* `build_e906/apps/serial_loopback_app/platforms/allwinner_e906/e906_serial_loopback.bin`

---

## Deploying via Linux RemoteProc

```bash
# 1. Copy binary to Cubie A5E target filesystem:
scp build_e906/apps/serial_loopback_app/platforms/allwinner_e906/e906_serial_loopback.elf     root@cubie-a5e:/lib/firmware/

# 2. On the Cubie A5E board, start the firmware:
echo "e906_serial_loopback.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state

# 3. View live heartbeat diagnostics:
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
```

---

## Testing Serial Loopback

Connect a 3.3V USB-to-UART adapter to the Cubie A5E header:
* **TXD**: `PB0` (Header Pin)
* **RXD**: `PB1` (Header Pin)
* **GND**: Ground Pin
* **Baud Rate**: 115200 (8N1)

Send any text string. The co-processor will immediately echo the characters back,
and `trace0` will report updated packet and byte counters in real time!
