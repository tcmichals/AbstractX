/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: Zynq-7000 UIO Hardware Interface & Target IoProcessor Verification
 * -----------------------------------------------------------------------
 * Exercises the concrete Zynq-7000 Target HAL components on the host against
 * a mock 64 KB memory-mapped AXI-Lite address window.
 *
 * @verifies [SPEC-ZYNQ-01] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-01
 * @verifies [SPEC-ZYNQ-02] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-02
 * @verifies [SPEC-ZYNQ-03] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-03
 * @verifies [SPEC-ZYNQ-06] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-06
 */

#undef NDEBUG  // the asserts below carry the calls under test; keep them in Release builds
#include "abstractx/target/zynq_uio.hpp"
#include "abstractx/hal/io_processor.hpp"
#include "tlp_ring.hpp"
#include "asp_tlp64.hpp"

#include <cstdio>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

using namespace abstractx;
using namespace abstractx::target;
using namespace abstractx::hal;

namespace {

struct MockUioEnvironment {
    char path[64];
    int fd{-1};
    volatile uint32_t* mem{nullptr};

    MockUioEnvironment() {
        std::snprintf(path, sizeof(path), "/tmp/mock_zynq_uio_XXXXXX");
        fd = ::mkstemp(path);
        assert(fd >= 0 && "Failed to create mock UIO backing temp file");

        // Size to 64 KB AXI-Lite window
        int ret = ::ftruncate(fd, ZynqUioBridge::MAP_SIZE);
        assert(ret == 0 && "Failed to truncate mock UIO backing file");

        void* ptr = ::mmap(nullptr, ZynqUioBridge::MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        assert(ptr != MAP_FAILED && "Failed to mmap mock UIO memory");
        mem = reinterpret_cast<volatile uint32_t*>(ptr);
        std::memset(ptr, 0, ZynqUioBridge::MAP_SIZE);
    }

    ~MockUioEnvironment() {
        if (mem != nullptr) {
            ::munmap(const_cast<uint32_t*>(mem), ZynqUioBridge::MAP_SIZE);
            mem = nullptr;
        }
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
        ::unlink(path);
    }

    void set_reg(uint32_t byte_offset, uint32_t val) {
        assert(mem != nullptr);
        mem[byte_offset / sizeof(uint32_t)] = val;
    }

    uint32_t get_reg(uint32_t byte_offset) const {
        assert(mem != nullptr);
        return mem[byte_offset / sizeof(uint32_t)];
    }
};

void test_uio_bridge_hardware_magic_and_enable() {
    std::printf("[TEST 1] ZynqUioBridge: Hardware ID & Initialization...\n");
    MockUioEnvironment env;

    // Without magic number, open_uio must fail safely
    ZynqUioBridge bridge_fail;
    assert(!bridge_fail.open_uio(env.path) && "Expected open_uio to fail on missing magic number");
    assert(!bridge_fail.is_open());

    // Populate valid magic and capacity
    env.set_reg(ZynqUioBridge::REG_HARDWARE_ID, ZynqUioBridge::HARDWARE_MAGIC);
    env.set_reg(ZynqUioBridge::REG_ING_FREE, 128);

    ZynqUioBridge bridge;
    assert(bridge.open_uio(env.path) && "Expected open_uio to succeed with valid magic");
    assert(bridge.is_open() && "Bridge must report is_open() == true");

    // Must automatically enable DMA and unmask IRQ in hardware CSR
    assert(env.get_reg(ZynqUioBridge::REG_CONTROL) == 0x01 && "Bridge must write 0x01 to REG_CONTROL");
    assert(env.get_reg(ZynqUioBridge::REG_IRQ_ENABLE) == 0x01 && "Bridge must write 0x01 to REG_IRQ_ENABLE");
    assert(bridge.ingress_free() == 128 && "Ingress free count must read 128");

    bridge.close_uio();
    assert(!bridge.is_open() && "Bridge must report is_open() == false after close");
    assert(env.get_reg(ZynqUioBridge::REG_CONTROL) == 0x00 && "REG_CONTROL must be cleared on close");
    assert(env.get_reg(ZynqUioBridge::REG_IRQ_ENABLE) == 0x00 && "REG_IRQ_ENABLE must be cleared on close");
    std::printf("  -> Hardware ID verification and initialization passed.\n");
}

void test_uio_bridge_tlp_ingress_and_egress() {
    std::printf("[TEST 2] ZynqUioBridge: 64B TLP Ingress and Egress Data Plane...\n");
    MockUioEnvironment env;
    env.set_reg(ZynqUioBridge::REG_HARDWARE_ID, ZynqUioBridge::HARDWARE_MAGIC);
    env.set_reg(ZynqUioBridge::REG_ING_FREE, 128);
    env.set_reg(ZynqUioBridge::REG_EGR_COUNT, 0);

    ZynqUioBridge bridge;
    assert(bridge.open_uio(env.path));

    // Construct a test 64-byte TLP
    Tlp64 tx_tlp{};
    auto* tx_words = reinterpret_cast<uint32_t*>(&tx_tlp.wire);
    for (size_t i = 0; i < 16; ++i) {
        tx_words[i] = 0xAA000000 + static_cast<uint32_t>(i);
    }

    // Ingress write test
    assert(bridge.write_tlp(tx_tlp) && "write_tlp must succeed when ingress_free > 0");
    // Verify last written DWORD in the TLP port matches tx_words[15]
    assert(env.get_reg(ZynqUioBridge::REG_TLP_IN_PORT) == tx_words[15] &&
           "REG_TLP_IN_PORT must contain written data");

    // Egress read test
    env.set_reg(ZynqUioBridge::REG_EGR_COUNT, 1);
    env.set_reg(ZynqUioBridge::REG_TLP_OUT_PORT, 0xBBCCDDEE);

    Tlp64 rx_tlp{};
    assert(bridge.read_tlp(rx_tlp) && "read_tlp must succeed when egress_count > 0");
    const auto* rx_words = reinterpret_cast<const uint32_t*>(&rx_tlp.wire);
    for (size_t i = 0; i < 16; ++i) {
        assert(rx_words[i] == 0xBBCCDDEE && "read_tlp must consume words from REG_TLP_OUT_PORT");
    }

    // Acknowledge interrupt
    env.set_reg(ZynqUioBridge::REG_EGR_COUNT, 0);
    bridge.ack_interrupt();
    assert(env.get_reg(ZynqUioBridge::REG_IRQ_STATUS) == 0x01 && "ack_interrupt must write W1C to REG_IRQ_STATUS");

    bridge.close_uio();
    std::printf("  -> 64B TLP ingress and egress data plane passed.\n");
}

struct CallbackHelper {
    bool rx_pushed{false};
    void on_pushed() { rx_pushed = true; }
};

void test_zynq_io_processor_reactor_loop() {
    std::printf("[TEST 3] Zynq7000IoProcessor: Reactor Data Plane Pipeline...\n");
    MockUioEnvironment env;
    env.set_reg(ZynqUioBridge::REG_HARDWARE_ID, ZynqUioBridge::HARDWARE_MAGIC);
    env.set_reg(ZynqUioBridge::REG_ING_FREE, 128);
    env.set_reg(ZynqUioBridge::REG_EGR_COUNT, 0);

    ::setenv("ABSTRACTX_UIO_DEV", env.path, 1);

    MpscTlpRing<> egress_tx_ring{};
    MpscTlpRing<> ingress_rx_ring{};

    CallbackHelper helper;
    IoProcessorSetup setup{};
    setup.egress_tx_ring = &egress_tx_ring;
    setup.ingress_rx_ring = &ingress_rx_ring;
    setup.on_rx_pushed = etl::delegate<void()>::create<CallbackHelper, &CallbackHelper::on_pushed>(helper);

    IIoProcessor& io_proc = get_target_io_processor();
    assert(io_proc.configure(setup) && "io_proc configure must succeed");
    assert(io_proc.start() && "io_proc start must succeed with mock UIO device");
    assert(io_proc.is_running() && "io_proc must report is_running() == true");

    // 1. Flush egress command from application to FPGA
    Tlp64 cmd_tlp = Tlp64::make_mem_write(0x40000008, 0xCAFEBABE, 1);
    assert(egress_tx_ring.push(cmd_tlp) && "Push to egress ring must succeed");

    int processed_tx = io_proc.step(0);
    assert(processed_tx == 1 && "io_proc step must process 1 outgoing TLP");
    const auto* cmd_words = reinterpret_cast<const uint32_t*>(&cmd_tlp.wire);
    assert(env.get_reg(ZynqUioBridge::REG_TLP_IN_PORT) == cmd_words[15] &&
           "FPGA ingress port must receive outgoing packet");

    // 2. Drain incoming telemetry packet from FPGA to application
    env.set_reg(ZynqUioBridge::REG_EGR_COUNT, 1);
    env.set_reg(ZynqUioBridge::REG_TLP_OUT_PORT, 0x12345678);

    int processed_rx = io_proc.step(0);
    assert(processed_rx == 1 && "io_proc step must process 1 incoming TLP");
    assert(helper.rx_pushed && "on_rx_pushed callback must be invoked");

    Tlp64 popped_tlp{};
    assert(ingress_rx_ring.pop(popped_tlp) && "Popping from ingress RX ring must succeed");
    const auto* popped_words = reinterpret_cast<const uint32_t*>(&popped_tlp.wire);
    assert(popped_words[0] == 0x12345678 && "Popped TLP data must match FPGA egress data");

    io_proc.stop();
    assert(!io_proc.is_running() && "io_proc must report is_running() == false after stop");

    // 3. Verify concurrent stop() from another thread while run() is active
    assert(io_proc.start() && "io_proc restart must succeed");
    std::thread runner([&]() { io_proc.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    io_proc.stop();
    runner.join();
    assert(!io_proc.is_running() && "io_proc must report is_running() == false after concurrent stop");

    ::unsetenv("ABSTRACTX_UIO_DEV");
    std::printf("  -> Target IoProcessor reactor data plane passed.\n");
}

} // namespace

// [SPEC-ZYNQ-02] re-arm rule: an interrupt taken while egress still holds packets is
// re-armed by the first later call that finds egress empty, never while it is non-empty.
void test_uio_irq_rearm_after_burst() {
    std::printf("[TEST] UIO interrupt re-armed after a burst outlasts one wake...\n");
    MockUioEnvironment env;
    env.set_reg(ZynqUioBridge::REG_HARDWARE_ID, ZynqUioBridge::HARDWARE_MAGIC);
    env.set_reg(ZynqUioBridge::REG_EGR_COUNT, 0);
    ZynqUioBridge bridge;
    assert(bridge.open_uio(env.path));
    assert(bridge.irq_armed() && "armed after open");

    env.set_reg(ZynqUioBridge::REG_EGR_COUNT, 3);   // more packets arrived during the drain
    env.set_reg(ZynqUioBridge::REG_IRQ_STATUS, 0);
    bridge.ack_interrupt();
    assert(!bridge.irq_armed() && "must stay masked while egress is non-empty");
    assert(env.get_reg(ZynqUioBridge::REG_IRQ_STATUS) == 0 && "no W1C while egress is non-empty");
    for (int i = 0; i < 1000; ++i) {
        assert(!bridge.rearm_if_drained() && "no re-arm while egress is non-empty");
    }

    env.set_reg(ZynqUioBridge::REG_EGR_COUNT, 0);   // a later polled step drained it
    assert(bridge.rearm_if_drained() && "first call with egress empty re-arms");
    assert(bridge.irq_armed());
    assert(env.get_reg(ZynqUioBridge::REG_IRQ_STATUS) == 0x01 && "W1C written on re-arm");
    bridge.close_uio();
    std::printf("  -> re-arm after burst passed.\n");
}

// [SPEC-ZYNQ-02] full ingress ring: packets stay in the FPGA egress FIFO, nothing is read
// and discarded, on_rx_pushed runs only for pushed packets, the stall is counted.
void test_full_ingress_ring_backpressure() {
    std::printf("[TEST] Full ingress ring leaves packets in the FPGA (backpressure)...\n");
    MockUioEnvironment env;
    env.set_reg(ZynqUioBridge::REG_HARDWARE_ID, ZynqUioBridge::HARDWARE_MAGIC);
    env.set_reg(ZynqUioBridge::REG_ING_FREE, 128);
    env.set_reg(ZynqUioBridge::REG_EGR_COUNT, 0);
    ::setenv("ABSTRACTX_UIO_DEV", env.path, 1);

    MpscTlpRing<> egress_tx_ring{};
    static MpscTlpRing<> ingress_rx_ring{};
    CallbackHelper helper;
    IoProcessorSetup setup{};
    setup.egress_tx_ring = &egress_tx_ring;
    setup.ingress_rx_ring = &ingress_rx_ring;
    setup.on_rx_pushed = etl::delegate<void()>::create<CallbackHelper, &CallbackHelper::on_pushed>(helper);
    IIoProcessor& io_proc = get_target_io_processor();
    assert(io_proc.configure(setup) && io_proc.start());

    const Tlp64 filler{};
    while (ingress_rx_ring.push(filler)) {}
    assert(ingress_rx_ring.size() == ingress_rx_ring.capacity());

    env.set_reg(ZynqUioBridge::REG_EGR_COUNT, 5);
    for (int i = 0; i < 1000; ++i) {
        assert(io_proc.step(0) == 0 && "nothing may be taken from the FPGA while the ring is full");
    }
    assert(!helper.rx_pushed && "on_rx_pushed must not run for a packet that was not pushed");

    Tlp64 out{};
    assert(ingress_rx_ring.pop(out));        // consumer frees one slot
    env.set_reg(ZynqUioBridge::REG_EGR_COUNT, 1);
    assert(io_proc.step(0) == 1 && "one slot free: exactly one packet moves");
    assert(helper.rx_pushed);

    io_proc.stop();
    ::unsetenv("ABSTRACTX_UIO_DEV");
    std::printf("  -> full ring backpressure passed.\n");
}

// [SPEC-ZYNQ-03] one user of the TLP ports (EL-1027-22): a second open_uio on the same device is
// refused while the first holds it, and succeeds once it closed.
void test_uio_single_user() {
    std::printf("[TEST] Second user of the UIO device is refused (flock)...\n");
    MockUioEnvironment env;
    env.set_reg(ZynqUioBridge::REG_HARDWARE_ID, ZynqUioBridge::HARDWARE_MAGIC);
    ZynqUioBridge first;
    ZynqUioBridge second;
    assert(first.open_uio(env.path));
    assert(!second.open_uio(env.path) && "device is held: refused");
    assert(!second.is_open());
    first.close_uio();
    assert(second.open_uio(env.path) && "free again: opens");
    second.close_uio();
    std::printf("  -> single user passed.\n");
}

// EL-1027-19: 1000 request / completion cycles through step(): every TLP crosses with its 16 words in
// order (DW0 first, [SPEC-ZYNQ-03]), each completion is pushed once, nothing piles up; then one
// step(timeout) runs the poll / interrupt-taken / re-arm path ([SPEC-ZYNQ-02]). The mock device is a
// plain file, so poll() always reports it readable and the wait itself is not tested; the UIO "unmask"
// write() also lands in the file (offset 8 = REG_IRQ_STATUS here), so the final check shows the path ran
// and ended armed, not which of the two writes set the bit. The real wait needs the board.
void test_steady_state_1000_and_poll_path() {
    std::printf("[TEST] 1000 request/completion cycles, then the poll/ack path...\n");
    MockUioEnvironment env;
    env.set_reg(ZynqUioBridge::REG_HARDWARE_ID, ZynqUioBridge::HARDWARE_MAGIC);
    env.set_reg(ZynqUioBridge::REG_ING_FREE, 128);
    env.set_reg(ZynqUioBridge::REG_EGR_COUNT, 0);
    ::setenv("ABSTRACTX_UIO_DEV", env.path, 1);

    MpscTlpRing<> egress_tx_ring{};
    static MpscTlpRing<> ingress_rx_ring{};
    struct Count { int n{0}; void on_pushed() { ++n; } } count;
    IoProcessorSetup setup{};
    setup.egress_tx_ring = &egress_tx_ring;
    setup.ingress_rx_ring = &ingress_rx_ring;
    setup.on_rx_pushed = etl::delegate<void()>::create<Count, &Count::on_pushed>(count);
    IIoProcessor& io_proc = get_target_io_processor();
    Tlp64 drain{};
    while (ingress_rx_ring.pop(drain)) {}
    assert(io_proc.configure(setup) && io_proc.start());

    for (uint32_t i = 0; i < 1000; ++i) {
        const Tlp64 req = Tlp64::make_mem_write(0x40000008u, 0xA5000000u | i, static_cast<uint8_t>(1 + i % 255));
        assert(egress_tx_ring.push(req));
        assert(io_proc.step(0) == 1 && "one request out");
        const auto* w = reinterpret_cast<const uint32_t*>(&req.wire);
        assert(env.get_reg(ZynqUioBridge::REG_TLP_IN_PORT) == w[15] && "DW15 is the last word written");
        assert((w[0] & 0xFFU) == 0x02U && w[1] == 0x40000008u && "DW0 = type..channel, DW1 = address");

        env.set_reg(ZynqUioBridge::REG_TLP_OUT_PORT, 0x03000000u | i);   // the completion's words
        env.set_reg(ZynqUioBridge::REG_EGR_COUNT, 1);
        assert(io_proc.step(0) == 1 && "one completion in");
        env.set_reg(ZynqUioBridge::REG_EGR_COUNT, 0);
        Tlp64 got{};
        assert(ingress_rx_ring.pop(got));
        assert(reinterpret_cast<const uint32_t*>(&got.wire)[15] == (0x03000000u | i));
        assert(!ingress_rx_ring.pop(got) && egress_tx_ring.size() == 0 && "nothing piles up");
    }
    assert(count.n == 1000 && "on_rx_pushed once per completion");

    // poll / interrupt-taken / re-arm: nothing to do, timeout > 0 -> poll, read the event, drain, re-arm
    env.set_reg(ZynqUioBridge::REG_IRQ_STATUS, 0);
    assert(io_proc.step(1) == 0);
    assert(env.get_reg(ZynqUioBridge::REG_IRQ_STATUS) == 0x01 && "egress empty: W1C written, re-armed");

    io_proc.stop();
    ::unsetenv("ABSTRACTX_UIO_DEV");
    std::printf("  -> 1000 cycles and poll/ack passed.\n");
}

int main() {
    std::printf("==============================================================================\n");
    std::printf(" Zynq-7000 Target HAL & IoProcessor Unit Verification\n");
    std::printf("==============================================================================\n");

    test_uio_bridge_hardware_magic_and_enable();
    test_uio_bridge_tlp_ingress_and_egress();
    test_uio_irq_rearm_after_burst();
    test_zynq_io_processor_reactor_loop();
    test_full_ingress_ring_backpressure();
    test_uio_single_user();
    test_steady_state_1000_and_poll_path();

    std::printf("------------------------------------------------------------------------------\n");
    std::printf("[SUCCESS] 100%% Zynq-7000 Target Data Plane Loops Verified.\n");
    return 0;
}
