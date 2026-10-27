#ifndef IOPROCESSOR_MEMORY_MAP_H
#define IOPROCESSOR_MEMORY_MAP_H

#include <stdint.h>

/*
 * ============================================================================
 *           XuanTie E906 RISC-V Memory Map (Allwinner A523 / A527 / T527)
 * ============================================================================
 */

/*
 * Memory Architecture Truths (Allwinner T527 / A527 XuanTie E906):
 * 1. PURE SRAM & DDR ARCHITECTURE: There is NO ITCM and NO DTCM on E906.
 * 2. 0x00020000 (128 KB) IS HIFI4 DSP MEMORY: Physically wired to the DSP as its local RAM.
 *    E906 must never boot or run from here!
 * 3. 0x00044000 (160 KB) IS OP-TEE / TRUSTZONE MEMORY (SRAM A2): Firewalled for secure boot.
 * 4. STAY OUT OF DSP SECONDARY RAM: 0x00400000 - 0x0044FFFF belongs to HiFi4 DSP.
 * 5. E906 FIRMWARE LIVES IN SRAM (core address -> host physical address):
 *    - SRAM Space 0: 0x3FFC0000 -> 0x07280000 (256 KB, r_sram)
 *    - SRAM Space 1: 0x40000000 -> 0x072C0000 (256 KB, r_sram1, REMAP_CTRL_REG[1]=1)
 *    These are the only SRAM windows sunxi_rproc_da_to_va() accepts
 *    (E906_SRAM_SPACE{0,1}_DA in drivers/remoteproc/sunxi_rproc.h).
 * 6. 0x07130000 IS THE RISC-V CFG REGISTER BLOCK (4 KB), NOT SRAM.
 *
 * @impl [SPEC-E906-MEM-01] targets/allwinner_e906/SPECIFICATION.md
 */

/* Forbidden Non-E906 Regions (DO NOT USE FOR E906!) */
#define DSP_LOCAL_RAM_BASE          0x00020000  /* 128 KB HiFi4 DSP Local Memory */
#define DSP_LOCAL_RAM_SIZE          0x00020000
#define OPTEE_SRAM_A2_BASE          0x00044000  /* 160 KB OP-TEE / TrustZone SRAM A2 */
#define OPTEE_SRAM_A2_SIZE          0x00028000

/* Verified Hardware Memory Windows for E906 (SRAM Pools) */
#define SRAM_A3_BASE                0x3FFC0000  /* Primary E906 Execution Window (Space 0) */
#define SRAM_A3_SPACE0_BASE         0x3FFC0000  /* 256 KB Dedicated SRAM Slice 0 (host 0x07280000) */
#define SRAM_A3_SPACE0_SIZE         0x00040000
#define SRAM_A3_SPACE1_BASE         0x40000000  /* 256 KB Switchable SRAM Slice 1 (host 0x072C0000) */
#define SRAM_A3_SPACE1_SIZE         0x00040000
#define SRAM_SPACE0_HOST_PA         0x07280000
#define SRAM_SPACE1_HOST_PA         0x072C0000

/* Shared tail of Space 0, excluded from the linker's SRAM_FAST region and
 * readable by Linux at host 0x072B0000. Layout matches cubie-a5e's
 * shm_ping_protocol.h / dram_spsc_protocol.h (verified on silicon). */
#define SRAM_SHARED_TAIL_BASE       0x3FFF0000  /* host 0x072B0000 */
#define SRAM_SHARED_TAIL_SIZE       0x00010000  /* 64 KB */

/* [SPEC-E906-IPC-04] Upper half of Space 1: Linux remoteproc DMA pool (vrings +
 * RPMsg buffers, written in place by the trace engine). Firmware places nothing here. */
#define E906_RPMSG_POOL_BASE        0x40020000  /* host 0x072E0000 */
#define E906_RPMSG_POOL_HOST_PA     0x072E0000
#define E906_RPMSG_POOL_SIZE        0x00020000  /* 128 KB */

/* Default E906 SRAM Aliases */
#define SRAM_BASE                   SRAM_A3_BASE
#define SRAM_SIZE                   (SRAM_A3_SPACE0_SIZE + SRAM_A3_SPACE1_SIZE)
#define R_SRAM_BASE                 SRAM_A3_BASE
#define R_SRAM_SIZE                 SRAM_SIZE

/* Hardware REMAP Control Register (Offset 0x364) */
#define SUNXI_DSP_CCU_BASE          0x07140000  /* DSP CCU Base on T527 (sun60iw1) */
#define SUNXI_PRCM_R_CCU_BASE       0x07010000  /* R_CCU / PRCM Base on A527 (sun55iw3) */
#define SUNXI_REMAP_CTRL_OFFSET     0x0364
#define SUNXI_REMAP_CTRL_REG_T527   (SUNXI_DSP_CCU_BASE + SUNXI_REMAP_CTRL_OFFSET)
#define SUNXI_REMAP_CTRL_REG_A527   (SUNXI_PRCM_R_CCU_BASE + SUNXI_REMAP_CTRL_OFFSET)

#define REMAP_CTRL_MCU_RAM_REMAP_BIT      (1U << 0) /* 0: DSP local RAM only for MCU_SYS; 1: share for system */
#define REMAP_CTRL_SRAMA3_2_RAM_REMAP_BIT (1U << 1) /* 0: SRAMA3_2 not shared for MCU_SYS; 1: shares for MCU_SYS */

/* Diagnostics in the shared tail of SRAM Space 0. (The former IPC TX/RX ring
 * offsets from IPC_SHARED_MEM_BASE were never used and would have overlaid
 * the vector table; IPC rings belong in the shared tail or Space 1.) */
#define IPC_SHARED_MEM_BASE         SRAM_A3_BASE
#define IPC_CRASH_DUMP_OFFSET       0x0003FF00  /* 256 B - Fatal Trap Dump: core 0x3FFFFF00, host 0x072BFF00 */
#define IPC_CRASH_DUMP_SIZE         0x0100

/*
 * RemoteProc Trace Buffer Note:
 * g_rproc_trace_buffer is placed into section .trace_buffer in on-chip SRAM (SRAM_A3).
 * Trace buffers MUST NEVER be in DDR!
 */

/* Core SoC Peripheral Base Addresses (RISC-V Local MMIO View) */
#define PIO_BASE                    0x02000000  /* Main PIO Controller (PB-PK) */
#define CCU_BASE                    0x02001000  /* Main Clock Control Unit */
#define SUNXI_CCU_BASE              CCU_BASE    /* HAL Compatibility Alias */
#define MSGBOX_BASE                 0x03003000  /* Hardware Message Box Doorbell */
#define SPI0_BASE                   0x04025000  /* High-Speed SPI0 Controller (Port C) */
#define SPI1_BASE                   0x04026000  /* High-Speed SPI1 Controller (Port B - 40-Pin Header) */
#define UART0_BASE                  0x02500000  /* UART0 Debug Serial Console */
#define UART2_BASE                  0x02500800  /* UART2 (PB0/PB1 not connected on the Cubie A5E) */
#define UART3_BASE                  0x02500C00  /* UART3 GPS / navigation serial (PI11/PI12, header pins 16, 31) */
#define TWI0_BASE                   0x02502000  /* High-Speed I2C / TWI0 Controller */
#define TWI1_BASE                   0x02502400  /* High-Speed I2C / TWI1 Controller */

/* MCU / Subsystem Control Blocks */
#define MCU_CCU_BASE                0x07010000  /* MCU Subsystem Clock/Reset Control */
#define SUNXI_MCU_PRCM_BASE         0x07102000  /* MCU Local PRCM Base */
#define R_PIO_BASE                  0x07022000  /* PRCM R_PIO Controller (PL, PM) */

/* Core Frequency Definition */
#define CPU_FREQ_HZ                 200000000ULL /* XuanTie E906/E906 Core Frequency: Up to 200 MHz */

#endif /* IOPROCESSOR_MEMORY_MAP_H */

