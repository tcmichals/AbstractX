// @verifies [SPEC-E906-IPC-01] [SPEC-E906-IPC-02] [SPEC-E906-IPC-03] [SPEC-E906-IPC-04] [SPEC-E906-CORO-01]
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: E906 RPMsg Flow Control, Torn Index Protection & Kick Suppression
 * Verifies parity with cubie-a5e firmware fixes:
 *   - Natural vring alignment (unpacked)
 *   - Torn avail index detection (avail_pending)
 *   - Reply buffer exhaustion wait (flow control)
 *   - Doorbell wait for hardware mailbox FIFO space
 *   - Doorbell queue draining
 *   - VirtIO kick suppression (VRING_USED_F_NO_NOTIFY)
 * and the 2026-10-26 hardening: every wait bounded (fakes that never recover),
 * ring errors skipped or resynced, host addresses checked against the window,
 * tx_cancel, Lite kick re-enable, a 1000-cycle steady state, and the awaiter wake slot.
 */

// The checks below have side effects (is_driver_ready() maps the vrings): they must run in
// RelWithDebInfo / Release builds too. Without this, NDEBUG removed them and later blocks
// wrote through vring pointers of an earlier, unmapped FakeHost ([SPEC-E906-IPC-02]).
#undef NDEBUG

#include "hal/idle_policy.hpp"
#include "hal/rpmsg.hpp"
#include "resource_table.h"
#include "abstractx/isr_dispatcher.hpp"

#include <coroutine>

#include <sys/mman.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include <functional>

namespace hal {
extern int g_fake_msgbox_sends[4];
extern int g_fake_msgbox_full_sends_left;
extern int g_fake_msgbox_rx_pending;
extern std::function<void()> g_fake_msgbox_on_send;
}

extern "C" void fc_msgbox_doorbell_isr() noexcept;

namespace {

constexpr uint32_t kN = VRING_NUM_DESCS;
constexpr uint32_t kAlign = VRING_ALIGN;
constexpr uint32_t kBufSize = RPMSG_BUFFER_SIZE;
constexpr size_t kBlock = 1 << 20;

struct Desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};
struct Avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VRING_NUM_DESCS];
};
struct UsedElem {
    uint32_t id;
    uint32_t len;
};
struct Used {
    uint16_t flags;
    uint16_t idx;
    UsedElem ring[VRING_NUM_DESCS];
};

struct Vring {
    volatile Desc *desc;
    volatile Avail *avail;
    volatile Used *used;
};

class FakeHost {
public:
    explicit FakeHost(uint32_t num = kN) : num_(num) {
        // Below 4 GB (vring addresses are 32-bit). MAP_32BIT is x86-64 only; elsewhere a
        // low hint address, checked below.
        int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_32BIT
        flags |= MAP_32BIT;
#endif
        mem_ = static_cast<uint8_t *>(mmap(reinterpret_cast<void *>(0x20000000u), kBlock,
                                           PROT_READ | PROT_WRITE, flags, -1, 0));
        assert(mem_ != MAP_FAILED && reinterpret_cast<uintptr_t>(mem_) + kBlock <= 0xFFFFFFFFu);
        std::memset(mem_, 0, kBlock);

        rsc_ = reinterpret_cast<rpmsg_resource_table *>(mem_);
        rsc_->vdev.vring[0].da = da(0x1000);
        rsc_->vdev.vring[0].num = num;
        rsc_->vdev.vring[0].align = kAlign;
        rsc_->vdev.vring[1].da = da(0x4000);
        rsc_->vdev.vring[1].num = num;
        rsc_->vdev.vring[1].align = kAlign;
        rsc_->vdev.status = VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER | VIRTIO_CONFIG_S_DRIVER_OK;

        rx_ = layout(0x1000); // vring[0]: Linux RX / Remote TX (replies)
        tx_ = layout(0x4000); // vring[1]: Linux TX / Remote RX (incoming)
    }
    ~FakeHost() {
        munmap(mem_, kBlock);
    }

    // Also makes this block the window the driver accepts host addresses from.
    const rpmsg_resource_table *rsc() const {
        hal::rpmsg_set_host_window(reinterpret_cast<uintptr_t>(mem_), kBlock);
        return rsc_;
    }
    void set_desc_addr(uint16_t d, uint64_t addr) { tx_.desc[d].addr = addr; }
    void set_avail_slot(uint16_t slot, uint16_t d) { tx_.avail->ring[slot % num_] = d; }
    Vring &rx() { return rx_; }
    Vring &tx() { return tx_; }

    void post(uint16_t d, const void *payload, uint16_t len, uint16_t hdr_len_override = 0) {
        uint8_t *buf = mem_ + 0x10000 + d * kBufSize;
        auto *hdr = reinterpret_cast<rpmsg_hdr *>(buf);
        hdr->src = 1024;
        hdr->dst = 1024;
        hdr->len = hdr_len_override ? hdr_len_override : len;
        hdr->flags = 0;
        hdr->reserved = 0;
        if (payload && len) {
            std::memcpy(hdr->data, payload, len);
        }
        tx_.desc[d].addr = da(0x10000 + d * kBufSize);
        tx_.desc[d].len = kBufSize;
        tx_.avail->ring[tx_.avail->idx % num_] = d;
        tx_.avail->idx = static_cast<uint16_t>(tx_.avail->idx + 1);
    }

    void lend(uint16_t d) {
        rx_.desc[d].addr = da(0x10000 + (kN + d) * kBufSize);
        rx_.desc[d].len = kBufSize;
        rx_.avail->ring[rx_.avail->idx % num_] = d;
        rx_.avail->idx = static_cast<uint16_t>(rx_.avail->idx + 1);
    }

    const rpmsg_hdr *reply_buf(uint32_t d) const {
        return reinterpret_cast<const rpmsg_hdr *>(mem_ + 0x10000 + (kN + d) * kBufSize);
    }

private:
    uint32_t da(size_t off) const {
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mem_ + off));
    }
    Vring layout(size_t off) {
        uint8_t *base = mem_ + off;
        size_t avail_off = sizeof(Desc) * num_;
        size_t used_off = (avail_off + sizeof(Avail) + kAlign - 1) & ~static_cast<size_t>(kAlign - 1);
        return {reinterpret_cast<volatile Desc *>(base),
                reinterpret_cast<volatile Avail *>(base + avail_off),
                reinterpret_cast<volatile Used *>(base + used_off)};
    }

    uint32_t num_;
    uint8_t *mem_;
    rpmsg_resource_table *rsc_;
    Vring rx_, tx_;
};

std::vector<std::vector<uint8_t>> g_received;
bool g_echo = false;
bool g_reply_ok = true;

void on_test_message(const hal::RpmsgMessage &msg, void *) {
    g_received.emplace_back(msg.data, msg.data + msg.len);
    if (g_echo) {
        g_reply_ok = hal::Rpmsg::reply_active(msg, msg.data, msg.len);
    }
}

void reset_fake_env() {
    hal::g_fake_msgbox_sends[0] = 0;
    hal::g_fake_msgbox_sends[1] = 0;
    hal::g_fake_msgbox_full_sends_left = 0;
    hal::g_fake_msgbox_rx_pending = 0;
    hal::g_fake_msgbox_on_send = nullptr;
    g_received.clear();
    g_echo = false;
    g_reply_ok = true;
}

} // namespace

static void test_idle_wake_deadline() {
    using hal::idle::wake_deadline_us;
    using hal::idle::kOwedRetryUs;
    assert(wake_deadline_us(10, 1'000'010, false) == 1'000'010);
    assert(wake_deadline_us(10, 1'000'010, true) == 10 + kOwedRetryUs);
    assert(wake_deadline_us(10, 30, true) == 30 && "an earlier status deadline still wins");
    assert(wake_deadline_us(10, UINT64_MAX, true) == 10 + kOwedRetryUs);
    assert(wake_deadline_us(10, UINT64_MAX, false) == UINT64_MAX);
}

int main() {
    test_idle_wake_deadline();
    std::cout << "[TEST] XuanTie E906 RPMsg Flow Control, Alignment & Kick Suppression Verification\n";

    // 1. Delivers posted messages in order and returns buffers
    {
        reset_fake_env();
        FakeHost host;
        hal::Rpmsg rpmsg;
        hal::Rpmsg::set_active_driver(&rpmsg);
        rpmsg.init(host.rsc());
        rpmsg.register_endpoint(1024, on_test_message);
        assert(rpmsg.is_driver_ready());

        const char a[] = "first", b[] = "second";
        host.post(3, a, sizeof(a));
        host.post(7, b, sizeof(b));

        assert(rpmsg.poll());
        assert(g_received.size() == 2);
        assert(std::memcmp(a, g_received[0].data(), sizeof(a)) == 0);
        assert(std::memcmp(b, g_received[1].data(), sizeof(b)) == 0);

        assert(host.tx().used->idx == 2);
        assert(host.tx().used->ring[0].id == 3);
        assert(host.tx().used->ring[1].id == 7);
        assert(!rpmsg.poll());
        assert(rpmsg.get_ring_errors() == 0);
        std::cout << "  -> DeliversEachPostedMessageOnceAndReturnsItsBuffer: PASS\n";
    }

    // 2. Torn Index Protection: Index further ahead than ring size is rejected
    {
        reset_fake_env();
        FakeHost host;
        hal::Rpmsg rpmsg;
        hal::Rpmsg::set_active_driver(&rpmsg);
        rpmsg.init(host.rsc());
        rpmsg.register_endpoint(1024, on_test_message);
        assert(rpmsg.is_driver_ready());

        // Simulate torn read (e.g. 0x01FF -> 0x02FF: 255 ahead)
        host.tx().avail->idx = 255;
        assert(!rpmsg.poll());
        assert(g_received.empty());
        assert(host.tx().used->idx == 0);
        assert(rpmsg.get_ring_errors() == 1);
        std::cout << "  -> IndexFurtherAheadThanTheRingIsRejected: PASS\n";
    }

    // 3. Exactly full ring accepted
    {
        reset_fake_env();
        FakeHost host;
        hal::Rpmsg rpmsg;
        hal::Rpmsg::set_active_driver(&rpmsg);
        rpmsg.init(host.rsc());
        rpmsg.register_endpoint(1024, on_test_message);
        assert(rpmsg.is_driver_ready());

        uint8_t byte = 0x55;
        for (uint16_t d = 0; d < kN; ++d) {
            host.post(d, &byte, 1);
        }
        assert(rpmsg.poll());
        assert(g_received.size() == kN);
        assert(rpmsg.get_ring_errors() == 0);
        std::cout << "  -> ExactlyAFullRingIsAccepted: PASS\n";
    }

    // 4. Index wrapping around 65536
    {
        reset_fake_env();
        FakeHost host;
        hal::Rpmsg rpmsg;
        hal::Rpmsg::set_active_driver(&rpmsg);
        rpmsg.init(host.rsc());
        rpmsg.register_endpoint(1024, on_test_message);
        assert(rpmsg.is_driver_ready());

        uint8_t byte = 0xAA;
        for (uint32_t i = 0; i < 65530; ++i) {
            host.post(i % kN, &byte, 1);
            assert(rpmsg.poll());
        }
        g_received.clear();
        for (uint16_t i = 0; i < 12; ++i) {
            host.post(i, &byte, 1);
        }
        assert(rpmsg.poll());
        assert(g_received.size() == 12);
        assert(rpmsg.get_ring_errors() == 0);
        std::cout << "  -> IndexWrapsAt65536: PASS\n";
    }

    // 5. Malformed payload length or out-of-bounds descriptor
    {
        reset_fake_env();
        FakeHost host;
        hal::Rpmsg rpmsg;
        hal::Rpmsg::set_active_driver(&rpmsg);
        rpmsg.init(host.rsc());
        rpmsg.register_endpoint(1024, on_test_message);
        assert(rpmsg.is_driver_ready());

        uint8_t byte = 0;
        host.post(0, &byte, 1, RPMSG_BUFFER_SIZE); // header claims more than buffer
        assert(rpmsg.poll());
        assert(g_received.empty()); // delivered nothing
        assert(host.tx().used->idx == 1); // buffer returned
        assert(rpmsg.get_ring_errors() == 1);
        std::cout << "  -> LengthLargerThanBufferIsMalformed: PASS\n";
    }

    // 6. Flow control: Reply waits for host buffer instead of dropping
    {
        reset_fake_env();
        FakeHost host;
        hal::Rpmsg rpmsg;
        hal::Rpmsg::set_active_driver(&rpmsg);
        rpmsg.init(host.rsc());
        rpmsg.register_endpoint(1024, on_test_message);
        assert(rpmsg.is_driver_ready());

        int doorbells = 0;
        hal::g_fake_msgbox_on_send = [&] {
            if (++doorbells == 3) {
                host.lend(0); // lend buffer after 3 doorbells
            }
        };
        g_echo = true;
        const char ping[] = "wait-for-buffer";
        host.post(0, ping, sizeof(ping));

        assert(rpmsg.poll());
        assert(g_reply_ok); // replied successfully, did not drop!
        assert(host.rx().used->idx == 1);
        assert(std::memcmp(ping, host.reply_buf(0)->data, sizeof(ping)) == 0);
        assert(rpmsg.get_tx_full_waits() == 3);
        std::cout << "  -> ReplyWaitsForHostBufferInsteadOfDropping: PASS\n";
    }

    // 7. Non-dropping mailbox kicks: Doorbell waits for FIFO space
    {
        reset_fake_env();
        FakeHost host;
        hal::Rpmsg rpmsg;
        hal::Rpmsg::set_active_driver(&rpmsg);
        rpmsg.init(host.rsc());
        rpmsg.register_endpoint(1024, on_test_message);
        assert(rpmsg.is_driver_ready());

        hal::g_fake_msgbox_full_sends_left = 5; // FIFO full 5 times
        uint8_t byte = 0x11;
        host.post(0, &byte, 1);

        assert(rpmsg.poll());
        assert(hal::g_fake_msgbox_sends[1] >= 1);
        assert(rpmsg.get_mbox_full_waits() == 5);
        std::cout << "  -> DoorbellWaitsForFifoSpaceInsteadOfDropping: PASS\n";
    }

    // 8. Queued host doorbells are drained in poll()
    {
        reset_fake_env();
        FakeHost host;
        hal::Rpmsg rpmsg;
        hal::Rpmsg::set_active_driver(&rpmsg);
        rpmsg.init(host.rsc());
        assert(rpmsg.is_driver_ready());

        hal::g_fake_msgbox_rx_pending = 6;
        rpmsg.poll();
        assert(hal::g_fake_msgbox_rx_pending == 0);
        assert(rpmsg.get_host_kicks() == 6);
        std::cout << "  -> AllQueuedHostDoorbellsAreDrained: PASS\n";
    }

    // 9. Kick suppression lifecycle
    {
        reset_fake_env();
        FakeHost host;
        hal::Rpmsg rpmsg;
        hal::Rpmsg::set_active_driver(&rpmsg);
        rpmsg.init(host.rsc());
        assert(rpmsg.is_driver_ready());

        // Default: suppressed (VRING_USED_F_NO_NOTIFY = 1)
        assert(host.tx().used->flags == 1);
        assert(host.rx().used->flags == 1);

        // AsyncRxAwaiter unsuppresses before sleeping
        rpmsg.enable_rx_kick(true);
        assert(host.tx().used->flags == 0);

        // AsyncRxAwaiter re-suppresses on resume
        rpmsg.enable_rx_kick(false);
        assert(host.tx().used->flags == 1);
        std::cout << "  -> KickSuppressionLifecycle: PASS\n";
    }

    // 10. RpmsgLiteMetal Driver Parity
    {
        reset_fake_env();
        FakeHost host;
        hal::RpmsgLiteMetal lite;
        hal::Rpmsg::set_active_driver(&lite);
        lite.init(host.rsc());
        lite.register_endpoint(1024, on_test_message);
        assert(lite.is_driver_ready());

        // Check defaults
        assert(host.rx().used->flags == 1);
        assert(host.tx().used->flags == 1);

        // Lend reply buffer in vring[0] and post a message to LiteMetal in vring[1]
        host.lend(0);
        const char ping[] = "LITE-PING";
        host.post(2, ping, sizeof(ping));

        g_echo = true;
        hal::g_fake_msgbox_full_sends_left = 2; // Test non-dropping FIFO send
        assert(lite.poll());
        assert(g_reply_ok);
        assert(lite.get_mbox_full_waits() == 2);
        assert(lite.get_rx_count() == 1);
        assert(lite.get_tx_count() == 1);
        std::cout << "  -> RpmsgLiteMetalParity: PASS\n";
    }

    // 11. Re-init forgets the previous resource table's vrings ([SPEC-E906-IPC-02] / [SPEC-E906-IPC-03]).
    //     Host A stays mapped so a stale write is caught by value, not by a lucky SEGV.
    {
        reset_fake_env();
        FakeHost host_a;
        FakeHost host_b;
        hal::Rpmsg rpmsg;
        hal::Rpmsg::set_active_driver(&rpmsg);
        rpmsg.init(host_a.rsc());
        assert(rpmsg.is_driver_ready());
        assert(host_a.tx().used->flags == 1);

        rpmsg.init(host_b.rsc());         // e.g. remoteproc restart with new vrings
        rpmsg.enable_rx_kick(true);       // before anything else touches the driver
        assert(host_a.tx().used->flags == 1 && "Rpmsg wrote into the old resource table's vring");
        assert(host_b.tx().used->flags == 0);

        hal::RpmsgLiteMetal lite;
        hal::Rpmsg::set_active_driver(&lite);
        lite.init(host_a.rsc());
        assert(lite.is_driver_ready());
        host_a.tx().used->flags = 1;
        lite.init(host_b.rsc());
        lite.enable_rx_kick(true);
        assert(host_a.tx().used->flags == 1 && "RpmsgLiteMetal wrote into the old resource table's vring");
        assert(host_b.tx().used->flags == 0);
        std::cout << "  -> ReinitForgetsOldVrings: PASS\n";
    }

    // 12. Reply gives up when the host never returns a buffer (bounded, [SPEC-E906-IPC-02]).
    //     Fails on the old code by hanging: there was no limit.
    {
        reset_fake_env();
        FakeHost host;
        hal::Rpmsg rpmsg;
        hal::Rpmsg::set_active_driver(&rpmsg);
        rpmsg.init(host.rsc());
        rpmsg.register_endpoint(1024, on_test_message);
        assert(rpmsg.is_driver_ready());

        g_echo = true;  // no host.lend(): Linux never posts a TX buffer
        const char ping[] = "nobody-reads";
        host.post(0, ping, sizeof(ping));
        assert(rpmsg.poll());
        assert(!g_reply_ok && "reply must return false, not spin");
        assert(rpmsg.get_tx_full_waits() == 64);
        assert(host.rx().used->idx == 0);
        std::cout << "  -> ReplyGivesUpWhenHostNeverReturnsABuffer: PASS\n";
    }

    // 13. A mailbox FIFO that never drains: poll returns, the kick is owed and sent later.
    {
        reset_fake_env();
        FakeHost host;
        hal::Rpmsg rpmsg;
        hal::Rpmsg::set_active_driver(&rpmsg);
        rpmsg.init(host.rsc());
        rpmsg.register_endpoint(1024, on_test_message);
        assert(rpmsg.is_driver_ready());

        hal::g_fake_msgbox_full_sends_left = 1 << 30;  // never recovers on its own
        uint8_t byte = 0x22;
        host.post(0, &byte, 1);
        assert(rpmsg.poll());
        assert(g_received.size() == 1 && host.tx().used->idx == 1);
        assert(rpmsg.get_mbox_full_waits() == 256);
        assert(hal::g_fake_msgbox_sends[1] == 0);
        // The owed kick is pending work: idle must wake within kOwedRetryUs, not at the
        // 1 s status deadline ([SPEC-E906-IPC-02]; ~1 s echo stalls without it).
        assert(rpmsg.kick_owed());
        assert(hal::idle::wake_deadline_us(1000, 1'001'000, rpmsg.kick_owed()) == 1000 + hal::idle::kOwedRetryUs);
        hal::g_fake_msgbox_full_sends_left = 0;
        rpmsg.flush_kick();
        assert(hal::g_fake_msgbox_sends[1] == 1 && "owed kick retried");
        assert(!rpmsg.kick_owed());
        assert(hal::idle::wake_deadline_us(1000, 1'001'000, rpmsg.kick_owed()) == 1'001'000);
        rpmsg.flush_kick();
        assert(hal::g_fake_msgbox_sends[1] == 1 && "and only once");
        std::cout << "  -> FullMailboxNeverHangsAndTheKickIsRetried: PASS\n";
    }

    // 14. A descriptor index >= num is skipped; the messages behind it still arrive.
    //     Old code returned at the bad entry on every poll: RX stalled for good.
    {
        reset_fake_env();
        FakeHost host;
        hal::Rpmsg rpmsg;
        hal::Rpmsg::set_active_driver(&rpmsg);
        rpmsg.init(host.rsc());
        rpmsg.register_endpoint(1024, on_test_message);
        assert(rpmsg.is_driver_ready());

        const char a[] = "lost", b[] = "behind";
        host.post(0, a, sizeof(a));
        host.set_avail_slot(0, 99);  // corrupt entry
        host.post(1, b, sizeof(b));
        assert(rpmsg.poll());
        assert(g_received.size() == 1 && std::memcmp(b, g_received[0].data(), sizeof(b)) == 0);
        assert(rpmsg.get_ring_errors() == 1);
        host.post(2, a, sizeof(a));
        assert(rpmsg.poll() && g_received.size() == 2);
        std::cout << "  -> BadDescriptorIsSkippedAndRxContinues: PASS\n";
    }

    // 15. A persistent bad avail index is resynced after 3 reads; RX resumes.
    {
        reset_fake_env();
        FakeHost host;
        hal::Rpmsg rpmsg;
        hal::Rpmsg::set_active_driver(&rpmsg);
        rpmsg.init(host.rsc());
        rpmsg.register_endpoint(1024, on_test_message);
        assert(rpmsg.is_driver_ready());

        host.tx().avail->idx = 255;
        for (int i = 0; i < 3; ++i) {
            assert(!rpmsg.poll());
        }
        assert(rpmsg.get_ring_errors() == 3);
        uint8_t byte = 0x33;
        host.post(4, &byte, 1);
        assert(rpmsg.poll() && g_received.size() == 1);
        std::cout << "  -> PersistentBadIndexIsResyncedAndRxResumes: PASS\n";
    }

    // 16. A host address outside the window is rejected, never dereferenced.
    {
        reset_fake_env();
        FakeHost host;
        hal::Rpmsg rpmsg;
        hal::Rpmsg::set_active_driver(&rpmsg);
        rpmsg.init(host.rsc());
        rpmsg.register_endpoint(1024, on_test_message);
        assert(rpmsg.is_driver_ready());

        uint8_t byte = 0x44;
        host.post(0, &byte, 1);
        host.set_desc_addr(0, 0x1000);  // e.g. firmware SRAM or MMIO
        assert(rpmsg.poll());
        assert(g_received.empty() && rpmsg.get_ring_errors() == 1);
        assert(host.tx().used->idx == 1 && "buffer still returned");
        std::cout << "  -> OutOfWindowHostAddressIsRejected: PASS\n";
    }

    // 17. Lite parity: poll() leaves host kicks enabled (old Lite left NO_NOTIFY set).
    {
        reset_fake_env();
        FakeHost host;
        hal::RpmsgLiteMetal lite;
        lite.init(host.rsc());
        lite.register_endpoint(1024, on_test_message);
        assert(lite.is_driver_ready());

        uint8_t byte = 0x55;
        host.post(0, &byte, 1);
        assert(lite.poll());
        assert(host.tx().used->flags == 0 && "Lite must re-enable kicks before its last check");
        std::cout << "  -> LitePollLeavesKicksEnabled: PASS\n";
    }

    // 18. tx_cancel hands back an unused buffer instead of a 0-byte message.
    {
        reset_fake_env();
        FakeHost host;
        hal::Rpmsg rpmsg;
        rpmsg.init(host.rsc());
        assert(rpmsg.is_driver_ready());
        host.lend(0);
        host.lend(1);
        hal::RpmsgTxBuffer buf = rpmsg.tx_acquire();
        assert(buf && rpmsg.tx_free_buffers() == 1);
        assert(rpmsg.tx_cancel(buf) && rpmsg.tx_free_buffers() == 2);
        assert(!rpmsg.tx_cancel(buf) && "only once");
        assert(host.rx().used->idx == 0 && "nothing published");
        hal::RpmsgTxBuffer a = rpmsg.tx_acquire();
        hal::RpmsgTxBuffer b = rpmsg.tx_acquire();
        assert(!rpmsg.tx_cancel(a) && "not the most recent: refused");
        assert(rpmsg.tx_cancel(b));
        std::cout << "  -> TxCancelReturnsAnUnusedBuffer: PASS\n";
    }

    // 19. Steady state (AGENTS §10.5): 1000 acquire/commit cycles with the host recycling
    //     buffers; nothing leaks and no error is counted.
    {
        reset_fake_env();
        FakeHost host;
        hal::Rpmsg rpmsg;
        rpmsg.init(host.rsc());
        assert(rpmsg.is_driver_ready());
        for (uint32_t i = 0; i < 1000; ++i) {
            host.lend(static_cast<uint16_t>(i % kN));
            hal::RpmsgTxBuffer buf = rpmsg.tx_acquire();
            assert(buf && buf.payload.size() == kBufSize - sizeof(rpmsg_hdr));
            buf.payload[0] = static_cast<uint8_t>(i);
            assert(rpmsg.tx_commit(buf, 1, 1024, 1024));
            assert(rpmsg.tx_free_buffers() == 0);
        }
        assert(rpmsg.get_tx_count() == 1000 && rpmsg.get_ring_errors() == 0);
        assert(host.rx().used->idx == 1000);
        std::cout << "  -> ThousandTxCyclesLeakNothing: PASS\n";
    }

    // 20. Awaiter wake slot ([SPEC-E906-CORO-01]): one waiter; the ISR frees the slot.
    {
        reset_fake_env();
        FakeHost host;
        hal::Rpmsg rpmsg;
        rpmsg.init(host.rsc());
        assert(rpmsg.is_driver_ready());
        auto first = rpmsg.async_receive();
        auto second = rpmsg.async_receive();
        assert(!first.await_ready());
        assert(first.await_suspend(std::noop_coroutine()));
        assert(host.tx().used->flags == 0 && "kicks enabled while waiting");
        assert(!second.await_suspend(std::noop_coroutine()) && "second waiter must not overwrite");
        fc_msgbox_doorbell_isr();  // posts the first waiter, frees the slot
        abstractx::Dispatcher::process();
        auto third = rpmsg.async_receive();
        assert(third.await_suspend(std::noop_coroutine()));
        (void)third.await_resume();  // clears its own slot
        auto fourth = rpmsg.async_receive();
        assert(fourth.await_suspend(std::noop_coroutine()));
        (void)fourth.await_resume();
        (void)first.await_resume();
        (void)hal::rpmsg_take_doorbell();
        std::cout << "  -> AwaiterWakeSlotHasOneOwner: PASS\n";
    }

    std::cout << "[ALL PASS] XuanTie E906 RPMsg Flow Control Verification complete!\n";
    return 0;
}
