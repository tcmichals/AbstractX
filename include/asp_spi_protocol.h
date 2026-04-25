// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASP_SPI_PROTOCOL_H_
#define ASP_SPI_PROTOCOL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ASP SPI vChip profile constants
 * Source of truth:
 *   docs/ASP_SPI_REGISTER_MAP.md
 */

/* Command bytes */
#define ASP_CMD_WRITE_DATA   ((uint8_t)0x80u)
#define ASP_CMD_READ_STATUS  ((uint8_t)0x01u)
#define ASP_CMD_READ_DATA    ((uint8_t)0x02u)

/* Status bits (ASP_REG_STATUS baseline layout) */
#define ASP_STATUS_RX_READY   ((uint8_t)(1u << 0))
#define ASP_STATUS_RX_OVERFLOW ((uint8_t)(1u << 1))
#define ASP_STATUS_CRC_ERR    ((uint8_t)(1u << 2))
#define ASP_STATUS_LEN_ERR    ((uint8_t)(1u << 3))
#define ASP_STATUS_TX_ACCEPT  ((uint8_t)(1u << 4))
#define ASP_STATUS_RESYNC_EVT ((uint8_t)(1u << 5))

/* Fixed READ_STATUS response length after command byte */
#define ASP_READ_STATUS_RESP_LEN ((uint8_t)4u)

/* Fixed response fields:
 *   B0: version
 *   B1: status
 *   B2: rx_len_msb
 *   B3: rx_len_lsb
 */
typedef struct asp_read_status_resp_s {
    uint8_t version;
    uint8_t status;
    uint8_t rx_len_msb;
    uint8_t rx_len_lsb;
} asp_read_status_resp_t;

static inline uint16_t asp_rx_len_from_status(const asp_read_status_resp_t* r)
{
    return (uint16_t)(((uint16_t)r->rx_len_msb << 8) | (uint16_t)r->rx_len_lsb);
}

static inline int asp_status_is_rx_ready(uint8_t status)
{
    return (status & ASP_STATUS_RX_READY) != 0u;
}

static inline int asp_status_is_tx_accept(uint8_t status)
{
    return (status & ASP_STATUS_TX_ACCEPT) != 0u;
}

#ifdef __cplusplus
}
#endif

#endif /* ASP_SPI_PROTOCOL_H_ */
