#include "hal/rpmsg.hpp"
#include "hal/msgbox.hpp"
#include "hal/pmp.hpp"
#include "abstractx/isr_dispatcher.hpp"
#include <string.h>
#include <span>

namespace hal {

static std::coroutine_handle<> s_rpmsg_coroutine_handle{nullptr};


/*
 * VirtIO Ring Internal Descriptors
 */
struct VirtioDesc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct VirtioAvail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VRING_NUM_DESCS];
} __attribute__((packed));

struct VirtioUsedElem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct VirtioUsed {
    uint16_t flags;
    uint16_t idx;
    struct VirtioUsedElem ring[VRING_NUM_DESCS];
} __attribute__((packed));

struct VirtQueueLayout {
    uint32_t da;
    uint32_t num;
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
    return static_cast<uintptr_t>(host_addr);
}

static void setup_vq(VirtQueueLayout &vq, uint32_t da, uint32_t num, uint32_t align) {
    vq.da = da;
    vq.num = num;
    vq.align = align;
    vq.last_avail_idx = 0;

    if (da == 0) {
        vq.desc = nullptr;
        vq.avail = nullptr;
        vq.used = nullptr;
        return;
    }

    uint8_t *base = reinterpret_cast<uint8_t *>(translate_da(da));
    vq.desc = reinterpret_cast<volatile struct VirtioDesc *>(base);

    size_t avail_offset = sizeof(struct VirtioDesc) * num;
    vq.avail = reinterpret_cast<volatile struct VirtioAvail *>(base + avail_offset);

    size_t used_offset = (avail_offset + sizeof(struct VirtioAvail) + align - 1) & ~(align - 1);
    vq.used = reinterpret_cast<volatile struct VirtioUsed *>(base + used_offset);
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
    const struct rpmsg_resource_table *s_linux_rsc = nullptr;
    bool s_linux_vdev_ready = false;

    std::atomic<uint32_t> s_linux_rx_count{0};
    std::atomic<uint32_t> s_linux_tx_count{0};

    struct EndpointSlot {
        uint32_t addr;
        EndpointCallback cb;
        void *user_data;
    } s_linux_endpoints[Rpmsg::MAX_ENDPOINTS];
    size_t s_num_linux_endpoints = 0;

    VirtQueueLayout s_tx_vq; // vring[0]: remote -> host
    VirtQueueLayout s_rx_vq; // vring[1]: host -> remote

    // Descriptor lent by tx_acquire() and not yet committed (at most one).
    int32_t s_tx_pending_desc = -1;

    volatile struct rpmsg_hdr *tx_header(uint16_t desc_idx) {
        return reinterpret_cast<volatile struct rpmsg_hdr *>(translate_da(s_tx_vq.desc[desc_idx].addr));
    }
}

void Rpmsg::init(const struct rpmsg_resource_table *rsc) noexcept {
    MsgBox::init();
    s_linux_rsc = rsc;
    s_linux_vdev_ready = false;
    s_tx_pending_desc = -1;
    s_linux_rx_count.store(0, std::memory_order_relaxed);
    s_linux_tx_count.store(0, std::memory_order_relaxed);
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
            setup_vq(s_tx_vq, rsc->vdev.vring[0].da, rsc->vdev.vring[0].num, rsc->vdev.vring[0].align);
            setup_vq(s_rx_vq, rsc->vdev.vring[1].da, rsc->vdev.vring[1].num, rsc->vdev.vring[1].align);
            s_linux_vdev_ready = true;
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

// Borrow the next free host buffer from vring[0]; its payload is written in
// place by the caller and sent by tx_commit(). No copy.
// @impl [SPEC-E906-IPC-04] targets/allwinner_e906/SPECIFICATION.md
std::span<uint8_t> Rpmsg::tx_acquire() noexcept {
    if (s_tx_pending_desc >= 0 || !is_driver_ready() || s_tx_vq.avail == nullptr) {
        return {};
    }
    std::atomic_thread_fence(std::memory_order_acquire);
    if (s_tx_vq.last_avail_idx == s_tx_vq.avail->idx) {
        return {}; // Linux has no free buffer posted
    }
    const uint16_t desc_idx = s_tx_vq.avail->ring[s_tx_vq.last_avail_idx % s_tx_vq.num];
    volatile struct VirtioDesc *desc = &s_tx_vq.desc[desc_idx];
    if (desc->addr == 0 || desc->len <= sizeof(struct rpmsg_hdr)) {
        return {};
    }
    s_tx_vq.last_avail_idx++;
    s_tx_pending_desc = desc_idx;
    uint8_t *payload = const_cast<uint8_t *>(tx_header(desc_idx)->data);
    return {payload, desc->len - sizeof(struct rpmsg_hdr)};
}

// Publish the buffer lent by tx_acquire(): header, release fence, used ring,
// release fence, used index, kick.
// @impl [SPEC-E906-IPC-04] targets/allwinner_e906/SPECIFICATION.md
bool Rpmsg::tx_commit(uint16_t used_len, uint32_t src, uint32_t dst) noexcept {
    if (s_tx_pending_desc < 0) {
        return false;
    }
    const uint16_t desc_idx = static_cast<uint16_t>(s_tx_pending_desc);
    s_tx_pending_desc = -1;

    volatile struct rpmsg_hdr *hdr = tx_header(desc_idx);
    hdr->src = src;
    hdr->dst = dst;
    hdr->len = used_len;
    hdr->flags = 0;
    hdr->reserved = 0;
    std::atomic_thread_fence(std::memory_order_release);

    const uint16_t used_slot = s_tx_vq.used->idx % s_tx_vq.num;
    s_tx_vq.used->ring[used_slot].id = desc_idx;
    s_tx_vq.used->ring[used_slot].len = sizeof(struct rpmsg_hdr) + used_len;
    std::atomic_thread_fence(std::memory_order_release);
    s_tx_vq.used->idx = s_tx_vq.used->idx + 1;

    MsgBox::send(MsgBox::Channel::Channel1, 0);
    s_linux_tx_count.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool Rpmsg::announce_service(const char *name, uint32_t addr) noexcept {
    struct rpmsg_ns_msg {
        char name[32];
        uint32_t addr;
        uint32_t flags;
    } __attribute__((packed));

    std::span<uint8_t> buf = tx_acquire();
    if (buf.size() < sizeof(rpmsg_ns_msg)) {
        if (!buf.empty()) {
            (void)tx_commit(0, addr, RPMSG_NS_ADDR);
        }
        return false;
    }
    auto *ns = reinterpret_cast<rpmsg_ns_msg *>(buf.data());
    for (size_t i = 0; i < sizeof(ns->name); ++i) {
        ns->name[i] = 0;
    }
    for (size_t i = 0; i < sizeof(ns->name) - 1 && name[i]; ++i) {
        ns->name[i] = name[i];
    }
    ns->addr = addr;
    ns->flags = RPMSG_NS_CREATE;
    return tx_commit(sizeof(rpmsg_ns_msg), addr, RPMSG_NS_ADDR);
}

bool Rpmsg::poll() noexcept {
    if (!is_driver_ready() || s_rx_vq.avail == nullptr) {
        return false;
    }

    if (MsgBox::is_rx_pending(MsgBox::Channel::Channel0)) {
        (void)MsgBox::receive(MsgBox::Channel::Channel0);
    }
    if (MsgBox::is_rx_pending(MsgBox::Channel::Channel1)) {
        (void)MsgBox::receive(MsgBox::Channel::Channel1);
    }

    std::atomic_thread_fence(std::memory_order_acquire);
    const uint16_t avail_idx = s_rx_vq.avail->idx;
    if (s_rx_vq.last_avail_idx == avail_idx) {
        return false;
    }

    bool processed = false;
    while (s_rx_vq.last_avail_idx != avail_idx) {
        const uint16_t desc_idx = s_rx_vq.avail->ring[s_rx_vq.last_avail_idx % s_rx_vq.num];
        volatile struct VirtioDesc *desc = &s_rx_vq.desc[desc_idx];

        if (desc->addr != 0 && desc->len >= sizeof(struct rpmsg_hdr)) {
            volatile struct rpmsg_hdr *hdr =
                reinterpret_cast<volatile struct rpmsg_hdr *>(translate_da(desc->addr));

            RpmsgMessage msg;
            msg.src = hdr->src;
            msg.dst = hdr->dst;
            msg.len = hdr->len;
            msg.data = const_cast<const uint8_t *>(hdr->data);
            msg.desc_idx = desc_idx;

            s_linux_rx_count.fetch_add(1, std::memory_order_relaxed);
            processed = true;

            for (size_t i = 0; i < s_num_linux_endpoints; ++i) {
                if (s_linux_endpoints[i].addr == msg.dst || s_linux_endpoints[i].addr == 0) {
                    if (s_linux_endpoints[i].cb) {
                        s_linux_endpoints[i].cb(msg, s_linux_endpoints[i].user_data);
                    }
                    break;
                }
            }

            // Return the consumed host buffer
            const uint16_t used_slot = s_rx_vq.used->idx % s_rx_vq.num;
            s_rx_vq.used->ring[used_slot].id = desc_idx;
            s_rx_vq.used->ring[used_slot].len = 0;
            std::atomic_thread_fence(std::memory_order_release);
            s_rx_vq.used->idx = s_rx_vq.used->idx + 1;
        }
        s_rx_vq.last_avail_idx++;
    }

    if (processed) {
        MsgBox::send(MsgBox::Channel::Channel1, 0); // host reclaims its buffers
    }
    return processed;
}

// Small control replies; bulk data (trace) uses tx_acquire()/tx_commit().
bool Rpmsg::reply(const RpmsgMessage &incoming, const void *payload, uint16_t len) noexcept {
    std::span<uint8_t> buf = tx_acquire();
    if (buf.empty()) {
        return false;
    }
    if (len > buf.size()) {
        len = static_cast<uint16_t>(buf.size());
    }
    const auto *src = static_cast<const uint8_t *>(payload);
    for (uint16_t i = 0; payload && i < len; ++i) {
        buf[i] = src[i];
    }
    return tx_commit(payload ? len : 0, incoming.dst, incoming.src);
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

// ============================================================================
// 2. RpmsgLiteMetal: Direct Bare-Metal Implementation (Zero Cache Operations)
// ============================================================================
namespace {
    const struct rpmsg_resource_table *s_lite_rsc = nullptr;
    bool s_lite_vdev_ready = false;
    bool s_lite_ns_announced = false;

    std::atomic<uint32_t> s_lite_rx_count{0};
    std::atomic<uint32_t> s_lite_tx_count{0};

    struct EndpointSlot s_lite_endpoints[RpmsgLiteMetal::MAX_ENDPOINTS];
    size_t s_num_lite_endpoints = 0;

    VirtQueueLayout s_lite_rx_vq;
    VirtQueueLayout s_lite_tx_vq;
}

void RpmsgLiteMetal::init(const struct rpmsg_resource_table *rsc) noexcept {
    MsgBox::init();
    s_lite_rsc = rsc;
    s_lite_vdev_ready = false;
    s_lite_ns_announced = false;
    s_lite_rx_count.store(0, std::memory_order_relaxed);
    s_lite_tx_count.store(0, std::memory_order_relaxed);
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
            setup_vq(s_lite_rx_vq, rsc->vdev.vring[0].da, rsc->vdev.vring[0].num, rsc->vdev.vring[0].align);
            setup_vq(s_lite_tx_vq, rsc->vdev.vring[1].da, rsc->vdev.vring[1].num, rsc->vdev.vring[1].align);
            s_lite_vdev_ready = true;
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

bool RpmsgLiteMetal::poll() noexcept {
    if (!is_driver_ready() || s_lite_rx_vq.avail == nullptr) {
        return false;
    }

    // Direct read with zero cache flushes (pure SRAM / uncached)
    std::atomic_thread_fence(std::memory_order_acquire);
    uint16_t avail_idx = s_lite_rx_vq.avail->idx;

    if (s_lite_rx_vq.last_avail_idx == avail_idx) {
        return false;
    }

    bool processed = false;
    while (s_lite_rx_vq.last_avail_idx != avail_idx) {
        uint16_t desc_idx = s_lite_rx_vq.avail->ring[s_lite_rx_vq.last_avail_idx % s_lite_rx_vq.num];
        volatile struct VirtioDesc *desc = &s_lite_rx_vq.desc[desc_idx];

        if (desc->addr != 0 && desc->len >= sizeof(struct rpmsg_hdr)) {
            volatile struct rpmsg_hdr *hdr = reinterpret_cast<volatile struct rpmsg_hdr *>(static_cast<uintptr_t>(desc->addr));

            RpmsgMessage msg;
            msg.src = hdr->src;
            msg.dst = hdr->dst;
            msg.len = hdr->len;
            msg.data = const_cast<const uint8_t *>(hdr->data);
            msg.desc_idx = desc_idx;

            s_lite_rx_count.fetch_add(1, std::memory_order_relaxed);
            processed = true;

            for (size_t i = 0; i < s_num_lite_endpoints; ++i) {
                if (s_lite_endpoints[i].addr == msg.dst || s_lite_endpoints[i].addr == 0) {
                    if (s_lite_endpoints[i].cb) {
                        s_lite_endpoints[i].cb(msg, s_lite_endpoints[i].user_data);
                    }
                    break;
                }
            }
        }

        s_lite_rx_vq.last_avail_idx++;
    }

    return processed;
}

bool RpmsgLiteMetal::reply(const RpmsgMessage &incoming, const void *payload, uint16_t len) noexcept {
    if (!s_lite_vdev_ready || s_lite_rx_vq.used == nullptr) {
        return false;
    }

    volatile struct VirtioDesc *desc = &s_lite_rx_vq.desc[incoming.desc_idx];
    if (desc->addr == 0) return false;

    volatile struct rpmsg_hdr *hdr = reinterpret_cast<volatile struct rpmsg_hdr *>(static_cast<uintptr_t>(desc->addr));

    uint32_t client_src = incoming.src;
    uint32_t local_dst  = incoming.dst;

    hdr->dst = client_src;
    hdr->src = local_dst;
    hdr->len = len;

    if (payload && len > 0) {
        uint8_t *dst_buf = reinterpret_cast<uint8_t *>(const_cast<uint8_t *>(hdr->data));
        const uint8_t *src_buf = reinterpret_cast<const uint8_t *>(payload);
        for (uint16_t i = 0; i < len; ++i) {
            dst_buf[i] = src_buf[i];
        }
    }

    std::atomic_thread_fence(std::memory_order_release);

    // Update Used Ring with zero cache flushes
    uint16_t used_idx = s_lite_rx_vq.used->idx;
    s_lite_rx_vq.used->ring[used_idx % s_lite_rx_vq.num].id = incoming.desc_idx;
    s_lite_rx_vq.used->ring[used_idx % s_lite_rx_vq.num].len = desc->len;

    std::atomic_thread_fence(std::memory_order_release);
    s_lite_rx_vq.used->idx = used_idx + 1;
    std::atomic_thread_fence(std::memory_order_seq_cst);

    // Kick MSGBOX Channel 0
    MsgBox::send(MsgBox::Channel::Channel0, 0);

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


// ============================================================================
// 3. C++20 Coroutine Async Awaiter & Hardware MSGBOX ISR Binding
// ============================================================================
bool IRpmsg::AsyncRxAwaiter::await_ready() const noexcept {
    return driver && driver->is_rx_pending();
}

void IRpmsg::AsyncRxAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept {
    // @impl [SPEC-E906-CORO-01] targets/allwinner_e906/SPECIFICATION.md
    s_rpmsg_coroutine_handle = handle;
    // Enable Channel 1 receive interrupt so Linux doorbell triggers PLIC IRQ
    MsgBox::enable_rx_irq(MsgBox::Channel::Channel1, true);
}

bool IRpmsg::AsyncRxAwaiter::await_resume() noexcept {
    s_rpmsg_coroutine_handle = nullptr;
    return driver ? driver->poll() : false;
}

} // namespace hal

// Hardware MSGBOX ISR (Overrides weak declaration in irq_dispatcher.cpp)
extern "C" __attribute__((section(".fastcode")))
void fc_msgbox_doorbell_isr() noexcept {
    // @impl [SPEC-E906-CORO-02] targets/allwinner_e906/SPECIFICATION.md
    // 1. Clear hardware interrupt status
    hal::MsgBox::clear_irq_status(hal::MsgBox::Channel::Channel1);

    // Post the handle; abstractx::step() resumes it from the cooperative context.
    if (hal::s_rpmsg_coroutine_handle) {
        auto h = hal::s_rpmsg_coroutine_handle;
        hal::s_rpmsg_coroutine_handle = nullptr;
        abstractx::IsrDispatcher::post(h);
    }
}
