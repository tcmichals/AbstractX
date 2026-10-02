#include "pmp.hpp"

namespace hal {

void Pmp::init() noexcept {
#if defined(__riscv)
    // 1. Initial barrier
    memory_fence();

    // 2. Default: Enable all physical memory access for Machine mode
    // Entry 0: Map all 4GB space as RWX using NAPOT
    uint32_t pmpaddr_all = 0x3FFFFFFF; // Covers 0x00000000 to 0xFFFFFFFF
    asm volatile ("csrw pmpaddr0, %0" :: "r"(pmpaddr_all));

    // pmpcfg0: Entry 0 = NAPOT (0x18) | RWX (0x07) = 0x1F
    uint32_t pmpcfg = (PmpFlags::ModeNapot | PmpFlags::RWX);
    asm volatile ("csrw pmpcfg0, %0" :: "r"(pmpcfg));

    instruction_fence();
    memory_fence();
#endif
}

void Pmp::set_tor_entry(uint32_t entry_idx, uintptr_t base_addr, uintptr_t end_addr, uint8_t flags) noexcept {
#if defined(__riscv)
    if (entry_idx == 1) {
        uint32_t addr0 = static_cast<uint32_t>(base_addr >> 2);
        uint32_t addr1 = static_cast<uint32_t>(end_addr >> 2);
        asm volatile ("csrw pmpaddr0, %0" :: "r"(addr0));
        asm volatile ("csrw pmpaddr1, %0" :: "r"(addr1));

        uint32_t cfg = (PmpFlags::ModeTor | (flags & PmpFlags::RWX)) << 8;
        asm volatile ("csrw pmpcfg0, %0" :: "r"(cfg));
    }
    instruction_fence();
    memory_fence();
#else
    (void)entry_idx; (void)base_addr; (void)end_addr; (void)flags;
#endif
}

void Pmp::set_napot_entry(uint32_t entry_idx, uintptr_t base_addr, size_t size, uint8_t flags) noexcept {
#if defined(__riscv)
    if (size >= 8 && (size & (size - 1)) == 0) {
        uintptr_t napot_addr = (base_addr >> 2) | ((size >> 3) - 1);
        if (entry_idx == 0) {
            asm volatile ("csrw pmpaddr0, %0" :: "r"(napot_addr));
            uint32_t cfg = (PmpFlags::ModeNapot | (flags & PmpFlags::RWX));
            asm volatile ("csrw pmpcfg0, %0" :: "r"(cfg));
        } else if (entry_idx == 1) {
            asm volatile ("csrw pmpaddr1, %0" :: "r"(napot_addr));
            uint32_t cfg;
            asm volatile ("csrr %0, pmpcfg0" : "=r"(cfg));
            cfg = (cfg & 0x00FF) | ((PmpFlags::ModeNapot | (flags & PmpFlags::RWX)) << 8);
            asm volatile ("csrw pmpcfg0, %0" :: "r"(cfg));
        }
    }
    instruction_fence();
    memory_fence();
#else
    (void)entry_idx; (void)base_addr; (void)size; (void)flags;
#endif
}

void Pmp::configure_dram_carveout(uintptr_t dram_base, size_t dram_size) noexcept {
#if defined(__riscv)
    // Setup PMP Read/Write permissions for the DDR carveout
    set_napot_entry(1, dram_base, dram_size, PmpFlags::Read | PmpFlags::Write);

    // Initial flush of any stale cache lines for the DRAM range
    dcache_invalidate_range(dram_base, dram_size);
    memory_fence();
#else
    (void)dram_base; (void)dram_size;
#endif
}

void Pmp::dcache_clean_range(uintptr_t addr, size_t len) noexcept {
#if defined(__riscv)
    // Verified Allwinner Tina implementation (32-byte cache line, dcache.cpa a5)
    register uintptr_t i asm("a5") = addr & ~0x1FUL;
    uintptr_t end = addr + len;
    for (; i < end; i += 32) {
        asm volatile(".word 0x0297800b" ::: "memory"); // dcache.cpa a5
    }
    asm volatile(".word 0x0000000f" ::: "memory");     // sync fence
#else
    (void)addr; (void)len;
#endif
}

void Pmp::dcache_invalidate_range(uintptr_t addr, size_t len) noexcept {
#if defined(__riscv)
    // Verified Allwinner Tina implementation (32-byte cache line, dcache.iva a5)
    register uintptr_t i asm("a5") = addr & ~0x1FUL;
    uintptr_t end = addr + len;
    for (; i < end; i += 32) {
        asm volatile(".word 0x02a7800b" ::: "memory"); // dcache.iva a5
    }
    asm volatile(".word 0x0000000f" ::: "memory");     // sync fence
#else
    (void)addr; (void)len;
#endif
}

void Pmp::dcache_clean_invalidate_range(uintptr_t addr, size_t len) noexcept {
#if defined(__riscv)
    // Verified Allwinner Tina implementation (32-byte cache line, dcache.civa a5)
    register uintptr_t i asm("a5") = addr & ~0x1FUL;
    uintptr_t end = addr + len;
    for (; i < end; i += 32) {
        asm volatile(".word 0x02b7800b" ::: "memory"); // dcache.civa a5
    }
    asm volatile(".word 0x0000000f" ::: "memory");     // sync fence
#else
    (void)addr; (void)len;
#endif
}

void Pmp::dcache_flush_all() noexcept {
#if defined(__riscv)
    // Allwinner Tina: dcache.ciall
    asm volatile(".word 0x0030000b" ::: "memory");
    asm volatile(".word 0x0000000f" ::: "memory");
#endif
}

void Pmp::icache_invalidate_all() noexcept {
#if defined(__riscv)
    // Allwinner Tina: icache.iall
    asm volatile(".word 0x0100000b" ::: "memory");
    instruction_fence();
#endif
}

} // namespace hal
