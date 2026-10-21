#include "hal/rpmsg.hpp"
#include "hal/msgbox.hpp"
#include "hal/pmp.hpp"
#include "hal/clic.hpp"
#include "abstractx/isr_dispatcher.hpp"
#include <string.h>
#include <span>

namespace hal {

// @impl [SPEC-E906-CORO-01] targets/allwinner_e906/SPECIFICATION.md
static std::coroutine_handle<> s_rpmsg_coroutine_handle{nullptr};

// Message box doorbell state ([SPEC-E906-IRQ-04]); set by the ISR, taken by consumers.
static std::atomic<bool> s_doorbell{false};
static std::atomic<bool> s_doorbell_armed{false};
static std::atomic<uint32_t> s_doorbell_irqs{0};

static void msgbox_rx_irq(bool enable) noexcept {
    // Linux kicks on channels 0 and 1; both are read by drain_host_doorbells()
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
};

// Linux writes host physical addresses into the resource table and vring
// descriptors; the E906 sees the same SRAM at different addresses
// ([SPEC-E906-MEM-01]). Same mapping as the silicon-tested cubie-a5e
// firmware/common/hal/rpmsg.cpp. DDR addresses are identical on both sides.
// @impl [SPEC-E906-IPC-02] targets/allwinner_e906/SPECIFICATION.md
static inline uintptr_t translate_da(uint64_t host_addr) {
    if (host_addr >= 0x072C0000u && host_addr < 0x07300000u) {
        return static_cast<uintptr_t>(host_addr - 0x072C0000u + 0x40000000u); // Space 1
    }
    if (host_addr >= 0x07280000u && host_addr < 0x072C0000u) {
        return static_cast<uintptr_t>(host_addr - 0x07280000u + 0x3FFC0000u); // Space 0
    }
    if (host_addr >= 0x07200000u && host_addr < 0x07240000u) {
        return static_cast<uintptr_t>(host_addr - 0x07200000u + 0x3FFC0000u); // Space 0 on T527
    }
    return static_cast<uintptr_t>(host_addr);
}

static bool setup_vq(VirtQueueLayout &vq, uint32_t da, uint32_t num, uint32_t align) {
    vq.desc = nullptr;
    vq.avail = nullptr;
    vq.used = nullptr;
    vq.last_avail_idx = 0;

    if (da == 0 || da == 0xFFFFFFFFUL) {
        return false;
    }
    if (num == 0 || num > VRING_NUM_DESCS || (num & (num - 1)) != 0 ||
        align == 0 || (align & (align - 1)) != 0) {
        return false;
    }
    vq.da = da;
    vq.num = num;
    vq.mask = num - 1;
    vq.align = align;

    uint8_t *base = reinterpret_cast<uint8_t *>(translate_da(da));
    vq.desc = reinterpret_cast<volatile struct VirtioDesc *>(base);

    size_t avail_offset = sizeof(struct VirtioDesc) * num;
    vq.avail = reinterpret_cast<volatile struct VirtioAvail *>(base + avail_offset);

    size_t used_offset = (avail_offset + sizeof(uint16_t) * (3 + num) + align - 1) & ~static_cast<size_t>(align - 1);
    vq.used = reinterpret_cast<volatile struct VirtioUsed *>(base + used_offset);
    return true;
}

// ============================================================================
// Global Driver Pointer & Default Instance
// ============================================================================
static Rpmsg s_default_rpmsg_instance;
static IRpmsg *s_active_driver = &s_default_rpmsg_instance;

void Rpmsg::set_active_driver(IRpmsg *driver) noexcept {
    if (driver) {
        s_active_driver = driver;
    } else {
        s_active_driver = &s_default_rpmsg_instance;
    }
}

IRpmsg* Rpmsg::get_active_driver() noexcept {
    return s_active_driver;
}

// ============================================================================
// 1. Rpmsg: Linux VirtIO Implementation
//    Ring roles, address translation and the kick channel follow the
//    silicon-tested cubie-a5e firmware/common/hal/rpmsg.cpp:
//      vring[0]: remote -> host. Linux pre-posts empty buffers in `avail`;
//                the E906 fills one and returns it through `used`.
//      vring[1]: host -> remote. Linux posts messages in `avail`.
//    Kicks go to message box Channel1 (Linux channel 9, cubie-a5e D11).
//    The D-cache is off ([SPEC-E906-CACHE-02]): no cache maintenance.
// @impl [SPEC-E906-IPC-02] targets/allwinner_e906/SPECIFICATION.md
// ============================================================================
namespace {
    constexpr uint16_t VRING_USED_F_NO_NOTIFY = 1;

    const struct rpmsg_resource_table *s_linux_rsc = nullptr;
    bool s_linux_vdev_ready = false;

    std::atomic<uint32_t> s_linux_rx_count{0};
    std::atomic<uint32_t> s_linux_tx_count{0};
    std::atomic<uint32_t> s_linux_ring_errors{0};
    std::atomic<uint32_t> s_linux_tx_full_waits{0};
    std::atomic<uint32_t> s_linux_mbox_full_waits{0};
    std::atomic<uint32_t> s_linux_host_kicks{0};

    struct EndpointSlot {
        uint32_t addr;
        EndpointCallback cb;
        void *user_data;
    } s_linux_endpoints[Rpmsg::MAX_ENDPOINTS];
    size_t s_num_linux_endpoints = 0;

    VirtQueueLayout s_tx_vq; // vring[0]: remote -> host
    VirtQueueLayout s_rx_vq; // vring[1]: host -> remote

    volatile struct rpmsg_hdr *tx_header(uint16_t desc_idx) {
        return reinterpret_cast<volatile struct rpmsg_hdr *>(translate_da(s_tx_vq.desc[desc_idx].addr));
    }

    bool avail_pending(const VirtQueueLayout &vq, uint16_t &pending) {
        if (vq.avail == nullptr) {
            pending = 0;
            return false;
        }
        uint16_t avail_idx = vq.avail->idx;
        std::atomic_thread_fence(std::memory_order_acquire);
        pending = static_cast<uint16_t>(avail_idx - vq.last_avail_idx);
        if (pending > vq.num) {
            s_linux_ring_errors.fetch_add(1, std::memory_order_relaxed);
            pending = 0;
            return false;
        }
        return true;
    }

    void notify_host() {
        while (!MsgBox::send(MsgBox::Channel::Channel1, 0)) {
            s_linux_mbox_full_waits.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void drain_host_doorbells() {
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
            s_linux_host_kicks.fetch_add(n, std::memory_order_relaxed);
        }
    }
}

void Rpmsg::init(const struct rpmsg_resource_table *rsc) noexcept {
    MsgBox::init();
    s_doorbell_armed.store(false, std::memory_order_relaxed);
    clic::enable(clic::kLineMsgbox);  // [SPEC-E906-IRQ-04]; the read IRQ stays masked until armed
    s_linux_rsc = rsc;
    s_linux_vdev_ready = false;
    s_linux_rx_count.store(0, std::memory_order_relaxed);
    s_linux_tx_count.store(0, std::memory_order_relaxed);
    s_linux_ring_errors.store(0, std::memory_order_relaxed);
    s_linux_tx_full_waits.store(0, std::memory_order_relaxed);
    s_linux_mbox_full_waits.store(0, std::memory_order_relaxed);
    s_linux_host_kicks.store(0, std::memory_order_relaxed);
    s_num_linux_endpoints = 0;

    for (size_t i = 0; i < MAX_ENDPOINTS; ++i) {
        s_linux_endpoints[i].addr = 0;
        s_linux_endpoints[i].cb = nullptr;
        s_linux_endpoints[i].user_data = nullptr;
    }
}

bool Rpmsg::is_driver_ready() noexcept {
    if (!s_linux_rsc) return false;

    if (!s_linux_vdev_ready) {
        volatile const struct rpmsg_resource_table *rsc = s_linux_rsc;
        if ((rsc->vdev.status & VIRTIO_CONFIG_S_DRIVER_OK) &&
            rsc->vdev.vring[0].da != 0 && rsc->vdev.vring[1].da != 0) {
            bool tx_ok = setup_vq(s_tx_vq, rsc->vdev.vring[0].da, rsc->vdev.vring[0].num, rsc->vdev.vring[0].align);
            bool rx_ok = setup_vq(s_rx_vq, rsc->vdev.vring[1].da, rsc->vdev.vring[1].num, rsc->vdev.vring[1].align);
            if (tx_ok && rx_ok) {
                s_tx_vq.used->flags = VRING_USED_F_NO_NOTIFY;
                s_rx_vq.used->flags = VRING_USED_F_NO_NOTIFY;
                std::atomic_thread_fence(std::memory_order_release);
                s_linux_vdev_ready = true;
            } else {
                s_linux_ring_errors.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    return s_linux_vdev_ready;
}

bool Rpmsg::register_endpoint(uint32_t addr, EndpointCallback cb, void *user_data) noexcept {
    for (size_t i = 0; i < s_num_linux_endpoints; ++i) {
        if (s_linux_endpoints[i].addr == addr) {
            s_linux_endpoints[i].cb = cb;
            s_linux_endpoints[i].user_data = user_data;
            return true;
        }
    }
    if (s_num_linux_endpoints >= MAX_ENDPOINTS) return false;

    s_linux_endpoints[s_num_linux_endpoints].addr = addr;
    s_linux_endpoints[s_num_linux_endpoints].cb = cb;
    s_linux_endpoints[s_num_linux_endpoints].user_data = user_data;
    s_num_linux_endpoints++;
    return true;
}

uint16_t Rpmsg::tx_free_buffers() noexcept {
    if (!is_driver_ready() || s_tx_vq.avail == nullptr) {
        return 0;
    }
    uint16_t free_bufs = 0;
    if (!avail_pending(s_tx_vq, free_bufs)) {
        return 0;
    }
    if (free_bufs == 0) {
        // Free buffers exhausted: clear NO_NOTIFY so host kicks when buffers are returned
        s_tx_vq.used->flags = 0;
        std::atomic_thread_fence(std::memory_order_seq_cst);
    } else {
        s_tx_vq.used->flags = VRING_USED_F_NO_NOTIFY;
    }
    return free_bufs;
}

// Borrow the next free host buffer from vring[0]; its payload is written in
// place by the caller and sent by tx_commit(). No copy. Refuses if fewer than
// min_free_after buffers would remain (lets TLPs keep a reserve over trace).
// @impl [SPEC-E906-IPC-04] targets/allwinner_e906/SPECIFICATION.md
RpmsgTxBuffer Rpmsg::tx_acquire(uint16_t min_free_after) noexcept {
    if (tx_free_buffers() <= min_free_after) {
        return {};
    }
    const uint16_t desc_idx = s_tx_vq.avail->ring[s_tx_vq.last_avail_idx & s_tx_vq.mask];
    if (desc_idx >= s_tx_vq.num) {
        s_linux_ring_errors.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    volatile struct VirtioDesc *desc = &s_tx_vq.desc[desc_idx];
    if (desc->addr == 0 || desc->len <= sizeof(struct rpmsg_hdr)) {
        s_linux_ring_errors.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    s_tx_vq.last_avail_idx++;
    uint8_t *payload = const_cast<uint8_t *>(tx_header(desc_idx)->data);
    return {{payload, desc->len - sizeof(struct rpmsg_hdr)}, desc_idx};
}

// Publish a buffer lent by tx_acquire(): header, release fence, used ring,
// release fence, used index, kick. Completion may be out of order.
// @impl [SPEC-E906-IPC-04] targets/allwinner_e906/SPECIFICATION.md
bool Rpmsg::tx_commit(const RpmsgTxBuffer &buf, uint16_t used_len, uint32_t src, uint32_t dst) noexcept {
    if (!buf || used_len > buf.payload.size()) {
        return false;
    }
    volatile struct rpmsg_hdr *hdr = tx_header(buf.desc_idx);
    hdr->src = src;
    hdr->dst = dst;
    hdr->len = used_len;
    hdr->flags = 0;
    hdr->reserved = 0;
    std::atomic_thread_fence(std::memory_order_release);

    const uint16_t used_slot = s_tx_vq.used->idx & s_tx_vq.mask;
    s_tx_vq.used->ring[used_slot].id = buf.desc_idx;
    s_tx_vq.used->ring[used_slot].len = sizeof(struct rpmsg_hdr) + used_len;
    std::atomic_thread_fence(std::memory_order_release);
    s_tx_vq.used->idx = static_cast<uint16_t>(s_tx_vq.used->idx + 1);

    notify_host();
    s_linux_tx_count.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool Rpmsg::announce_service(const char *name, uint32_t addr) noexcept {
    struct rpmsg_ns_msg {
        char name[32];
        uint32_t addr;
        uint32_t flags;
    } __attribute__((packed));

    RpmsgTxBuffer buf = tx_acquire();
    if (buf.payload.size() < sizeof(rpmsg_ns_msg)) {
        if (buf) {
            (void)tx_commit(buf, 0, addr, RPMSG_NS_ADDR);
        }
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

void Rpmsg::enable_rx_kick(bool enable) noexcept {
    if (s_rx_vq.used) {
        s_rx_vq.used->flags = enable ? 0 : VRING_USED_F_NO_NOTIFY;
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }
}

bool Rpmsg::poll() noexcept {
    if (!is_driver_ready() || s_rx_vq.avail == nullptr) {
        return false;
    }

    drain_host_doorbells();

    uint16_t pending = 0;
    if (!avail_pending(s_rx_vq, pending)) {
        s_rx_vq.used->flags = 0;
        return false;
    }
    if (pending == 0) {
        return false;
    }

    // Suppress redundant host kicks while actively draining
    s_rx_vq.used->flags = VRING_USED_F_NO_NOTIFY;
    std::atomic_thread_fence(std::memory_order_seq_cst);

    for (;;) {
        for (; pending > 0; --pending) {
            const uint16_t desc_idx = s_rx_vq.avail->ring[s_rx_vq.last_avail_idx & s_rx_vq.mask];
            if (desc_idx >= s_rx_vq.num) {
                s_linux_ring_errors.fetch_add(1, std::memory_order_relaxed);
                s_rx_vq.used->flags = 0;
                return false;
            }
            volatile struct VirtioDesc *desc = &s_rx_vq.desc[desc_idx];
            uint32_t buf_len = desc->len;
            uint32_t buf_da = static_cast<uint32_t>(desc->addr);

            volatile struct rpmsg_hdr *hdr = nullptr;
            if (buf_da != 0 && buf_len >= sizeof(struct rpmsg_hdr)) {
                hdr = reinterpret_cast<volatile struct rpmsg_hdr *>(translate_da(buf_da));
                if (hdr->len > buf_len - sizeof(struct rpmsg_hdr)) {
                    hdr = nullptr;
                }
            }

            if (hdr == nullptr) {
                s_linux_ring_errors.fetch_add(1, std::memory_order_relaxed);
            } else {
                RpmsgMessage msg;
                msg.src = hdr->src;
                msg.dst = hdr->dst;
                msg.len = hdr->len;
                msg.data = const_cast<const uint8_t *>(hdr->data);
                msg.desc_idx = desc_idx;

                s_linux_rx_count.fetch_add(1, std::memory_order_relaxed);

                for (size_t i = 0; i < s_num_linux_endpoints; ++i) {
                    if (s_linux_endpoints[i].addr == msg.dst || s_linux_endpoints[i].addr == 0) {
                        if (s_linux_endpoints[i].cb) {
                            s_linux_endpoints[i].cb(msg, s_linux_endpoints[i].user_data);
                        }
                        break;
                    }
                }
            }

            // Return the consumed host buffer
            uint16_t used_idx = s_rx_vq.used->idx;
            s_rx_vq.used->ring[used_idx & s_rx_vq.mask].id = desc_idx;
            s_rx_vq.used->ring[used_idx & s_rx_vq.mask].len = 0;
            std::atomic_thread_fence(std::memory_order_release);
            s_rx_vq.used->idx = static_cast<uint16_t>(used_idx + 1);
            s_rx_vq.last_avail_idx++;
        }

        // Re-enable host kicks before the last check: interrupt-driven RX
        // ([SPEC-E906-IRQ-04]) never wakes if NO_NOTIFY is left set. A buffer
        // posted between the check and the flag write is caught by re-checking.
        s_rx_vq.used->flags = 0;
        std::atomic_thread_fence(std::memory_order_seq_cst);
        if (!avail_pending(s_rx_vq, pending) || pending == 0) {
            break;
        }
        s_rx_vq.used->flags = VRING_USED_F_NO_NOTIFY;
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    // Tell the host its transmit buffers are back
    notify_host();
    return true;
}

// Full flow control in reply(): if free host buffers are exhausted, wait,
// drain doorbells, and notify host rather than dropping the reply.
// @impl [SPEC-E906-IPC-02] targets/allwinner_e906/SPECIFICATION.md
bool Rpmsg::reply(const RpmsgMessage &incoming, const void *payload, uint16_t len) noexcept {
    if (!s_linux_vdev_ready || s_tx_vq.avail == nullptr) {
        return false;
    }
    if (len > RPMSG_BUFFER_SIZE - sizeof(struct rpmsg_hdr)) {
        return false;
    }

    uint16_t free_bufs = 0;
    for (;;) {
        if (!avail_pending(s_tx_vq, free_bufs)) {
            return false;
        }
        if (free_bufs > 0) {
            break;
        }
        // Free buffers exhausted: clear NO_NOTIFY so host kicks when buffers are returned
        s_tx_vq.used->flags = 0;
        std::atomic_thread_fence(std::memory_order_seq_cst);
        s_linux_tx_full_waits.fetch_add(1, std::memory_order_relaxed);
        notify_host();
        drain_host_doorbells();
    }
    s_tx_vq.used->flags = VRING_USED_F_NO_NOTIFY;

    uint16_t desc_idx = s_tx_vq.avail->ring[s_tx_vq.last_avail_idx & s_tx_vq.mask];
    if (desc_idx >= s_tx_vq.num) {
        s_linux_ring_errors.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    volatile struct VirtioDesc *desc = &s_tx_vq.desc[desc_idx];
    uint32_t buf_da = static_cast<uint32_t>(desc->addr);
    if (buf_da == 0 || desc->len < sizeof(struct rpmsg_hdr) + len) {
        s_linux_ring_errors.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    volatile struct rpmsg_hdr *hdr = reinterpret_cast<volatile struct rpmsg_hdr *>(translate_da(buf_da));

    hdr->dst = incoming.src;
    hdr->src = incoming.dst;
    hdr->len = len;
    hdr->flags = 0;
    hdr->reserved = 0;

    if (payload && len > 0) {
        uint8_t *dst_buf = reinterpret_cast<uint8_t *>(const_cast<uint8_t *>(hdr->data));
        const uint8_t *src_buf = reinterpret_cast<const uint8_t *>(payload);
        for (uint16_t i = 0; i < len; ++i) {
            dst_buf[i] = src_buf[i];
        }
    }

    s_tx_vq.last_avail_idx++;

    // Add to Used Ring for Linux Host to receive
    uint16_t used_idx = s_tx_vq.used->idx;
    s_tx_vq.used->ring[used_idx & s_tx_vq.mask].id = desc_idx;
    s_tx_vq.used->ring[used_idx & s_tx_vq.mask].len = sizeof(struct rpmsg_hdr) + len;
    std::atomic_thread_fence(std::memory_order_release);
    s_tx_vq.used->idx = static_cast<uint16_t>(used_idx + 1);

    notify_host();
    s_linux_tx_count.fetch_add(1, std::memory_order_relaxed);
    return true;
}

uint32_t Rpmsg::get_rx_count() noexcept {
    return s_linux_rx_count.load(std::memory_order_relaxed);
}

uint32_t Rpmsg::get_tx_count() noexcept {
    return s_linux_tx_count.load(std::memory_order_relaxed);
}

bool Rpmsg::is_rx_pending() noexcept {
    if (!is_driver_ready() || s_rx_vq.avail == nullptr) return false;
    std::atomic_thread_fence(std::memory_order_acquire);
    return (s_rx_vq.last_avail_idx != s_rx_vq.avail->idx);
}

uint32_t Rpmsg::get_ring_errors() noexcept {
    return s_linux_ring_errors.load(std::memory_order_relaxed);
}

uint32_t Rpmsg::get_tx_full_waits() noexcept {
    return s_linux_tx_full_waits.load(std::memory_order_relaxed);
}

uint32_t Rpmsg::get_mbox_full_waits() noexcept {
    return s_linux_mbox_full_waits.load(std::memory_order_relaxed);
}

uint32_t Rpmsg::get_host_kicks() noexcept {
    return s_linux_host_kicks.load(std::memory_order_relaxed);
}

// ============================================================================
// 2. RpmsgLiteMetal: Direct Bare-Metal Implementation (Zero Cache Operations)
//    Parity with Rpmsg: non-packed vrings, doorbell queue draining,
//    non-dropping FIFO kick, and avail_pending validation.
// @impl [SPEC-E906-IPC-03] targets/allwinner_e906/SPECIFICATION.md
// ============================================================================
namespace {
    const struct rpmsg_resource_table *s_lite_rsc = nullptr;
    bool s_lite_vdev_ready = false;
    bool s_lite_ns_announced = false;

    std::atomic<uint32_t> s_lite_rx_count{0};
    std::atomic<uint32_t> s_lite_tx_count{0};
    std::atomic<uint32_t> s_lite_ring_errors{0};
    std::atomic<uint32_t> s_lite_tx_full_waits{0};
    std::atomic<uint32_t> s_lite_mbox_full_waits{0};
    std::atomic<uint32_t> s_lite_host_kicks{0};

    struct EndpointSlot s_lite_endpoints[RpmsgLiteMetal::MAX_ENDPOINTS];
    size_t s_num_lite_endpoints = 0;

    VirtQueueLayout s_lite_rx_vq;
    VirtQueueLayout s_lite_tx_vq;

    void notify_lite_host() {
        while (!MsgBox::send(MsgBox::Channel::Channel0, 0)) {
            s_lite_mbox_full_waits.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void drain_lite_host_doorbells() {
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
            s_lite_host_kicks.fetch_add(n, std::memory_order_relaxed);
        }
    }
}

void RpmsgLiteMetal::init(const struct rpmsg_resource_table *rsc) noexcept {
    MsgBox::init();
    s_lite_rsc = rsc;
    s_lite_vdev_ready = false;
    s_lite_ns_announced = false;
    s_lite_rx_count.store(0, std::memory_order_relaxed);
    s_lite_tx_count.store(0, std::memory_order_relaxed);
    s_lite_ring_errors.store(0, std::memory_order_relaxed);
    s_lite_tx_full_waits.store(0, std::memory_order_relaxed);
    s_lite_mbox_full_waits.store(0, std::memory_order_relaxed);
    s_lite_host_kicks.store(0, std::memory_order_relaxed);
    s_num_lite_endpoints = 0;

    for (size_t i = 0; i < MAX_ENDPOINTS; ++i) {
        s_lite_endpoints[i].addr = 0;
        s_lite_endpoints[i].cb = nullptr;
        s_lite_endpoints[i].user_data = nullptr;
    }
}

bool RpmsgLiteMetal::is_driver_ready() noexcept {
    if (!s_lite_rsc) return false;

    if (!s_lite_vdev_ready) {
        volatile const struct rpmsg_resource_table *rsc = s_lite_rsc;
        uint8_t status = rsc->vdev.status;

        if ((status & VIRTIO_CONFIG_S_DRIVER_OK) &&
            rsc->vdev.vring[0].da != 0 && rsc->vdev.vring[1].da != 0) {
            bool tx_ok = setup_vq(s_lite_tx_vq, rsc->vdev.vring[0].da, rsc->vdev.vring[0].num, rsc->vdev.vring[0].align);
            bool rx_ok = setup_vq(s_lite_rx_vq, rsc->vdev.vring[1].da, rsc->vdev.vring[1].num, rsc->vdev.vring[1].align);
            if (tx_ok && rx_ok) {
                s_lite_tx_vq.used->flags = VRING_USED_F_NO_NOTIFY;
                s_lite_rx_vq.used->flags = VRING_USED_F_NO_NOTIFY;
                std::atomic_thread_fence(std::memory_order_release);
                s_lite_vdev_ready = true;
            } else {
                s_lite_ring_errors.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    return s_lite_vdev_ready;
}

bool RpmsgLiteMetal::register_endpoint(uint32_t addr, EndpointCallback cb, void *user_data) noexcept {
    if (s_num_lite_endpoints >= MAX_ENDPOINTS) return false;

    for (size_t i = 0; i < s_num_lite_endpoints; ++i) {
        if (s_lite_endpoints[i].addr == addr) {
            s_lite_endpoints[i].cb = cb;
            s_lite_endpoints[i].user_data = user_data;
            return true;
        }
    }

    s_lite_endpoints[s_num_lite_endpoints].addr = addr;
    s_lite_endpoints[s_num_lite_endpoints].cb = cb;
    s_lite_endpoints[s_num_lite_endpoints].user_data = user_data;
    s_num_lite_endpoints++;
    return true;
}

bool RpmsgLiteMetal::announce_service(const char *name, uint32_t addr) noexcept {
    (void)name;
    (void)addr;
    s_lite_ns_announced = true;
    return true;
}

void RpmsgLiteMetal::enable_rx_kick(bool enable) noexcept {
    if (s_lite_rx_vq.used) {
        s_lite_rx_vq.used->flags = enable ? 0 : VRING_USED_F_NO_NOTIFY;
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }
}

bool RpmsgLiteMetal::poll() noexcept {
    if (!is_driver_ready() || s_lite_rx_vq.avail == nullptr) {
        return false;
    }

    drain_lite_host_doorbells();

    uint16_t pending = 0;
    if (!avail_pending(s_lite_rx_vq, pending)) {
        s_lite_rx_vq.used->flags = 0;
        return false;
    }
    if (pending == 0) {
        return false;
    }

    s_lite_rx_vq.used->flags = VRING_USED_F_NO_NOTIFY;
    std::atomic_thread_fence(std::memory_order_seq_cst);

    for (;;) {
        for (; pending > 0; --pending) {
            uint16_t desc_idx = s_lite_rx_vq.avail->ring[s_lite_rx_vq.last_avail_idx & s_lite_rx_vq.mask];
            if (desc_idx >= s_lite_rx_vq.num) {
                s_lite_ring_errors.fetch_add(1, std::memory_order_relaxed);
                s_lite_rx_vq.used->flags = 0;
                return false;
            }
            volatile struct VirtioDesc *desc = &s_lite_rx_vq.desc[desc_idx];
            uint32_t buf_len = desc->len;
            uint32_t buf_da = static_cast<uint32_t>(desc->addr);

            volatile struct rpmsg_hdr *hdr = nullptr;
            if (buf_da != 0 && buf_len >= sizeof(struct rpmsg_hdr)) {
                hdr = reinterpret_cast<volatile struct rpmsg_hdr *>(translate_da(buf_da));
                if (hdr->len > buf_len - sizeof(struct rpmsg_hdr)) {
                    hdr = nullptr;
                }
            }

            if (hdr == nullptr) {
                s_lite_ring_errors.fetch_add(1, std::memory_order_relaxed);
            } else {
                RpmsgMessage msg;
                msg.src = hdr->src;
                msg.dst = hdr->dst;
                msg.len = hdr->len;
                msg.data = const_cast<const uint8_t *>(hdr->data);
                msg.desc_idx = desc_idx;

                s_lite_rx_count.fetch_add(1, std::memory_order_relaxed);

                for (size_t i = 0; i < s_num_lite_endpoints; ++i) {
                    if (s_lite_endpoints[i].addr == msg.dst || s_lite_endpoints[i].addr == 0) {
                        if (s_lite_endpoints[i].cb) {
                            s_lite_endpoints[i].cb(msg, s_lite_endpoints[i].user_data);
                        }
                        break;
                    }
                }
            }

            // Return the consumed host buffer
            uint16_t used_idx = s_lite_rx_vq.used->idx;
            s_lite_rx_vq.used->ring[used_idx & s_lite_rx_vq.mask].id = desc_idx;
            s_lite_rx_vq.used->ring[used_idx & s_lite_rx_vq.mask].len = 0;
            std::atomic_thread_fence(std::memory_order_release);
            s_lite_rx_vq.used->idx = static_cast<uint16_t>(used_idx + 1);
            s_lite_rx_vq.last_avail_idx++;
        }

        if (!avail_pending(s_lite_rx_vq, pending) || pending == 0) {
            break;
        }
    }

    notify_lite_host();
    return true;
}

bool RpmsgLiteMetal::reply(const RpmsgMessage &incoming, const void *payload, uint16_t len) noexcept {
    if (!s_lite_vdev_ready || s_lite_tx_vq.avail == nullptr) {
        return false;
    }
    if (len > RPMSG_BUFFER_SIZE - sizeof(struct rpmsg_hdr)) {
        return false;
    }

    uint16_t free_bufs = 0;
    for (;;) {
        if (!avail_pending(s_lite_tx_vq, free_bufs)) {
            return false;
        }
        if (free_bufs > 0) {
            break;
        }
        s_lite_tx_vq.used->flags = 0;
        std::atomic_thread_fence(std::memory_order_seq_cst);
        s_lite_tx_full_waits.fetch_add(1, std::memory_order_relaxed);
        notify_lite_host();
        drain_lite_host_doorbells();
    }
    s_lite_tx_vq.used->flags = VRING_USED_F_NO_NOTIFY;

    uint16_t desc_idx = s_lite_tx_vq.avail->ring[s_lite_tx_vq.last_avail_idx & s_lite_tx_vq.mask];
    if (desc_idx >= s_lite_tx_vq.num) {
        s_lite_ring_errors.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    volatile struct VirtioDesc *desc = &s_lite_tx_vq.desc[desc_idx];
    uint32_t buf_da = static_cast<uint32_t>(desc->addr);
    if (buf_da == 0 || desc->len < sizeof(struct rpmsg_hdr) + len) {
        s_lite_ring_errors.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    volatile struct rpmsg_hdr *hdr = reinterpret_cast<volatile struct rpmsg_hdr *>(translate_da(buf_da));

    hdr->dst = incoming.src;
    hdr->src = incoming.dst;
    hdr->len = len;
    hdr->flags = 0;
    hdr->reserved = 0;

    if (payload && len > 0) {
        uint8_t *dst_buf = reinterpret_cast<uint8_t *>(const_cast<uint8_t *>(hdr->data));
        const uint8_t *src_buf = reinterpret_cast<const uint8_t *>(payload);
        for (uint16_t i = 0; i < len; ++i) {
            dst_buf[i] = src_buf[i];
        }
    }

    s_lite_tx_vq.last_avail_idx++;

    uint16_t used_idx = s_lite_tx_vq.used->idx;
    s_lite_tx_vq.used->ring[used_idx & s_lite_tx_vq.mask].id = desc_idx;
    s_lite_tx_vq.used->ring[used_idx & s_lite_tx_vq.mask].len = sizeof(struct rpmsg_hdr) + len;
    std::atomic_thread_fence(std::memory_order_release);
    s_lite_tx_vq.used->idx = static_cast<uint16_t>(used_idx + 1);

    notify_lite_host();
    s_lite_tx_count.fetch_add(1, std::memory_order_relaxed);
    return true;
}

uint32_t RpmsgLiteMetal::get_rx_count() noexcept {
    return s_lite_rx_count.load(std::memory_order_relaxed);
}

uint32_t RpmsgLiteMetal::get_tx_count() noexcept {
    return s_lite_tx_count.load(std::memory_order_relaxed);
}

bool RpmsgLiteMetal::is_rx_pending() noexcept {
    if (!is_driver_ready() || s_lite_rx_vq.avail == nullptr) return false;
    std::atomic_thread_fence(std::memory_order_acquire);
    return (s_lite_rx_vq.last_avail_idx != s_lite_rx_vq.avail->idx);
}

uint32_t RpmsgLiteMetal::get_ring_errors() noexcept {
    return s_lite_ring_errors.load(std::memory_order_relaxed);
}

uint32_t RpmsgLiteMetal::get_tx_full_waits() noexcept {
    return s_lite_tx_full_waits.load(std::memory_order_relaxed);
}

uint32_t RpmsgLiteMetal::get_mbox_full_waits() noexcept {
    return s_lite_mbox_full_waits.load(std::memory_order_relaxed);
}

uint32_t RpmsgLiteMetal::get_host_kicks() noexcept {
    return s_lite_host_kicks.load(std::memory_order_relaxed);
}

// ============================================================================
// 3. C++20 Coroutine Async Awaiter & Hardware MSGBOX ISR Binding
// ============================================================================
bool IRpmsg::AsyncRxAwaiter::await_ready() const noexcept {
    return driver && driver->is_rx_pending();
}

void IRpmsg::AsyncRxAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept {
    // @impl [SPEC-E906-CORO-01] [SPEC-E906-IRQ-04] targets/allwinner_e906/SPECIFICATION.md
    s_rpmsg_coroutine_handle = handle;
    if (driver) {
        driver->enable_rx_kick(true);
    }
    // Unmask the message box read interrupt (CLIC line 17) so a Linux kick resumes us
    rpmsg_arm_doorbell();
}

bool IRpmsg::AsyncRxAwaiter::await_resume() noexcept {
    s_rpmsg_coroutine_handle = nullptr;
    (void)rpmsg_take_doorbell();
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
    hal::s_doorbell_armed.store(false, std::memory_order_release);
    hal::s_doorbell_irqs.fetch_add(1, std::memory_order_relaxed);
    hal::s_doorbell.store(true, std::memory_order_release);

    // Post the handle; abstractx::step() resumes it from the cooperative context.
    if (hal::s_rpmsg_coroutine_handle) {
        auto h = hal::s_rpmsg_coroutine_handle;
        hal::s_rpmsg_coroutine_handle = nullptr;
        abstractx::IsrDispatcher::post(h);
    }
}
