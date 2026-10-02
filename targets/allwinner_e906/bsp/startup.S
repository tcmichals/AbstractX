/*
 * startup.S - XuanTie E906 (RV32IMAFDC) Unified Startup & Exception Vector Table
 */

    .section .vectors, "ax", @progbits
    .align 4
    .global default_trap_entry
    .type default_trap_entry, @function

/*
 * =====================================================================
 * Direct Mode Trap Entry Point
 * =====================================================================
 */
default_trap_entry:
    /* Allocate 144 bytes on stack for CrashFrame */
    addi sp, sp, -144

    /* Save caller-saved registers and critical GPRs */
    sw ra,   0(sp)
    
    /* -----------------------------------------------------------------
     * FIX 1: Save original t0 BEFORE using it for scratch calculations.
     * In the previous version, 'addi t0, sp, 144' overwrote t0, so
     * saving it to 16(sp) dumped the computed SP instead of original t0.
     * ----------------------------------------------------------------- */
    sw t0,  16(sp)

    /* Calculate and store the original sp prior to this trap entry */
    addi t0, sp, 144
    sw t0,   4(sp)

    /* Save remaining GPRs x3..x31 */
    sw gp,   8(sp)
    sw tp,  12(sp)
    sw t1,  20(sp)
    sw t2,  24(sp)
    sw s0,  28(sp)
    sw s1,  32(sp)
    sw a0,  36(sp)
    sw a1,  40(sp)
    sw a2,  44(sp)
    sw a3,  48(sp)
    sw a4,  52(sp)
    sw a5,  56(sp)
    sw a6,  60(sp)
    sw a7,  64(sp)
    sw s2,  68(sp)
    sw s3,  72(sp)
    sw s4,  76(sp)
    sw s5,  80(sp)
    sw s6,  84(sp)
    sw s7,  88(sp)
    sw s8,  92(sp)
    sw s9,  96(sp)
    sw s10, 100(sp)
    sw s11, 104(sp)
    sw t3,  108(sp)
    sw t4,  112(sp)
    sw t5,  116(sp)
    sw t6,  120(sp)

    /* Save RISC-V Machine Control and Status Registers (CSRs) */
    csrr t0, mepc
    sw t0,  124(sp)
    csrr t0, mcause
    sw t0,  128(sp)
    csrr t0, mtval
    sw t0,  132(sp)
    csrr t0, mstatus
    sw t0,  136(sp)

    /* -----------------------------------------------------------------
     * FIX 2: Check if this trap is an asynchronous interrupt (bit 31).
     * If mcause < 0 (sign bit set), dispatch interrupt and return (mret).
     * Only true synchronous exceptions/faults enter hal_crash_dispatcher.
     * ----------------------------------------------------------------- */
    bltz t0, .Lhandle_irq

    /* Pass pointer to CrashFrame (sp) in a0 to C++ crash dispatcher */
    mv a0, sp
    call hal_crash_dispatcher

.Ltrap_hang:
    wfi
    j .Ltrap_hang

.Lhandle_irq:
    /* Call C++ interrupt dispatcher: void riscv_trap_dispatcher(mcause, mepc) */
    lw a0, 128(sp) /* mcause */
    lw a1, 124(sp) /* mepc */
    call riscv_trap_dispatcher

    /* Restore CSRs */
    lw t0, 136(sp)
    csrw mstatus, t0
    lw t0, 124(sp)
    csrw mepc, t0

    /* Restore all GPRs */
    lw ra,   0(sp)
    lw gp,   8(sp)
    lw tp,  12(sp)
    lw t0,  16(sp)
    lw t1,  20(sp)
    lw t2,  24(sp)
    lw s0,  28(sp)
    lw s1,  32(sp)
    lw a0,  36(sp)
    lw a1,  40(sp)
    lw a2,  44(sp)
    lw a3,  48(sp)
    lw a4,  52(sp)
    lw a5,  56(sp)
    lw a6,  60(sp)
    lw a7,  64(sp)
    lw s2,  68(sp)
    lw s3,  72(sp)
    lw s4,  76(sp)
    lw s5,  80(sp)
    lw s6,  84(sp)
    lw s7,  88(sp)
    lw s8,  92(sp)
    lw s9,  96(sp)
    lw s10, 100(sp)
    lw s11, 104(sp)
    lw t3,  108(sp)
    lw t4,  112(sp)
    lw t5,  116(sp)
    lw t6,  120(sp)

    /* Deallocate stack frame and return from trap */
    addi sp, sp, 144
    mret

    .size default_trap_entry, . - default_trap_entry


    /* ============================================================= */
    /* Core Reset Entry Point                                        */
    /* ============================================================= */
    .section .text.startup, "ax", @progbits
    .global _start
    .type _start, @function

_start:
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
     * 4. Set mtvec CSR to Direct Mode Trap Handler 
     */
    la t0, default_trap_entry
    csrw mtvec, t0

    /* 
     * 5. Enable FPU (mstatus.FS = 0b11) for hardware float/double.
     * Note: If running on an RV32IMAC core without FPU, remove this block
     * or ensure compiler flags match -march=rv32imac -mabi=ilp32.
     */
    li t0, (3 << 13)
    csrs mstatus, t0

    /*
     * 5a. Enable T-Head Vendor ISA Extensions (CSR_MXSTATUS 0x7C0)
     * Bit 22: THEADISAEE (T-Head ISA extension enable - required for dcache opcodes)
     * Bit 15: MM (Hardware unaligned memory access support)
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
     * Bit 0:  IE (Instruction Cache Enable)
     * Bit 1:  DE (Data Cache Enable)
     * Bit 2:  WB (Write-Back mode for D-Cache)
     * Bit 3:  WA (Write-Allocate mode for D-Cache)
     * Bit 4:  RS (Return Stack Enable)
     * Bit 5:  BPE (Branch Prediction Enable)
     * Bit 12: BTB (Branch Target Buffer Enable)
     */
    li t0, (1 << 12) | (0xF << 2) | (1 << 1) | (1 << 0)
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