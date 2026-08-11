/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX 64-Byte PCIe-like TLP Data Structures & Hardware Map
 * Portable C/C++ Header for ARM Cortex-A55 (Linux) & T-Head E907 RISC-V Firmware
 */

#ifndef ASP_TLP64_H
#define ASP_TLP64_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* TLP Container Metrics */
#define ASP_TLP64_SIZE          64
#define ASP_TLP64_PAYLOAD_SIZE  40
#define ASP_TLP64_DWORDS        16

/* TLP Type Definitions */
#define ASP_TLP_TYPE_MEM_RD     0x01  /* Host -> FPGA Memory Read */
#define ASP_TLP_TYPE_MEM_WR     0x02  /* Host -> FPGA Memory Write */
#define ASP_TLP_TYPE_CPL_D      0x03  /* FPGA -> Host Completion with Data */
#define ASP_TLP_TYPE_CPL        0x04  /* FPGA -> Host Completion Status */
#define ASP_TLP_TYPE_DMA_STREAM 0x10  /* FPGA -> Host Autonomous Stream */
#define ASP_TLP_TYPE_DMA_CFG    0x11  /* Host -> FPGA DMA Config */

/* Channel / AXID Routing Planes */
#define ASP_CHANNEL_CONTROL     0x01  /* Wishbone Gateway */
#define ASP_CHANNEL_TELEMETRY   0x02  /* IMU Auto-DMA Stream */
#define ASP_CHANNEL_FC_LOG      0x03  /* Flight Log Stream */
#define ASP_CHANNEL_DEBUG_TRACE 0x04  /* Debug Trace */
#define ASP_CHANNEL_ESC_SERIAL  0x05  /* UART ESC Serial Tunnel */

/* Host Dual-SPI Command Bytes */
#define ASP_SPI_CMD_READ_STATUS 0xA0  /* Read 4-byte status vector */
#define ASP_SPI_CMD_WRITE_BURST 0xA1  /* Write 64-byte TLP burst */
#define ASP_SPI_CMD_READ_BURST  0xA2  /* Read 64-byte TLP burst */

/* Wishbone Device Base Address Map */
#define ASP_ADDR_WHO_AM_I       0x40000000
#define ASP_ADDR_IMU_BASE       0x40000100
#define ASP_ADDR_DSHOT_BASE     0x40000300
#define ASP_ADDR_UART_ESC_BASE  0x40000500
#define ASP_ADDR_LED_BASE       0x40000C00

/*
 * Portable 64-Byte TLP Packed Structure
 * Big-Endian Wire Layout
 */
typedef struct __attribute__((packed, aligned(64))) {
    uint8_t  type;            /* TLP operation type */
    uint8_t  flags;           /* ACK request / Error bitfield */
    uint8_t  tag;             /* Split-transaction correlation ID */
    uint8_t  channel;         /* Routing plane / AXID */
    uint32_t target_address;  /* 32-bit Wishbone target address (Device ID) */
    uint16_t length_dw;       /* Valid payload length in 32-bit DWORDs */
    uint16_t sequence;        /* Sequence counter */
    uint64_t timestamp_ns;    /* 64-bit nanosecond hardware timestamp */
    uint8_t  payload[40];     /* Data payload (zero-padded if < 40B) */
    uint32_t crc32;           /* IEEE 802.3 CRC32 checksum */
} asp_tlp64_t;

/* Host Status Response Structure (Command 0xA0) */
typedef struct __attribute__((packed)) {
    uint8_t  version;         /* Protocol version (0x64) */
    uint8_t  status_flags;    /* Egress ready, ingress accept, CRC err */
    uint8_t  egress_count;    /* Number of 64B TLPs ready in FPGA output FIFO */
    uint8_t  ingress_space;   /* Free 64B slots in FPGA input FIFO */
} asp_spi_status_t;

#ifdef __cplusplus
}
#endif

#endif /* ASP_TLP64_H */
