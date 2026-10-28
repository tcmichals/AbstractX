#include "hal/rpmsg.hpp"
#include "hal/msgbox.hpp"
#include "hal/pmp.hpp"
#include "hal/clic.hpp"
#include "abstractx/isr_dispatcher.hpp"
#include <string.h>
#include <span>

namespace hal {

// One wake slot shared with fc_msgbox_doorbell_isr() ([SPEC-E906-CORO-01]).
// @impl [SPEC-E906-CORO-01] targets/allwinner_e906/SPECIFICATION.md
// shared: awaiter sets (main), msgbox ISR takes; std::atomic CAS/exchange acq_rel
static std::atomic<void *> s_rpmsg_waiter{nullptr};

// Message box doorbell state ([SPEC-E906-IRQ-04]); set by the ISR, taken by consumers.
// shared: msgbox ISR sets, platform_poll_network() takes (single owner); std::atomic acq_rel
static std::atomic<bool> s_doorbell{false};
// shared: ISR clears, rpmsg_arm_doorbell() sets with the IRQ masked; std::atomic acq_rel
static std::atomic<bool> s_doorbell_armed{false};
// shared: ISR increments, main reads (statistics only); std::atomic relaxed
static std::atomic<uint32_t> s_doorbell_irqs{0};

static void msgbox_rx_irq(bool enable) noexcept {
    // Linux kicks on channels 0 and 1; both are read by drain_doorbells()
    MsgBox::enable_rx_irq(MsgBox::Channel::Channel0, enable);
    MsgBox::enable_rx_irq(MsgBox::Channel::Channel1, enable);
}

bool rpmsg_take_doorbell() noexcept {
    return s_doorbell.exchange(false, std::memory_order_acq_rel);
}

void rpmsg_arm_doorbell() noexcept {
    if (!s_doorbell_armed.exchange(true, std::memory_order_acq_rel)) {
        MsgBox::clear_irq_status(MsgBox::Channel::Channel0);
        MsgBox::clear_irq_status(MsgBox::Channel::Channel1);
        msgbox_rx_irq(true);  // level line: fires at once if a kick is still queued
    }
}

bool rpmsg_doorbell_pending() noexcept {
    return s_doorbell.load(std::memory_order_acquire);
}

uint32_t rpmsg_doorbell_irqs() noexcept {
    return s_doorbell_irqs.load(std::memory_order_relaxed);
}

/*
 * VirtIO Ring Internal Descriptors
 *
 * Not packed: the virtio layout is naturally aligned, and packing made the
 * compiler split every 16-bit ring index into two byte accesses. Linux updates
 * an index with one 16-bit store, so a byte-wise read could combine old and new
 * halves (e.g. 0x01FF -> 0x0200 read as 0x02FF) and the RX loop then replayed
 * ~65k stale descriptors. Aligned fields give single-copy-atomic accesses.
 * @impl [SPEC-E906-IPC-02] targets/allwinner_e906/SPECIFICATION.md
 */
struct VirtioDesc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct VirtioAvail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VRING_NUM_DESCS];
};

struct VirtioUsedElem {
    uint32_t id;
    uint32_t len;
};

struct VirtioUsed {
    uint16_t flags;
    uint16_t idx;
    struct VirtioUsedElem ring[VRING_NUM_DESCS];
};

static_assert(sizeof(VirtioDesc) == 16, "virtio desc layout");
static_assert(offsetof(VirtioAvail, idx) == 2 && offsetof(VirtioAvail, ring) == 4, "virtio avail layout");
static_assert(sizeof(VirtioUsedElem) == 8, "virtio used elem layout");
static_assert(offsetof(VirtioUsed, idx) == 2 && offsetof(VirtioUsed, ring) == 4, "virtio used layout");
static_assert(alignof(VirtioAvail) == 2 && alignof(VirtioUsed) == 4 && alignof(VirtioDesc) == 8,
              "vring structs must not be packed");

struct VirtQueueLayout {
    uint32_t da;
    uint32_t num;
    uint32_t mask;
    uint32_t align;
    volatile struct VirtioDesc  *desc;
    volatile struct VirtioAvail *avail;
    volatile struct VirtioUsed  *used;
    uint16_t last_avail_idx;
    uint8_t  bad_reads;  // consecutive pending > num reads ([SPEC-E906-IPC-02] resync)
};

namespace {

constexpr uint16_t VRING_USED_F_NO_NOTIFY = 1;
// @impl [SPEC-E906-IPC-02] targets/allwinner_e906/SPECIFICATION.md
constexpr uint32_t kMboxSendTries   = 256;
constexpr uint32_t kReplyWaitRounds = 64;
constexpr uint8_t  kRingResyncAfter = 3;
constexpr size_t   kMaxEndpoints    = 8;
constexpr size_t   kHdr             = sizeof(struct rpmsg_hdr);

// Host physical -> E906 windows that host buffers and vrings may use: the RPMsg pools of the
// two overlays only ([SPEC-E906-IPC-02], [SPEC-E906-MEM-01]). Never pass an unknown address through.
// @impl [SPEC-E906-IPC-02] targets/allwinner_e906/SPECIFICATION.md
struct HostWindow {
    uint64_t  host;
    uintptr_t core;
    uint32_t  size;
};
#if defined(__riscv)
constexpr HostWindow s_windows[] = {
    {0x48000000u, 0x48000000u, 0x100000u},  // flight-stack overlay: vdev@48000000 DDR pool (board)
    {0x072E0000u, 0x40020000u, 0x20000u},   // SRAM-pool overlay: SRAM Space 1 upper half
};
#else
HostWindow s_windows[] = {{0, 0, 0}};  // host tests: rpmsg_set_host_window()
#endif

void *translate_buf(uint64_t host_addr, uint32_t len) {
    for (const HostWindow &w : s_windows) {
        if (len <= w.size && host_addr >= w.host && host_addr - w.host <= w.size - len) {
            return reinterpret_cast<void *>(w.core + static_cast<uintptr_t>(host_addr - w.host));
        }
    }
    return nullptr;
}

bool setup_vq(VirtQueueLayout &vq, uint32_t da, uint32_t num, uint32_t align) {
    vq = VirtQueueLayout{};
    if (num == 0 || num > VRING_NUM_DESCS || (num & (num - 1)) != 0 ||
        align == 0 || (align & (align - 1)) != 0) {
        return false;
    }
    const size_t avail_offset = sizeof(struct VirtioDesc) * num;
    const size_t used_offset = (avail_offset + sizeof(uint16_t) * (3 + num) + align - 1) &
                               ~static_cast<size_t>(align - 1);
    const size_t ring_bytes = used_offset + sizeof(uint16_t) * 3 + sizeof(struct VirtioUsedElem) * num;
    auto *base = static_cast<uint8_t *>(translate_buf(da, static_cast<uint32_t>(ring_bytes)));
    if (base == nullptr) {
        return false;
    }
    vq.da = da;
    vq.num = num;
    vq.mask = num - 1;
    vq.align = align;
    vq.desc = reinterpret_cast<volatile struct VirtioDesc *>(base);
    vq.avail = reinterpret_cast<volatile struct VirtioAvail *>(base + avail_offset);
    vq.used = reinterpret_cast<volatile struct VirtioUsed *>(base + used_offset);
    return true;
}

struct EndpointSlot {
    uint32_t addr;
    EndpointCallback cb;
    void *user_data;
};

// Shared virtio core for Rpmsg and RpmsgLiteMetal; they differ only in the kick
// channel, so the two can no longer drift apart ([SPEC-E906-IPC-03] parity).
// @impl [SPEC-E906-IPC-02] [SPEC-E906-IPC-03] targets/allwinner_e906/SPECIFICATION.md
struct Transport {
    explicit Transport(MsgBox::Channel ch) noexcept : kick_channel(ch) {}

    const MsgBox::Channel kick_channel;
    const struct rpmsg_resource_table *rsc = nullptr;
    bool ready = false;
    bool kick_owed = false;
    bool can_cancel = false;
    uint16_t cancel_pos = 0;

    std::atomic<uint32_t> rx_count{0};
    std::atomic<uint32_t> tx_count{0};
    std::atomic<uint32_t> ring_errors{0};
    std::atomic<uint32_t> tx_full_waits{0};
    std::atomic<uint32_t> mbox_full_waits{0};
    std::atomic<uint32_t> host_kicks{0};

    EndpointSlot endpoints[kMaxEndpoints]{};
    size_t num_endpoints = 0;

    VirtQueueLayout tx{};  // vring[0]: remote -> host
    VirtQueueLayout rx{};  // vring[1]: host -> remote
};

Transport s_linux{MsgBox::Channel::Channel1};  // Linux channel 9 (cubie-a5e D11)
Transport s_lite{MsgBox::Channel::Channel0};

void reset(Transport &t, const struct rpmsg_resource_table *rsc) {
    MsgBox::init();
    t.rsc = rsc;
    t.ready = false;
    t.kick_owed = false;
    t.can_cancel = false;
    // Forget the vrings of any earlier resource table; ready() maps the new ones.
    t.tx = VirtQueueLayout{};
    t.rx = VirtQueueLayout{};
    t.rx_count.store(0, std::memory_order_relaxed);
    t.tx_count.store(0, std::memory_order_relaxed);
    t.ring_errors.store(0, std::memory_order_relaxed);
    t.tx_full_waits.store(0, std::memory_order_relaxed);
    t.mbox_full_waits.store(0, std::memory_order_relaxed);
    t.host_kicks.store(0, std::memory_order_relaxed);
    t.num_endpoints = 0;
    for (auto &ep : t.endpoints) {
        ep = EndpointSlot{0, nullptr, nullptr};
    }
}

void set_flags(VirtQueueLayout &vq, uint16_t flags) {
    if (vq.used->flags != flags) {
        vq.used->flags = flags;
    }
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

bool ready(Transport &t) {
    if (!t.rsc) return false;
    if (!t.ready) {
        volatile const struct rpmsg_resource_table *rsc = t.rsc;
        if ((rsc->vdev.status & VIRTIO_CONFIG_S_DRIVER_OK) &&
            rsc->vdev.vring[0].da != 0 && rsc->vdev.vring[1].da != 0) {
            const bool tx_ok = setup_vq(t.tx, rsc->vdev.vring[0].da, rsc->vdev.vring[0].num, rsc->vdev.vring[0].align);
            const bool rx_ok = setup_vq(t.rx, rsc->vdev.vring[1].da, rsc->vdev.vring[1].num, rsc->vdev.vring[1].align);
            if (tx_ok && rx_ok) {
                t.tx.used->flags = VRING_USED_F_NO_NOTIFY;
                t.rx.used->flags = VRING_USED_F_NO_NOTIFY;
                std::atomic_thread_fence(std::memory_order_release);
                t.ready = true;
            } else {
                t.tx = VirtQueueLayout{};
                t.rx = VirtQueueLayout{};
                t.ring_errors.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    return t.ready;
}

bool register_ep(Transport &t, uint32_t addr, EndpointCallback cb, void *user_data) {
    for (size_t i = 0; i < t.num_endpoints; ++i) {
        if (t.endpoints[i].addr == addr) {
            t.endpoints[i].cb = cb;
            t.endpoints[i].user_data = user_data;
            return true;
        }
    }
    if (t.num_endpoints >= kMaxEndpoints) return false;
    t.endpoints[t.num_endpoints++] = EndpointSlot{addr, cb, user_data};
    return true;
}

// Pending = avail.idx - last_avail_idx. More than num is a torn read: rejected, and
// after kRingResyncAfter in a row the ring is resynced so RX cannot stall forever.
// @impl [SPEC-E906-IPC-02] targets/allwinner_e906/SPECIFICATION.md
bool avail_pending(Transport &t, VirtQueueLayout &vq, uint16_t &pending) {
    pending = 0;
    if (vq.avail == nullptr) {
        return false;
    }
    const uint16_t avail_idx = vq.avail->idx;
    std::atomic_thread_fence(std::memory_order_acquire);
    const uint16_t ahead = static_cast<uint16_t>(avail_idx - vq.last_avail_idx);
    if (ahead > vq.num) {
        t.ring_errors.fetch_add(1, std::memory_order_relaxed);
        if (++vq.bad_reads >= kRingResyncAfter) {
            vq.last_avail_idx = avail_idx;
            vq.bad_reads = 0;
        }
        return false;
    }
    vq.bad_reads = 0;
    pending = ahead;
    return true;
}

// Bounded kick: a kick that does not fit is owed and retried later, never spun on.
// @impl [SPEC-E906-IPC-02] targets/allwinner_e906/SPECIFICATION.md
void notify(Transport &t) {
    for (uint32_t i = 0; i < kMboxSendTries; ++i) {
        if (MsgBox::send(t.kick_channel, 0)) {
            t.kick_owed = false;
            return;
        }
        t.mbox_full_waits.fetch_add(1, std::memory_order_relaxed);
    }
    t.kick_owed = true;
}

void retry_owed_kick(Transport &t) {
    if (t.kick_owed) {
        notify(t);
    }
}

void drain_doorbells(Transport &t) {
    uint32_t n = 0;
    while (MsgBox::is_rx_pending(MsgBox::Channel::Channel0)) {
        (void)MsgBox::receive(MsgBox::Channel::Channel0);
        ++n;
    }
    while (MsgBox::is_rx_pending(MsgBox::Channel::Channel1)) {
        (void)MsgBox::receive(MsgBox::Channel::Channel1);
        ++n;
    }
    if (n) {
        t.host_kicks.fetch_add(n, std::memory_order_relaxed);
    }
}

void publish_used(VirtQueueLayout &vq, uint16_t desc_idx, uint32_t len) {
    const uint16_t used_idx = vq.used->idx;
    vq.used->ring[used_idx & vq.mask].id = desc_idx;
    vq.used->ring[used_idx & vq.mask].len = len;
    std::atomic_thread_fence(std::memory_order_release);
    vq.used->idx = static_cast<uint16_t>(used_idx + 1);
}

uint16_t tx_free(Transport &t) {
    if (!ready(t)) {
        return 0;
    }
    uint16_t free_bufs = 0;
    (void)avail_pending(t, t.tx, free_bufs);
    // Out of buffers: let the host kick us when it returns one.
    set_flags(t.tx, free_bufs == 0 ? 0 : VRING_USED_F_NO_NOTIFY);
    return free_bufs;
}

// Take the next free host buffer. desc fields are read once and bounded; a bad
// entry is skipped (counted), so one corrupt descriptor cannot stop TX.
// @impl [SPEC-E906-IPC-02] [SPEC-E906-IPC-04] targets/allwinner_e906/SPECIFICATION.md
RpmsgTxBuffer tx_take(Transport &t, uint16_t min_free_after) {
    retry_owed_kick(t);
    for (;;) {
        if (tx_free(t) <= min_free_after) {
            return {};
        }
        const uint16_t pos = t.tx.last_avail_idx;
        const uint16_t desc_idx = t.tx.avail->ring[pos & t.tx.mask];
        t.tx.last_avail_idx = static_cast<uint16_t>(pos + 1);
        if (desc_idx >= t.tx.num) {
            t.ring_errors.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        const uint64_t addr = t.tx.desc[desc_idx].addr;
        uint32_t len = t.tx.desc[desc_idx].len;
        if (len > RPMSG_BUFFER_SIZE) {
            len = RPMSG_BUFFER_SIZE;
        }
        auto *hdr = (len > kHdr) ? static_cast<uint8_t *>(translate_buf(addr, len)) : nullptr;
        if (hdr == nullptr) {
            t.ring_errors.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        t.can_cancel = true;
        t.cancel_pos = pos;
        return {{hdr + kHdr, len - kHdr}, desc_idx};
    }
}

// Un-take the most recently acquired buffer instead of publishing a 0-byte message.
// @impl [SPEC-E906-IPC-04] targets/allwinner_e906/SPECIFICATION.md
bool tx_cancel(Transport &t, const RpmsgTxBuffer &buf) {
    if (!buf || !t.can_cancel || !t.ready ||
        static_cast<uint16_t>(t.cancel_pos + 1) != t.tx.last_avail_idx ||
        t.tx.avail->ring[t.cancel_pos & t.tx.mask] != buf.desc_idx) {
        return false;
    }
    t.tx.last_avail_idx = t.cancel_pos;
    t.can_cancel = false;
    return true;
}

// Publish a buffer lent by tx_take(): header, release fence, used ring, release
// fence, used index, kick. The header pointer comes from the token, not a re-read
// of the host-writable descriptor.
// @impl [SPEC-E906-IPC-04] targets/allwinner_e906/SPECIFICATION.md
bool tx_publish(Transport &t, const RpmsgTxBuffer &buf, uint16_t used_len, uint32_t src, uint32_t dst) {
    if (!t.ready || t.tx.used == nullptr || !buf || buf.desc_idx >= t.tx.num ||
        used_len > buf.payload.size()) {
        return false;
    }
    auto *hdr = reinterpret_cast<volatile struct rpmsg_hdr *>(buf.payload.data() - kHdr);
    hdr->src = src;
    hdr->dst = dst;
    hdr->len = used_len;
    hdr->flags = 0;
    hdr->reserved = 0;
    std::atomic_thread_fence(std::memory_order_release);
    publish_used(t.tx, buf.desc_idx, static_cast<uint32_t>(kHdr + used_len));
    t.can_cancel = false;
    notify(t);
    t.tx_count.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void dispatch(Transport &t, const RpmsgMessage &msg) {
    // Exact address first; an addr-0 catch-all never hides a later exact match.
    const EndpointSlot *any = nullptr;
    for (size_t i = 0; i < t.num_endpoints; ++i) {
        if (t.endpoints[i].addr == msg.dst) {
            if (t.endpoints[i].cb) {
                t.endpoints[i].cb(msg, t.endpoints[i].user_data);
            }
            return;
        }
        if (t.endpoints[i].addr == 0 && any == nullptr) {
            any = &t.endpoints[i];
        }
    }
    if (any && any->cb) {
        any->cb(msg, any->user_data);
    }
}

// @impl [SPEC-E906-IPC-02] [SPEC-E906-IPC-03] targets/allwinner_e906/SPECIFICATION.md
bool poll(Transport &t) {
    if (!ready(t)) {
        return false;
    }
    retry_owed_kick(t);
    drain_doorbells(t);

    uint16_t pending = 0;
    if (!avail_pending(t, t.rx, pending)) {
        set_flags(t.rx, 0);
        return false;
    }
    if (pending == 0) {
        return false;
    }

    // Suppress redundant host kicks while actively draining
    set_flags(t.rx, VRING_USED_F_NO_NOTIFY);

    bool consumed = false;
    for (;;) {
        for (; pending > 0; --pending) {
            const uint16_t desc_idx = t.rx.avail->ring[t.rx.last_avail_idx & t.rx.mask];
            t.rx.last_avail_idx++;
            consumed = true;
            if (desc_idx >= t.rx.num) {
                t.ring_errors.fetch_add(1, std::memory_order_relaxed);
                continue;  // invalid id: skip, nothing to return
            }
            // Host-writable fields are read once ([SPEC-E906-IPC-02]).
            const uint64_t buf_addr = t.rx.desc[desc_idx].addr;
            uint32_t buf_len = t.rx.desc[desc_idx].len;
            if (buf_len > RPMSG_BUFFER_SIZE) {
                buf_len = RPMSG_BUFFER_SIZE;
            }
            auto *hdr = (buf_len >= kHdr)
                ? static_cast<volatile struct rpmsg_hdr *>(translate_buf(buf_addr, buf_len))
                : nullptr;
            const uint16_t msg_len = hdr ? hdr->len : 0;
            if (hdr == nullptr || msg_len > buf_len - kHdr) {
                t.ring_errors.fetch_add(1, std::memory_order_relaxed);
            } else {
                RpmsgMessage msg;
                msg.src = hdr->src;
                msg.dst = hdr->dst;
                msg.len = msg_len;
                msg.data = const_cast<const uint8_t *>(hdr->data);
                msg.desc_idx = desc_idx;
                t.rx_count.fetch_add(1, std::memory_order_relaxed);
                dispatch(t, msg);
            }
            publish_used(t.rx, desc_idx, 0);  // return the consumed host buffer
        }

        // Re-enable host kicks before the last check: interrupt-driven RX
        // ([SPEC-E906-IRQ-04]) never wakes if NO_NOTIFY is left set. A buffer
        // posted between the check and the flag write is caught by re-checking.
        set_flags(t.rx, 0);
        if (!avail_pending(t, t.rx, pending) || pending == 0) {
            break;
        }
        set_flags(t.rx, VRING_USED_F_NO_NOTIFY);
    }

    // Tell the host its transmit buffers are back
    if (consumed) {
        notify(t);
    }
    return consumed;
}

// Bounded wait for a host buffer: kick and drain at most kReplyWaitRounds times,
// then return false so the caller drops or retries ([SPEC-E906-IPC-02]).
// @impl [SPEC-E906-IPC-02] targets/allwinner_e906/SPECIFICATION.md
bool reply(Transport &t, const RpmsgMessage &incoming, const void *payload, uint16_t len) {
    if (!ready(t) || len > RPMSG_BUFFER_SIZE - kHdr) {
        return false;
    }
    RpmsgTxBuffer buf{};
    for (uint32_t round = 0;; ++round) {
        buf = tx_take(t, 0);
        if (buf) {
            break;
        }
        if (round >= kReplyWaitRounds) {
            return false;
        }
        t.tx_full_waits.fetch_add(1, std::memory_order_relaxed);
        notify(t);
        drain_doorbells(t);
    }
    if (buf.payload.size() < len) {
        (void)tx_cancel(t, buf);
        t.ring_errors.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (payload && len > 0) {
        const auto *src_buf = static_cast<const uint8_t *>(payload);
        for (uint16_t i = 0; i < len; ++i) {
            buf.payload[i] = src_buf[i];
        }
    }
    return tx_publish(t, buf, len, incoming.dst, incoming.src);
}

void enable_rx_kick(Transport &t, bool enable) {
    if (ready(t) && t.rx.used) {
        set_flags(t.rx, enable ? 0 : VRING_USED_F_NO_NOTIFY);
    }
}

bool is_rx_pending(Transport &t) {
    if (!ready(t) || t.rx.avail == nullptr) return false;
    const uint16_t idx = t.rx.avail->idx;
    std::atomic_thread_fence(std::memory_order_acquire);
    return t.rx.last_avail_idx != idx;
}

} // namespace

#if !defined(__riscv)
void rpmsg_set_host_window(uintptr_t base, size_t size) noexcept {
    s_windows[0] = HostWindow{base, base, static_cast<uint32_t>(size)};
}
#endif

// ============================================================================
// Global Driver Pointer & Default Instance
// ============================================================================
static Rpmsg s_default_rpmsg_instance;
static IRpmsg *s_active_driver = &s_default_rpmsg_instance;

void Rpmsg::set_active_driver(IRpmsg *driver) noexcept {
    s_active_driver = driver ? driver : &s_default_rpmsg_instance;
}

IRpmsg* Rpmsg::get_active_driver() noexcept {
    return s_active_driver;
}

// ============================================================================
// 1. Rpmsg: Linux VirtIO Implementation
//    Ring roles follow the silicon-tested cubie-a5e firmware/common/hal/rpmsg.cpp:
//      vring[0]: remote -> host. Linux pre-posts empty buffers in `avail`;
//                the E906 fills one and returns it through `used`.
//      vring[1]: host -> remote. Linux posts messages in `avail`.
//    Kicks go to message box Channel1 (Linux channel 9, cubie-a5e D11).
//    The D-cache is off ([SPEC-E906-CACHE-02]): no cache maintenance.
// @impl [SPEC-E906-IPC-02] targets/allwinner_e906/SPECIFICATION.md
// ============================================================================
void Rpmsg::init(const struct rpmsg_resource_table *rsc) noexcept {
    reset(s_linux, rsc);
    s_doorbell_armed.store(false, std::memory_order_relaxed);
    clic::enable(clic::kLineMsgbox);  // [SPEC-E906-IRQ-04]; the read IRQ stays masked until armed
}

bool Rpmsg::is_driver_ready() noexcept { return ready(s_linux); }

bool Rpmsg::register_endpoint(uint32_t addr, EndpointCallback cb, void *user_data) noexcept {
    return register_ep(s_linux, addr, cb, user_data);
}

uint16_t Rpmsg::tx_free_buffers() noexcept { return tx_free(s_linux); }

// @impl [SPEC-E906-IPC-04] targets/allwinner_e906/SPECIFICATION.md
RpmsgTxBuffer Rpmsg::tx_acquire(uint16_t min_free_after) noexcept {
    return tx_take(s_linux, min_free_after);
}

bool Rpmsg::tx_cancel(const RpmsgTxBuffer &buf) noexcept { return hal::tx_cancel(s_linux, buf); }

bool Rpmsg::tx_commit(const RpmsgTxBuffer &buf, uint16_t used_len, uint32_t src, uint32_t dst) noexcept {
    return tx_publish(s_linux, buf, used_len, src, dst);
}

void Rpmsg::flush_kick() noexcept {
    if (s_linux.ready) {
        retry_owed_kick(s_linux);
    }
}

bool Rpmsg::announce_service(const char *name, uint32_t addr) noexcept {
    struct rpmsg_ns_msg {
        char name[32];
        uint32_t addr;
        uint32_t flags;
    } __attribute__((packed));

    RpmsgTxBuffer buf = tx_acquire();
    if (buf.payload.size() < sizeof(rpmsg_ns_msg)) {
        (void)tx_cancel(buf);  // never a 0-byte NS message ([SPEC-E906-IPC-04])
        return false;
    }
    auto *ns = reinterpret_cast<rpmsg_ns_msg *>(buf.payload.data());
    for (size_t i = 0; i < sizeof(ns->name); ++i) {
        ns->name[i] = 0;
    }
    for (size_t i = 0; i < sizeof(ns->name) - 1 && name[i]; ++i) {
        ns->name[i] = name[i];
    }
    ns->addr = addr;
    ns->flags = RPMSG_NS_CREATE;
    return tx_commit(buf, sizeof(rpmsg_ns_msg), addr, RPMSG_NS_ADDR);
}

void Rpmsg::enable_rx_kick(bool enable) noexcept { hal::enable_rx_kick(s_linux, enable); }
bool Rpmsg::poll() noexcept { return hal::poll(s_linux); }

bool Rpmsg::reply(const RpmsgMessage &incoming, const void *payload, uint16_t len) noexcept {
    return hal::reply(s_linux, incoming, payload, len);
}

uint32_t Rpmsg::get_rx_count() noexcept { return s_linux.rx_count.load(std::memory_order_relaxed); }
uint32_t Rpmsg::get_tx_count() noexcept { return s_linux.tx_count.load(std::memory_order_relaxed); }
bool Rpmsg::is_rx_pending() noexcept { return hal::is_rx_pending(s_linux); }
uint32_t Rpmsg::get_ring_errors() noexcept { return s_linux.ring_errors.load(std::memory_order_relaxed); }
uint32_t Rpmsg::get_tx_full_waits() noexcept { return s_linux.tx_full_waits.load(std::memory_order_relaxed); }
uint32_t Rpmsg::get_mbox_full_waits() noexcept { return s_linux.mbox_full_waits.load(std::memory_order_relaxed); }
uint32_t Rpmsg::get_host_kicks() noexcept { return s_linux.host_kicks.load(std::memory_order_relaxed); }

// ============================================================================
// 2. RpmsgLiteMetal: same virtio core, kicks on Channel0, no name service.
// @impl [SPEC-E906-IPC-03] targets/allwinner_e906/SPECIFICATION.md
// ============================================================================
void RpmsgLiteMetal::init(const struct rpmsg_resource_table *rsc) noexcept { reset(s_lite, rsc); }
bool RpmsgLiteMetal::is_driver_ready() noexcept { return ready(s_lite); }

bool RpmsgLiteMetal::register_endpoint(uint32_t addr, EndpointCallback cb, void *user_data) noexcept {
    return register_ep(s_lite, addr, cb, user_data);
}

// Peer-to-peer use has no Linux name service; there is nothing to announce.
bool RpmsgLiteMetal::announce_service(const char *name, uint32_t addr) noexcept {
    (void)name;
    (void)addr;
    return true;
}

void RpmsgLiteMetal::enable_rx_kick(bool enable) noexcept { hal::enable_rx_kick(s_lite, enable); }
bool RpmsgLiteMetal::poll() noexcept { return hal::poll(s_lite); }

bool RpmsgLiteMetal::reply(const RpmsgMessage &incoming, const void *payload, uint16_t len) noexcept {
    return hal::reply(s_lite, incoming, payload, len);
}

uint32_t RpmsgLiteMetal::get_rx_count() noexcept { return s_lite.rx_count.load(std::memory_order_relaxed); }
uint32_t RpmsgLiteMetal::get_tx_count() noexcept { return s_lite.tx_count.load(std::memory_order_relaxed); }
bool RpmsgLiteMetal::is_rx_pending() noexcept { return hal::is_rx_pending(s_lite); }
uint32_t RpmsgLiteMetal::get_ring_errors() noexcept { return s_lite.ring_errors.load(std::memory_order_relaxed); }
uint32_t RpmsgLiteMetal::get_tx_full_waits() noexcept { return s_lite.tx_full_waits.load(std::memory_order_relaxed); }
uint32_t RpmsgLiteMetal::get_mbox_full_waits() noexcept { return s_lite.mbox_full_waits.load(std::memory_order_relaxed); }
uint32_t RpmsgLiteMetal::get_host_kicks() noexcept { return s_lite.host_kicks.load(std::memory_order_relaxed); }

// ============================================================================
// 3. C++20 Coroutine Async Awaiter & Hardware MSGBOX ISR Binding
// ============================================================================
bool IRpmsg::AsyncRxAwaiter::await_ready() const noexcept {
    return driver && driver->is_rx_pending();
}

bool IRpmsg::AsyncRxAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept {
    // @impl [SPEC-E906-CORO-01] [SPEC-E906-IRQ-04] targets/allwinner_e906/SPECIFICATION.md
    void *expected = nullptr;
    if (!s_rpmsg_waiter.compare_exchange_strong(expected, handle.address(), std::memory_order_acq_rel)) {
        return false;  // one slot: never overwrite another waiter
    }
    registered = handle.address();
    if (driver) {
        driver->enable_rx_kick(true);
    }
    // Unmask the message box read interrupt (CLIC line 17) so a Linux kick resumes us
    rpmsg_arm_doorbell();
    return true;
}

bool IRpmsg::AsyncRxAwaiter::await_resume() noexcept {
    if (registered) {
        void *mine = registered;
        (void)s_rpmsg_waiter.compare_exchange_strong(mine, nullptr, std::memory_order_acq_rel);
        registered = nullptr;
    }
    // The doorbell flag belongs to platform_poll_network(); poll() reads the vrings.
    if (driver) {
        driver->enable_rx_kick(false);
    }
    return driver ? driver->poll() : false;  // irq-gated: resumed by the doorbell ISR or pending vring
}

} // namespace hal

// Hardware MSGBOX ISR (Overrides weak declaration in irq_dispatcher.cpp)
extern "C" __attribute__((section(".fastcode")))
void fc_msgbox_doorbell_isr() noexcept {
    // @impl [SPEC-E906-CORO-02] targets/allwinner_e906/SPECIFICATION.md
    // @impl [SPEC-E906-IRQ-04] targets/allwinner_e906/SPECIFICATION.md
    // 1. The line is level until the FIFO is read: mask it, clear, and leave the
    //    FIFO to the consumer, which re-arms after draining.
    hal::msgbox_rx_irq(false);
    hal::MsgBox::clear_irq_status(hal::MsgBox::Channel::Channel0);
    hal::MsgBox::clear_irq_status(hal::MsgBox::Channel::Channel1);
    // handshake: same hart; the waiter CAS-publishes then unmasks this level IRQ, so the ISR runs after it
    hal::s_doorbell_armed.store(false, std::memory_order_release);
    hal::s_doorbell_irqs.fetch_add(1, std::memory_order_relaxed);
    // handshake: as above (no load can pass the waiter's publish: the ISR starts after the unmask)
    hal::s_doorbell.store(true, std::memory_order_release);

    // Post the handle; abstractx::step() resumes it from the cooperative context.
    if (void *h = hal::s_rpmsg_waiter.exchange(nullptr, std::memory_order_acq_rel)) {
        abstractx::IsrDispatcher::post(std::coroutine_handle<>::from_address(h));
    }
}
