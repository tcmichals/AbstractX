/*
 * startup.S - XuanTie E906 (RV32IMAFC, no D) Unified Startup & Exception Vector Table
 */

    .section .vectors, "ax", @progbits
    /* CLIC mode needs a 64-byte aligned mtvec base ([SPEC-E906-IRQ-01]) */
    .balign 64
    .global default_trap_entry
    .type default_trap_entry, @function

/*
 * =====================================================================
 * Trap Entry Point (CLIC non-vectored mode: every trap lands here)
 * =====================================================================
 */
default_trap_entry:
    /* CrashFrame layout (416 B, include/hal/crash.hpp). Classify first: interrupts take the
     * fast integer-only path, exceptions build the full frame.
     * @impl [SPEC-E906-CRASH-01] targets/allwinner_e906/SPECIFICATION.md */
    addi sp, sp, -416
    sw t0,  16(sp)
    sw t1,  20(sp)
    csrr t0, mcause
    sw t0, 128(sp)
    bltz t0, .Lirq_entry          /* mcause bit 31 = interrupt (mstatus bit 31 is SD) */

    /* ---- Synchronous exception: full frame, then the crash dump (never returns) ---- */
    sw ra, 0(sp)
    sw gp, 8(sp)
    sw tp, 12(sp)
    sw t2, 24(sp)
    sw s0, 28(sp)
    sw s1, 32(sp)
    sw a0, 36(sp)
    sw a1, 40(sp)
    sw a2, 44(sp)
    sw a3, 48(sp)
    sw a4, 52(sp)
    sw a5, 56(sp)
    sw a6, 60(sp)
    sw a7, 64(sp)
    sw s2, 68(sp)
    sw s3, 72(sp)
    sw s4, 76(sp)
    sw s5, 80(sp)
    sw s6, 84(sp)
    sw s7, 88(sp)
    sw s8, 92(sp)
    sw s9, 96(sp)
    sw s10, 100(sp)
    sw s11, 104(sp)
    sw t3, 108(sp)
    sw t4, 112(sp)
    sw t5, 116(sp)
    sw t6, 120(sp)
    addi t1, sp, 416              /* sp before the trap */
    sw t1,   4(sp)
    csrr t1, mepc
    sw t1, 124(sp)
    la t1, g_e906_diag            /* trap count [11], mcause [12], mepc [13] for devmem */
    lw t2, 44(t1)
    addi t2, t2, 1
    sw t2, 44(t1)
    sw t0, 48(t1)
    lw t2, 124(sp)
    sw t2, 52(t1)
    csrr t1, mtval
    sw t1, 132(sp)
    csrr t1, mstatus
    sw t1, 136(sp)                /* as at the trap: FS = Off if it hit interrupt context */
    li t1, (1 << 13)
    csrs mstatus, t1              /* FS on, so the FP registers can be dumped */
    fsw f0, 144(sp)
    fsw f1, 152(sp)
    fsw f2, 160(sp)
    fsw f3, 168(sp)
    fsw f4, 176(sp)
    fsw f5, 184(sp)
    fsw f6, 192(sp)
    fsw f7, 200(sp)
    fsw f8, 208(sp)
    fsw f9, 216(sp)
    fsw f10, 224(sp)
    fsw f11, 232(sp)
    fsw f12, 240(sp)
    fsw f13, 248(sp)
    fsw f14, 256(sp)
    fsw f15, 264(sp)
    fsw f16, 272(sp)
    fsw f17, 280(sp)
    fsw f18, 288(sp)
    fsw f19, 296(sp)
    fsw f20, 304(sp)
    fsw f21, 312(sp)
    fsw f22, 320(sp)
    fsw f23, 328(sp)
    fsw f24, 336(sp)
    fsw f25, 344(sp)
    fsw f26, 352(sp)
    fsw f27, 360(sp)
    fsw f28, 368(sp)
    fsw f29, 376(sp)
    fsw f30, 384(sp)
    fsw f31, 392(sp)
    frcsr t1
    sw t1, 400(sp)
    mv a0, sp
    call hal_crash_dispatcher
.Ltrap_hang:
    wfi
    j .Ltrap_hang

    /* ---- Interrupt: save only what a C handler may clobber (s0..s11, gp, tp are
     * callee-saved). No FP save: handlers are integer-only and run with FS = Off, so an
     * FP instruction there is an illegal-instruction crash, not silent corruption. ---- */
.Lirq_entry:
    sw ra, 0(sp)
    sw t2, 24(sp)
    sw a0, 36(sp)
    sw a1, 40(sp)
    sw a2, 44(sp)
    sw a3, 48(sp)
    sw a4, 52(sp)
    sw a5, 56(sp)
    sw a6, 60(sp)
    sw a7, 64(sp)
    sw t3, 108(sp)
    sw t4, 112(sp)
    sw t5, 116(sp)
    sw t6, 120(sp)
    csrr t1, mepc
    sw t1, 124(sp)
    la t1, g_e906_diag
    lw t2, 44(t1)
    addi t2, t2, 1
    sw t2, 44(t1)
    sw t0, 48(t1)
    lw t2, 124(sp)
    sw t2, 52(t1)
    csrr t1, mstatus
    sw t1, 136(sp)
    li t1, (3 << 13)
    csrc mstatus, t1              /* FS = Off for the handler */
    mv a0, t0                     /* mcause */
    lw a1, 124(sp)                /* mepc */
    call riscv_trap_dispatcher

    lw t0, 136(sp)
    csrw mstatus, t0              /* restores the interrupted FS, MPIE, MPP */
    lw t0, 124(sp)
    /* An interrupt taken on the idle wfi returns past it, so work it posted is
     * never slept through ([SPEC-ARCH-06], platform_idle_wait). A timer interrupt that
     * only took a profiler sample returns to the wfi ([SPEC-E906-IRQ-06]); the flag is
     * read and cleared on every trap return. t1..t3 are restored below. */
    la t2, g_irq_sample_only
    lw t3, 0(t2)
    sw zero, 0(t2)
    la t1, e906_idle_wfi
    beqz t3, 2f
    /* Sample only: back to the wfi whether mepc is the wfi or (as on this E906) the one after. */
    beq t0, t1, 1f
    addi t2, t1, 4
    bne t0, t2, 1f
    mv t0, t1
    j 1f
2:
    bne t0, t1, 1f
    addi t0, t0, 4
1:
    csrw mepc, t0
    lw ra, 0(sp)
    lw t0, 16(sp)
    lw t1, 20(sp)
    lw t2, 24(sp)
    lw a0, 36(sp)
    lw a1, 40(sp)
    lw a2, 44(sp)
    lw a3, 48(sp)
    lw a4, 52(sp)
    lw a5, 56(sp)
    lw a6, 60(sp)
    lw a7, 64(sp)
    lw t3, 108(sp)
    lw t4, 112(sp)
    lw t5, 116(sp)
    lw t6, 120(sp)
    addi sp, sp, 416
    mret

    .size default_trap_entry, . - default_trap_entry


    /* ============================================================= */
    /* Core Reset Entry Point                                        */
    /* ============================================================= */
    .section .text.startup, "ax", @progbits
    .global _start
    .type _start, @function

_start:
    /* @impl [SPEC-E906-ARCH-01] targets/allwinner_e906/SPECIFICATION.md */
    /* 1. Disable all interrupts during early bring-up */
    csrw mie, zero
    csrw mip, zero

    /* 2. Setup Hardware Stack Pointer in SRAM */
    la sp, _stack_top

    /* 3. Setup Global Pointer */
.option push
.option norelax
    la gp, __global_pointer$
.option pop

    /*
     * 4. mtvec = trap entry, MODE = 3 (T-Head CLIC, non-vectored). In direct
     *    mode no CLIC line is delivered. @impl [SPEC-E906-IRQ-01] targets/allwinner_e906/SPECIFICATION.md
     */
    la t0, default_trap_entry
    ori t0, t0, 3
    csrw mtvec, t0

    /* 
     * 5. Enable FPU (mstatus.FS = 0b11): single precision only, the E906 has no D.
     * Note: If running on an RV32IMAC core without FPU, remove this block
     * or ensure compiler flags match -march=rv32imac -mabi=ilp32.
     */
    li t0, (3 << 13)
    csrs mstatus, t0

    /*
     * 5a. Enable T-Head Vendor ISA Extensions (CSR_MXSTATUS 0x7C0)
     * Bit 22: THEADISAEE (T-Head ISA extension enable - required for dcache opcodes)
     * Bit 15: MM (Hardware unaligned memory access support)
     * @impl [SPEC-E906-CACHE-01] targets/allwinner_e906/SPECIFICATION.md
     */
    li t0, (1 << 22) | (1 << 15)
    csrs 0x7c0, t0

    /*
     * 5b. Invalidate both I-Cache and D-Cache prior to activation:
     * CSR_MCOR (0x7C2): Bit 6 = Clean & Invalidate all D-Cache, Bit 17 = Invalidate all I-Cache
     */
    li t0, (1 << 6) | (1 << 17)
    csrs 0x7c2, t0

    /*
     * 5c. Enable Hardware Configuration (CSR_MHCR 0x7C1):
     * Bit 0:  IE (Instruction Cache Enable)          -> ON
     * Bit 1:  DE (Data Cache Enable)                 -> OFF
     * Bit 2/3: WA/WB (D-Cache allocate/write-back)   -> OFF (no D-cache)
     * Bit 4:  RS (Return Stack Enable)               -> ON
     * Bit 5:  BPE (Branch Prediction Enable)         -> ON
     * Bit 12: BTB (Branch Target Buffer Enable)      -> ON
     *
     * D-cache stays OFF: all firmware data, IPC and DMA buffers live in
     * zero-wait-state on-chip SRAM, which SYSMAP hardwires as cacheable, so
     * with DE=1 every store to memory Linux or DMA reads would need a clean.
     * DE=0 gives deterministic load/store timing (low jitter) and needs no
     * cache maintenance. Matches the silicon-tested cubie-a5e firmware.
     * @impl [SPEC-E906-CACHE-02] targets/allwinner_e906/SPECIFICATION.md
     */
    li t0, (1 << 12) | (1 << 5) | (1 << 4) | (1 << 0)
    csrw 0x7c1, t0

    fence.i
    fence rw, rw

    /* 6. Copy .data from LMA to VMA if separate */
    la a0, _sdata
    la a1, _edata
    la a2, _sidata
    beq a0, a2, .Lcopy_data_done
.Lcopy_data:
    bgeu a0, a1, .Lcopy_data_done
    lw t0, 0(a2)
    sw t0, 0(a0)
    addi a0, a0, 4
    addi a2, a2, 4
    j .Lcopy_data
.Lcopy_data_done:

    /* 7. Zero .bss in SRAM */
    la a0, _sbss
    la a1, _ebss
.Lzero_bss:
    bgeu a0, a1, .Lzero_bss_done
    sw zero, 0(a0)
    addi a0, a0, 4
    j .Lzero_bss
.Lzero_bss_done:

    /* 8. Call global static C++ constructors (if any) */
    la t0, __libc_init_array
    beqz t0, .Lcall_main
    jalr ra, t0, 0

.Lcall_main:
    /* 9. Jump to application main */
    call main

.Lhang:
    wfi
    j .Lhang

    .size _start, . - _start