#include "pio.hpp"
#include "memory_map.h"

namespace fc::hal {

/* Main PIO Registers */

#define PC_CFG0         (*(volatile uint32_t *)(PIO_BASE + 0x0060))
#define PC_CFG1         (*(volatile uint32_t *)(PIO_BASE + 0x0064))
#define PC_DATA         (*(volatile uint32_t *)(PIO_BASE + 0x0070))
#define PC_DRV0         (*(volatile uint32_t *)(PIO_BASE + 0x0074))
#define PC_DRV1         (*(volatile uint32_t *)(PIO_BASE + 0x0078))

void Pio::init() {
    // @impl [SPEC-E906-HAL-04] targets/allwinner_e906/SPECIFICATION.md
    // UART / TWI pins are set by their driver from hal/ports.hpp;
    // UART2's PB0/PB1 are not connected on the Cubie A5E.

    /*
     * 2. Configure Port C (SPI0 on Pins 19, 21, 23, 24, 26)
     * - PC2 (MOSI/IO0) -> SPI0 (Mux 4)
     * - PC3 (CS0)      -> Output GPIO (Mux 1, high)
     * - PC4 (MISO/IO1) -> SPI0 (Mux 4)
     * - PC7 (CS1)      -> Output GPIO (Mux 1, high)
     * - PC12 (CLK)     -> SPI0 (Mux 4)
     */
    PC_CFG0 &= ~((0xF << 8) | (0xF << 12) | (0xF << 16) | (0xF << 28));
    PC_CFG0 |=  ((0x4 << 8) | (0x1 << 12) | (0x4 << 16) | (0x1 << 28));

    PC_CFG1 &= ~(0xF << 16);
    PC_CFG1 |=  (0x4 << 16); // PC12 = SPI0_CLK

    // Drive strength Level 3 (high speed)
    PC_DRV0 |= (3 << 4) | (3 << 6) | (3 << 8) | (3 << 14);
    PC_DRV1 |= (3 << 8);

    // Deassert CS lines (Active Low -> default High)
    PC_DATA |= (1 << 3) | (1 << 7);
}

} // namespace fc::hal
