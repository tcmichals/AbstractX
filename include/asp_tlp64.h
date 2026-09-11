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
#define ASP_CHANNEL_SPI_BRIDGE  0x06  /* SPI Packet Transfer */
#define ASP_CHANNEL_I2C_BRIDGE  0x07  /* I2C Packet Transfer */
#define ASP_CHANNEL_GPIO_BRIDGE 0x08  /* GPIO / PIO Pin Control & Event Stream */

/* Host Dual-SPI Command Bytes */
#define ASP_SPI_CMD_READ_STATUS 0xA0  /* Read 4-byte status vector */
#define ASP_SPI_CMD_WRITE_BURST 0xA1  /* Write 64-byte TLP burst */
#define ASP_SPI_CMD_READ_BURST  0xA2  /* Read 64-byte TLP burst */

/* Wishbone & Virtual BAR Base Address Map */
#define ASP_ADDR_WHO_AM_I       0x40000000
#define ASP_ADDR_IMU_BASE       0x40000100
#define ASP_ADDR_SPI_BASE       0x40000180
#define ASP_ADDR_DSHOT_BASE     0x40000300
#define ASP_ADDR_I2C_BASE       0x40000380
#define ASP_ADDR_MAG_BASE       0x40000400
#define ASP_ADDR_UART_ESC_BASE  0x40000500
#define ASP_ADDR_GPIO_BASE      0x40000800
#define ASP_ADDR_LED_BASE       0x40000C00

/* Register Offsets */
#define ASP_REG_CFG             0x00  /* Bus clock / baud configuration IOCTL */
#define ASP_REG_XFER            0x08  /* Packet transfer trigger */

/* SPI Protocol Operation Flags (DW0 Flags / payload flags) */
#define ASP_SPI_FLAG_AUTO_CS     0x01  /* Assert CS before transfer, deassert after */
#define ASP_SPI_FLAG_HOLD_CS     0x02  /* Keep CS asserted across packet chain */
#define ASP_SPI_FLAG_FULL_DUPLEX 0x04  /* Full-duplex simultaneous TX and RX */
#define ASP_SPI_FLAG_DUAL_SPI    0x08  /* Dual-SPI mode (2-bit transfer) */
#define ASP_SPI_FLAG_MANUAL_CS   0x10  /* Manual CS handling via GPIO */

/* I2C Protocol Operation Flags (DW0 Flags / payload flags) */
#define ASP_I2C_FLAG_USE_REG        0x01  /* Sub-address register offset present */
#define ASP_I2C_FLAG_REPEATED_START 0x02  /* Combined write-then-read without releasing bus */
#define ASP_I2C_FLAG_HOLD_BUS       0x04  /* Do not emit STOP condition after transfer */
#define ASP_I2C_FLAG_10BIT          0x08  /* 10-bit slave address */
#define ASP_I2C_FLAG_16BIT_REG      0x10  /* 16-bit register offset */

/* SPI TLP Payload Headers */
typedef struct __attribute__((packed)) {
    uint8_t  bus_id;          /* SPI bus index (0=SPI0, 1=SPI1) */
    uint8_t  cs_pin;          /* Chip-select GPIO or index (e.g. 13 for GP13) */
    uint8_t  tx_len;          /* Transmit byte count in payload[4..39] (0..36) */
    uint8_t  rx_len;          /* Receive byte count requested from MISO (0..36) */
} asp_tlp_spi_req_header_t;

typedef struct __attribute__((packed)) {
    uint8_t  status;          /* 0=Ok, 1=DmaError, 2=Timeout, 3=CrcError, 4=QueueFull */
    uint8_t  transferred_len; /* Actual bytes transferred (0..36) */
    uint16_t bus_duration_us; /* Hardware bus clocking duration in microseconds */
} asp_tlp_spi_cpl_header_t;

/* General Completion Status Codes */
#define ASP_STATUS_OK               0x00
#define ASP_STATUS_DMA_ERROR        0x01
#define ASP_STATUS_TIMEOUT          0x02
#define ASP_STATUS_CRC_ERROR        0x03
#define ASP_STATUS_QUEUE_FULL       0x04
#define ASP_STATUS_BUS_LOCKED       0x05  /* Manual R/W rejected: Auto-DMA active */

/* Auto-DMA / Hardware Trigger Commands */
#define ASP_DMA_CMD_SETUP           0x01  /* Configure trigger, pin, edge, burst */
#define ASP_DMA_CMD_ENABLE          0x02  /* Arm Auto-DMA mode (locks bus for streaming) */
#define ASP_DMA_CMD_DISABLE         0x03  /* Disarm Auto-DMA mode (unlocks bus for manual R/W) */

/* Auto-DMA Configuration Payload Header */
typedef struct __attribute__((packed)) {
    uint8_t  cmd;             /* ASP_DMA_CMD_SETUP, ENABLE, DISABLE */
    uint8_t  channel_id;      /* Channel index (0..3) */
    uint8_t  trigger_pin;     /* GPIO pin index */
    uint8_t  trigger_edge;    /* 1=Rising/Pos, 2=Falling/Neg, 3=Both */
    uint8_t  bus_type;        /* 0=SPI, 1=I2C */
    uint8_t  bus_id;          /* SPI0 / SPI1 */
    uint8_t  tx_len;          /* Command length */
    uint8_t  rx_len;          /* Burst read length */
    uint8_t  tx_cmd[8];       /* Outgoing command bytes */
    uint8_t  tlp_channel;     /* Egress channel (0x02) */
    uint8_t  tlp_tag;         /* Correlation tag */
} asp_tlp_auto_dma_cfg_t;

/* I2C TLP Payload Headers */
typedef struct __attribute__((packed)) {
    uint8_t  bus_id;          /* I2C Bus index (0=I2C0, 1=I2C1) */
    uint8_t  slave_addr;      /* 7-bit slave address (e.g. 0x76) */
    uint8_t  reg_offset;      /* Register sub-address (e.g. 0xF7) */
    uint8_t  sub_flags;       /* Extended flags or high 8 bits of 16-bit reg */
    uint8_t  tx_len;          /* Transmit byte count in payload[8..39] (0..32) */
    uint8_t  rx_len;          /* Receive byte count requested (0..32) */
    uint16_t reserved;
} asp_tlp_i2c_req_header_t;

typedef struct __attribute__((packed)) {
    uint8_t  status;          /* 0=Ok, 1=NackAddress, 2=NackData, 3=BusError, 4=Timeout, 5=BusLocked */
    uint8_t  transferred_len; /* Actual bytes read or written (0..36) */
    uint16_t bus_duration_us; /* Hardware bus clocking duration in microseconds */
} asp_tlp_i2c_cpl_header_t;

/* GPIO / PIO Virtual Register Offsets (Base: ASP_ADDR_GPIO_BASE = 0x40000800) */
#define ASP_GPIO_REG_CONFIG         0x00  /* Pin direction & pull configuration */
#define ASP_GPIO_REG_WRITE          0x04  /* Direct Pin output write (0=Low, 1=High) */
#define ASP_GPIO_REG_SET            0x08  /* Atomic bit SET (Output HIGH) */
#define ASP_GPIO_REG_CLR            0x0C  /* Atomic bit CLEAR (Output LOW) */
#define ASP_GPIO_REG_XOR            0x10  /* Atomic bit XOR (Toggle) */
#define ASP_GPIO_REG_READ           0x14  /* Pin input read */
#define ASP_GPIO_REG_IRQ_CFG        0x18  /* Edge IRQ attach: 1=Pos/Rising, 2=Neg/Falling, 3=Both */
#define ASP_GPIO_REG_IRQ_STATUS     0x1C  /* Pending IRQ status */

/* GPIO Commands */
#define ASP_GPIO_CMD_CONFIG         0x01  /* Configure Pin Mode & Pull */
#define ASP_GPIO_CMD_WRITE          0x02  /* Write Pin Output Level */
#define ASP_GPIO_CMD_SET            0x03  /* Atomic Set Pin (High) */
#define ASP_GPIO_CMD_CLR            0x04  /* Atomic Clear Pin (Low) */
#define ASP_GPIO_CMD_XOR            0x05  /* Atomic XOR Pin (Toggle) */
#define ASP_GPIO_CMD_READ           0x06  /* Read Pin Input Level */
#define ASP_GPIO_CMD_IRQ_ATTACH     0x07  /* Configure & Arm Edge ISR */

/* GPIO Pin Modes */
#define ASP_GPIO_MODE_INPUT         0x00
#define ASP_GPIO_MODE_OUTPUT        0x01
#define ASP_GPIO_MODE_ALTERNATE     0x02
#define ASP_GPIO_MODE_ANALOG        0x03

/* GPIO Pull Modes */
#define ASP_GPIO_PULL_NONE          0x00
#define ASP_GPIO_PULL_UP            0x01
#define ASP_GPIO_PULL_DOWN          0x02

/* GPIO Edge Triggers */
#define ASP_GPIO_EDGE_NONE          0x00
#define ASP_GPIO_EDGE_RISING        0x01  /* Positive Edge */
#define ASP_GPIO_EDGE_FALLING       0x02  /* Negative Edge */
#define ASP_GPIO_EDGE_BOTH          0x03  /* Both Positive & Negative */

/* GPIO TLP Payload Headers */
typedef struct __attribute__((packed)) {
    uint8_t  cmd;         /* ASP_GPIO_CMD_* */
    uint8_t  pin;         /* Physical pin number (0..63) */
    uint8_t  mode;        /* ASP_GPIO_MODE_* */
    uint8_t  pull_edge;   /* ASP_GPIO_PULL_* or ASP_GPIO_EDGE_* */
    uint32_t mask;        /* 32-bit pin bitmask for multi-pin Set/Xor/Clear */
    uint32_t value;       /* Direct value or state */
} asp_tlp_gpio_req_header_t;

typedef struct __attribute__((packed)) {
    uint8_t  status;          /* 0=Ok, Error */
    uint8_t  pin;             /* Pin number */
    uint8_t  level;           /* Pin input or output level (0 or 1) */
    uint8_t  edge_detected;   /* 0=None, 1=Rising/Pos, 2=Falling/Neg */
    uint32_t mask_state;      /* Full 32-bit port level mask */
    uint64_t timestamp_ns;    /* Nanosecond timestamp when read or IRQ triggered */
} asp_tlp_gpio_cpl_header_t;



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
    union {
        uint16_t length_dw;   /* Valid payload length in 32-bit DWORDs */
        uint16_t length;      /* Backward-compatible byte/DW alias */
    };
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
