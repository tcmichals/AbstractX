/*
 * e906_ddr.ld - SRAM & DDR Linker Script for Allwinner T527 / A523 XuanTie E906
 *
 * Target: Allwinner XuanTie E906 RISC-V Co-Processor
 * Memory (E906 core address -> Linux host physical address):
 *   SRAM Space 0  0x3FFC0000 -> 0x07280000  256 KB  code, data, stack
 *     shared tail 0x3FFF0000 -> 0x072B0000   64 KB  reserved for host IPC
 *   SRAM Space 1  0x40000000 -> 0x072C0000  128 KB  resource table, trace0
 *     upper half  0x40020000 -> 0x072E0000  128 KB  Linux RPMsg pool (not firmware)
 *                                                   (REMAP_CTRL_REG[1]=1, set by remoteproc)
 *   DDR carveout  0x48100000                  1 MB  streaming payloads
 *
 * These are the only SRAM windows sunxi_rproc_da_to_va() accepts
 * (E906_SRAM_SPACE{0,1}_DA in drivers/remoteproc/sunxi_rproc.h); a segment
 * outside them, or outside a DRAM carveout, makes remoteproc refuse the ELF.
 * @impl [SPEC-E906-MEM-01] targets/allwinner_e906/SPECIFICATION.md
 *
 * HARDWARE TRUTHS:
 * - 0x00020000 (128 KB) is HiFi4 DSP Memory! DO NOT USE for E906!
 * - 0x00044000 (160 KB) is OP-TEE / TrustZone Memory (SRAM A2)! DO NOT USE!
 */

OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    /* SRAM Space 0, minus the 64 KB shared tail at 0x3FFF0000 (host IPC) */
    SRAM_FAST (rwx) : ORIGIN = 0x3FFC0000, LENGTH = 192K
    /* SRAM Space 1 (SRAMA3_2 via REMAP_CTRL_REG[1]=1), lower half */
    SRAM_A3_2 (rwx) : ORIGIN = 0x40000000, LENGTH = 128K
    /* SRAM Space 1 upper half (host 0x072E0000): Linux RPMsg DMA pool,
     * vrings + RPMsg buffers ([SPEC-E906-IPC-04]). Firmware places nothing here. */
    IPC       (rw)  : ORIGIN = 0x40020000, LENGTH = 128K
    /* Non-cacheable DDR DMA Carveout */
    DRAM      (rwx) : ORIGIN = 0x48100000, LENGTH = 1M
}

SECTIONS
{
    /* 1. Fast Vectors, Startup & Time-Critical Code in Zero-Wait-State SRAM A3 (Pool 0) */
    .vectors : ALIGN(64)
    {
        KEEP(*(.vectors))
        KEEP(*(.text.startup))
        *(.text.fast .text.fast.*)
        *(.text.trap)
    } > SRAM_FAST

    /* Standard application code in fast SRAM */
    .text : ALIGN(4)
    {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        *(.srodata)
        *(.srodata.*)
    } > SRAM_FAST

    /* Global C++ constructor tables */
    .preinit_array : ALIGN(4)
    {
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP (*(.preinit_array*))
        PROVIDE_HIDDEN (__preinit_array_end = .);
    } > SRAM_FAST

    .init_array : ALIGN(4)
    {
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP (*(.init_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .ctors))
        PROVIDE_HIDDEN (__init_array_end = .);
    } > SRAM_FAST

    /* 2. Fast Data & Critical Data */
    .data : ALIGN(4)
    {
        _sdata = .;
        __data_start = .;
        PROVIDE(__global_pointer$ = . + 0x800);
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
        _edata = .;
        __data_end = .;
    } > SRAM_FAST
    _sidata = LOADADDR(.data);
    __data_load = LOADADDR(.data);

    .bss : ALIGN(8)
    {
        _sbss = .;
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
        _ebss = .;
        __bss_end = .;
    } > SRAM_FAST

    /* Execution Stack allocated in fast SRAM (8 KB, matches mp_stack_set_limit) */
    .stack (NOLOAD) : ALIGN(16)
    {
        _stack_bottom = .;
        __stack_bottom = .;
        . += 0x00002000; /* 8 KB Stack */
        _stack_top = .;
        __stack_top = .;
        _sp = .;
    } > SRAM_FAST

    /* 3. Resource Table & trace0 placed in SRAM_FAST so remoteproc debugfs reliably maps them */
    .resource_table : ALIGN(1024)
    {
        __resource_table_start = .;
        KEEP(*(.resource_table))
        KEEP(*(.resource_table*))
        __resource_table_end = .;
    } > SRAM_FAST

    /* RemoteProc Trace Buffer in SRAM_FAST (Minimal 1KB for Crash / Liveness) */
    .trace_buffer (NOLOAD) : ALIGN(4)
    {
        __trace_start = .;
        KEEP(*(.trace_buffer))
        KEEP(*(.trace_buffer.*))
        . = ALIGN(4);
        __trace_end = .;
    } > SRAM_FAST

    /* Shared Application / IPC Memory in SRAM A3 Pool 1 */
    .sram_c (NOLOAD) : ALIGN(4)
    {
        __sram_c_start = .;
        *(.sram_c)
        *(.sram_c.*)
        *(.sram_c_loc1)
        *(.sram_c_loc2)
        *(.sram_ipc .sram_ipc.*)
        *(.vring_ctrl)
        . = ALIGN(4);
        __sram_c_end = .;
    } > SRAM_A3_2

    /* 4. DDR DRAM: Large DMA Packet Buffers */
    .dram_payload (NOLOAD) : ALIGN(4096)
    {
        *(.dram_payload .dram_payload.*)
        *(.rpmsg_bufs)
    } > DRAM

    .dtcm_scratch (NOLOAD) : ALIGN(4)
    {
        __dtcm_scratch_start = .;
        *(.dtcm_scratch)
        *(.dtcm_scratch.*)
        . = ALIGN(4);
        __dtcm_scratch_end = .;
    } > SRAM_FAST

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.comment)
        *(.note.gnu.build-id)
    }
}