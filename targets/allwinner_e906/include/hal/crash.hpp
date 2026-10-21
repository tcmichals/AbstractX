#ifndef HAL_CRASH_HPP
#define HAL_CRASH_HPP

#include <stdint.h>
#include <stddef.h>

namespace hal {

struct CrashFrame {
    // General Purpose Registers x1..x31
    uint32_t ra;   // x1
    uint32_t sp;   // x2
    uint32_t gp;   // x3
    uint32_t tp;   // x4
    uint32_t t0;   // x5
    uint32_t t1;   // x6
    uint32_t t2;   // x7
    uint32_t s0;   // x8 / fp
    uint32_t s1;   // x9
    uint32_t a0;   // x10
    uint32_t a1;   // x11
    uint32_t a2;   // x12
    uint32_t a3;   // x13
    uint32_t a4;   // x14
    uint32_t a5;   // x15
    uint32_t a6;   // x16
    uint32_t a7;   // x17
    uint32_t s2;   // x18
    uint32_t s3;   // x19
    uint32_t s4;   // x20
    uint32_t s5;   // x21
    uint32_t s6;   // x22
    uint32_t s7;   // x23
    uint32_t s8;   // x24
    uint32_t s9;   // x25
    uint32_t s10;  // x26
    uint32_t s11;  // x27
    uint32_t t3;   // x28
    uint32_t t4;   // x29
    uint32_t t5;   // x30
    uint32_t t6;   // x31

    // CSRs
    uint32_t mepc;
    uint32_t mcause;
    uint32_t mtval;
    uint32_t mstatus;

    // Floating Point Registers f0..f31 (RV32IMAFDC 64-bit double precision, ilp32d)
    // @impl [SPEC-E906-CRASH-01] targets/allwinner_e906/SPECIFICATION.md
    uint32_t _reserved; // offset 140 (padding to 8-byte boundary for f0)
    uint64_t f[32];     // offsets 144..399; single-precision f-regs in the low word (no D on the E906)
    uint32_t fcsr;      // offset 400
    uint32_t _pad[3];   // offsets 404..415 (padding to 16-byte stack frame alignment)
};

static_assert(sizeof(CrashFrame) == 416, "CrashFrame size must match trap stack frame (416 bytes)");
static_assert(__builtin_offsetof(CrashFrame, mcause) == 128, "mcause offset must be 128 for e906-disassembly-check");
static_assert(__builtin_offsetof(CrashFrame, f) == 144, "FP registers must be at offset 144 (8-byte aligned)");
static_assert(__builtin_offsetof(CrashFrame, fcsr) == 400, "fcsr must be at offset 400");

class CrashHandler {
public:
    // Human-readable cause name translation
    static const char *get_cause_name(uint32_t mcause) noexcept;

    // Format and dump the complete crash report to hal::Trace and SRAM
    static void handle(const CrashFrame &frame) noexcept;
};

} // namespace hal

#ifdef __cplusplus
extern "C" {
#endif

void hal_crash_dispatcher(const hal::CrashFrame *frame);

#ifdef __cplusplus
}
#endif

#endif // HAL_CRASH_HPP
