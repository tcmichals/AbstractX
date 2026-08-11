// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PCIE_REG_API_H
#define PCIE_REG_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Virtual BAR Address Map
#define PCIE_BAR_SYS_BASE     0x40000000
#define PCIE_BAR_IMU_BASE     0x40000100
#define PCIE_BAR_ESC_BASE     0x40000200
#define PCIE_BAR_BARO_BASE    0x40000300
#define PCIE_BAR_MAG_BASE     0x40000400
#define PCIE_BAR_SERIAL_BASE  0x40000500

// Abstract 32-Bit Register Read / Write Hardware Interface
uint32_t pcie_reg_read32(uint32_t addr);
void     pcie_reg_write32(uint32_t addr, uint32_t value);

// Asynchronous Telemetry Event Callback Function Signature
typedef void (*pcie_dma_callback_t)(uint32_t addr, const uint8_t *payload, uint32_t len, uint64_t timestamp_ns);

// Register hardware event callback for autonomous stream packets (e.g. IMU Telemetry)
void pcie_register_dma_callback(uint32_t target_addr, pcie_dma_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif // PCIE_REG_API_H
