# AbstractX - Project Status & Roadmap (TODO)

Last Updated: September 16, 2026

---

## 1. Verified & Completed Milestones

### 1.1 Architecture & Queues
- [x] **Wait-Free MPSC Queue (`include/mpsc_isr_queue.hpp`)**:
  - Implemented bounded wait-free multi-producer single-consumer queue for single-core targets (XuanTie E907) and intra-core ISR dispatch.
  - Uses 2-cycle hardware IRQ masking (`csrrci mstatus, 8` on RV32, `cpsid i` on Cortex-M, thread-safe on SITL).
  - Fully compatible with `DomainQueuePolicy` (`push_from_isr()`, `pop_from_isr()`, `empty_from_isr()`).
- [x] **Eliminated GPS UART Debug Corruption**:
  - Completely decoupled debug logging from the dedicated GPS serial port (`hal::IUart`).
  - Diagnostic messages now route strictly to system `stdout` / `printf` (USB-CDC, debug UART, or RemoteProc `trace0`), keeping the binary UBX GPS line clean.

### 1.2 Universal Barectf CTF 1.8 Telemetry Egress
- [x] **Platform Telemetry Interface (`platform_send_telemetry`)**:
  - Added unified `hal::platform_send_telemetry(const uint8_t* data, size_t len)` across all targets.
  - Connected `trace::g_tracer.set_flush_handler()` and `g_telemetry_ring` drain in `apps/gps_imu_app/src/main.cpp`.
- [x] **Target Implementations**:
  - **Host / Linux**: POSIX UDP socket broadcasting to `127.0.0.1:9870` for `abstractx_studio.py`.
  - **Pico 2 W**: lwIP UDP packet transmission over CYW43439 Wi-Fi to port `9870`.
  - **XuanTie E907**: Direct zero-copy write to Dedicated MCU SRAM C (`0x07131000`) and SRAM Space 0 (`0x3FFC8100`) with hardware Mailbox doorbell notification.

### 1.3 Build Status Across All Targets
- [x] **Host SITL**: Builds cleanly via `cmake -B build-host -S . -DABSTRACTX_TARGET=host`. Runs `gps_imu_app` with live heartbeat; 100% CTest suite passed (22/22 tests).
- [x] **Allwinner Cubie A5E (XuanTie E907)**: Builds cleanly via `cmake -B build-e907 -S . -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-e907.cmake -DABSTRACTX_TARGET=e907`. Generates `gps_imu_app` (ELF) and `gps_imu_app.bin` (240 KB).
- [x] **Raspberry Pi Pico 2 W (RP2350)**: Builds cleanly via `cmake -B build-pico2w -S . -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-pico2w.cmake -DABSTRACTX_TARGET=pico2w -DPICO_PLATFORM=rp2350-arm-s -DPICO_BOARD=pico2_w`. Generates `gps_imu_app.uf2` (761 KB), `.elf`, and `.bin`.

---

## 2. Immediate Action Items (Next Steps)

### 2.1 Hardware Deployment & Field Testing
- [ ] **Load & Run on Radxa Cubie A5E (E907)**:
  - Copy `build-e907/apps/gps_imu_app/gps_imu_app` to `/lib/firmware/gps_imu_app.elf` on the board.
  - Verify active Device Tree overlay in `/boot/config.txt`:
    ```bash
    dtoverlay=cubie-a5e-flight-stack cubie-a5e-uio
    ```
  - Start via Linux RemoteProc:
    ```bash
    echo "gps_imu_app.elf" > /sys/class/remoteproc/remoteproc0/firmware
    echo start > /sys/class/remoteproc/remoteproc0/state
    ```
  - Verify status:
    ```bash
    cat /sys/class/remoteproc/remoteproc0/state
    cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
    ```

- [ ] **Load & Run on Raspberry Pi Pico 2 W (RP2350)**:
  - Configure target network credentials:
    ```bash
    cmake -B build-pico2w -S . -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-pico2w.cmake -DABSTRACTX_TARGET=pico2w -DPICO_PLATFORM=rp2350-arm-s -DPICO_BOARD=pico2_w -DWIFI_SSID="YourNetwork" -DWIFI_PASSWORD="YourPassword"
    cmake --build build-pico2w --target gps_imu_app -j$(nproc)
    ```
  - Flash `build-pico2w/apps/gps_imu_app/gps_imu_app.uf2` via USB BOOTSEL mode or `picotool`.
  - Connect serial console at 115200 baud to observe DHCP IP assignment.

- [ ] **Live Telemetry Visualization with AbstractX Studio**:
  - Start visualizer on host PC:
    ```bash
    python3 tools/visualizer/abstractx_studio.py
    ```
  - Verify live incoming 64B TLPs and CTF 1.8 packets over UDP port 9870.

### 2.2 Host Linux Bridge for Cubie A5E
- [ ] **SRAM-to-UDP Bridge Daemon**:
  - Adapt `/home/tcmichals/ssdData/projects/home/CubieA5E/cubie-a5e/firmware/e907-riscv/apps/testStringBinaryTrace0/fast_sram_telemetry.py` to read popped SRAM telemetry packets and forward them over UDP (`sock.sendto(data, ("127.0.0.1", 9870))`).
  - Wire to `/dev/uio0` for zero-overhead doorbell wakeups.

### 2.3 Priority Dispatch Tuning
- [ ] **E907 Priority Lane Execution**:
  - Evaluate multi-lane priority queue (`RealTime` 8 kHz IMU > `Flight` EKF > `Telemetry` low-priority) in `E907IoProcessor::step()` to ensure zero jitter on the high-rate IMU pipeline.
