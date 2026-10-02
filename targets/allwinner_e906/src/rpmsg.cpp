#include "hal/rpmsg.hpp"
#include "hal/msgbox.hpp"
#include "hal/pmp.hpp"
#include <string.h>

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

    uint8_t *base = reinterpret_cast<uint8_t *>(da);
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
// 1. Rpmsg: Linux VirtIO Implementation (With D-Cache Maintenance)
// ============================================================================
namespace {
    const struct rpmsg_resource_table *s_linux_rsc = nullptr;
    bool s_linux_vdev_ready = false;
    bool s_linux_ns_announced = false;

    std::atomic<uint32_t> s_linux_rx_count{0};
    std::atomic<uint32_t> s_linux_tx_count{0};

    RpmsgMessage s_endpoints[Rpmsg::MAX_ENDPOINTS];
    struct EndpointSlot {
        uint32_t addr;
        EndpointCallback cb;
        void *user_data;
    } s_linux_endpoints[Rpmsg::MAX_ENDPOINTS];
    size_t s_num_linux_endpoints = 0;

    VirtQueueLayout s_linux_rx_vq; // vring0: Host -> Remote (RX)
    VirtQueueLayout s_linux_tx_vq; // vring1: Remote -> Host (TX)
}

void Rpmsg::init(const struct rpmsg_resource_table *rsc) noexcept {
    MsgBox::init();
    s_linux_rsc = rsc;
    s_linux_vdev_ready = false;
    s_linux_ns_announced = false;
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
        uint8_t status = rsc->vdev.status;

        if ((status & VIRTIO_CONFIG_S_DRIVER_OK) &&
            rsc->vdev.vring[0].da != 0 && rsc->vdev.vring[1].da != 0) {
            setup_vq(s_linux_rx_vq, rsc->vdev.vring[0].da, rsc->vdev.vring[0].num, rsc->vdev.vring[0].align);
            setup_vq(s_linux_tx_vq, rsc->vdev.vring[1].da, rsc->vdev.vring[1].num, rsc->vdev.vring[1].align);
            s_linux_vdev_ready = true;
        }
    }
    return s_linux_vdev_ready;
}

bool Rpmsg::register_endpoint(uint32_t addr, EndpointCallback cb, void *user_data) noexcept {
    if (s_num_linux_endpoints >= MAX_ENDPOINTS) return false;

    for (size_t i = 0; i < s_num_linux_endpoints; ++i) {
        if (s_linux_endpoints[i].addr == addr) {
            s_linux_endpoints[i].cb = cb;
            s_linux_endpoints[i].user_data = user_data;
            return true;
        }
    }

    s_linux_endpoints[s_num_linux_endpoints].addr = addr;
    s_linux_endpoints[s_num_linux_endpoints].cb = cb;
    s_linux_endpoints[s_num_linux_endpoints].user_data = user_data;
    s_num_linux_endpoints++;
    return true;
}

bool Rpmsg::announce_service(const char *name, uint32_t addr) noexcept {
    (void)name;
    (void)addr;
    s_linux_ns_announced = true;
    return true;
}

bool Rpmsg::poll() noexcept {
    if (!is_driver_ready() || s_linux_rx_vq.avail == nullptr) {
        return false;
    }

    // Invalidate D-Cache for the available ring to read fresh indices from Linux host
    Pmp::dcache_invalidate_range(reinterpret_cast<uintptr_t>(const_cast<struct VirtioAvail *>(s_linux_rx_vq.avail)), sizeof(struct VirtioAvail));
    std::atomic_thread_fence(std::memory_order_acquire);
    uint16_t avail_idx = s_linux_rx_vq.avail->idx;

    if (s_linux_rx_vq.last_avail_idx == avail_idx) {
        return false;
    }

    bool processed = false;
    while (s_linux_rx_vq.last_avail_idx != avail_idx) {
        uint16_t desc_idx = s_linux_rx_vq.avail->ring[s_linux_rx_vq.last_avail_idx % s_linux_rx_vq.num];
        volatile struct VirtioDesc *desc = &s_linux_rx_vq.desc[desc_idx];
        Pmp::dcache_invalidate_range(reinterpret_cast<uintptr_t>(const_cast<struct VirtioDesc *>(desc)), sizeof(struct VirtioDesc));

        if (desc->addr != 0 && desc->len >= sizeof(struct rpmsg_hdr)) {
            // Invalidate D-Cache for incoming packet buffer so CPU reads DRAM written by host
            Pmp::dcache_invalidate_range(static_cast<uintptr_t>(desc->addr), desc->len);
            volatile struct rpmsg_hdr *hdr = reinterpret_cast<volatile struct rpmsg_hdr *>(static_cast<uintptr_t>(desc->addr));

            RpmsgMessage msg;
            msg.src = hdr->src;
            msg.dst = hdr->dst;
            msg.len = hdr->len;
            msg.data = const_cast<const uint8_t *>(hdr->data);
            msg.desc_idx = desc_idx;

            s_linux_rx_count.fetch_add(1, std::memory_order_relaxed);
            processed = true;

            // Dispatch to registered endpoint callback
            for (size_t i = 0; i < s_num_linux_endpoints; ++i) {
                if (s_linux_endpoints[i].addr == msg.dst || s_linux_endpoints[i].addr == 0) {
                    if (s_linux_endpoints[i].cb) {
                        s_linux_endpoints[i].cb(msg, s_linux_endpoints[i].user_data);
                    }
                    break;
                }
            }
        }

        s_linux_rx_vq.last_avail_idx++;
    }

    return processed;
}

bool Rpmsg::reply(const RpmsgMessage &incoming, const void *payload, uint16_t len) noexcept {
    if (!s_linux_vdev_ready || s_linux_rx_vq.used == nullptr) {
        return false;
    }

    volatile struct VirtioDesc *desc = &s_linux_rx_vq.desc[incoming.desc_idx];
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

    // Clean (write-back) modified response buffer to DDR memory before signaling host
    Pmp::dcache_clean_range(static_cast<uintptr_t>(desc->addr), sizeof(struct rpmsg_hdr) + len);

    std::atomic_thread_fence(std::memory_order_release);

    // Update Used Ring
    uint16_t used_idx = s_linux_rx_vq.used->idx;
    s_linux_rx_vq.used->ring[used_idx % s_linux_rx_vq.num].id = incoming.desc_idx;
    s_linux_rx_vq.used->ring[used_idx % s_linux_rx_vq.num].len = desc->len;

    std::atomic_thread_fence(std::memory_order_release);
    s_linux_rx_vq.used->idx = used_idx + 1;
    std::atomic_thread_fence(std::memory_order_seq_cst);

    // Clean (write-back) modified Used Ring to DDR memory before ringing doorbell
    Pmp::dcache_clean_range(reinterpret_cast<uintptr_t>(const_cast<struct VirtioUsed *>(s_linux_rx_vq.used)), sizeof(struct VirtioUsed));

    // Kick Linux host on MSGBOX Channel 0 (doorbell interrupt)
    MsgBox::send(MsgBox::Channel::Channel0, 0);

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
    if (!is_driver_ready() || s_linux_rx_vq.avail == nullptr) return false;
    Pmp::dcache_invalidate_range(reinterpret_cast<uintptr_t>(const_cast<struct VirtioAvail *>(s_linux_rx_vq.avail)), sizeof(struct VirtioAvail));
    std::atomic_thread_fence(std::memory_order_acquire);
    return (s_linux_rx_vq.last_avail_idx != s_linux_rx_vq.avail->idx);
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
    // 1. Clear hardware interrupt status
    hal::MsgBox::clear_irq_status(hal::MsgBox::Channel::Channel1);

    // 2. Resume waiting coroutine directly (< 25 ns wakeup latency)
    if (hal::s_rpmsg_coroutine_handle && !hal::s_rpmsg_coroutine_handle.done()) {
        auto h = hal::s_rpmsg_coroutine_handle;
        hal::s_rpmsg_coroutine_handle = nullptr;
        h.resume();
    }
}
