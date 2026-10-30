#ifndef IOPROCESSOR_HAL_PIO_HPP
#define IOPROCESSOR_HAL_PIO_HPP
// @impl [SPEC-E906-HAL-03] targets/allwinner_e906/SPECIFICATION.md

#include <stdint.h>
#include <stdbool.h>
#include "memory_map.h"

namespace fc::hal {

#define PB_DATA_REG (*(volatile uint32_t *)(PIO_BASE + 0x0040))
#define PC_DATA_REG (*(volatile uint32_t *)(PIO_BASE + 0x0070))
#define PI_DATA_REG (*(volatile uint32_t *)(PIO_BASE + 0x0190))

class Pio {
public:
    static void init();
    
    /* Port B Chip Select Control (PB11 / Pin 24 on 40-pin header) */
    static inline void set_cs0(bool active) {
        if (active)
            PB_DATA_REG &= ~(1U << 11); // Pull LOW (Active)
        else
            PB_DATA_REG |=  (1U << 11); // Pull HIGH (Inactive)
    }

    static inline void set_cs1(bool active) {
        if (active)
            PC_DATA_REG &= ~(1U << 7); // Pull LOW (Active)
        else
            PC_DATA_REG |=  (1U << 7); // Pull HIGH (Inactive)
    }

    /* Interrupt / Status Checks */
    static inline bool get_fpga_frame_ready() {
        return (PC_DATA_REG & (1 << 6)) != 0; // Pin 22 (PC6)
    }

    // @impl [SPEC-E906-IRQ-07] targets/allwinner_e906/SPECIFICATION.md
    static inline bool get_imu_drdy() {
        return (PI_DATA_REG & (1U << 8)) != 0; // Pin 29 (PI8, IMU INT1), measured 2026-10-29
    }
};

} // namespace fc::hal

#endif // IOPROCESSOR_HAL_PIO_HPP
