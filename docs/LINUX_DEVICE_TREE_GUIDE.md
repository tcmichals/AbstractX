# Linux Device Tree Guide for AbstractX ASP (`LINUX_DEVICE_TREE_GUIDE.md`)

This document provides ready-to-use **Linux Device Tree (`.dts` / `.dtbo`)** node definitions and overlays for configuring `spidev` in **Dual-SPI Mode** (2-bit `SDIO0`/`SDIO1`) and **Standard SPI Mode** (1-bit `MOSI`/`MISO`).

---

## 1. Device Tree Node Definition (`.dts` Snippet)

Add the following `spidev` node under your SoC SPI master node (e.g., `&spi0` or `&spi1`):

```dts
&spi0 {
    status = "okay";
    pinctrl-names = "default";
    pinctrl-0 = <&spi0_pins>; // SCLK, CSn, IO0 (MOSI), IO1 (MISO)

    asp_fpga: spidev@0 {
        compatible = "linux,spidev";
        reg = <0>;                         // Chip Select 0
        spi-max-frequency = <50000000>;    // 50 MHz Max SCLK (or 10000000 for 10 MHz)

        /* DUAL-SPI 2X THROUGHPUT PROPERTIES */
        spi-tx-bus-width = <2>;            // 2-bit Dual-SPI TX (SDIO0 + SDIO1)
        spi-rx-bus-width = <2>;            // 2-bit Dual-SPI RX (SDIO0 + SDIO1)

        /* Host Doorbell Interrupt Request Pin (Optional) */
        interrupt-parent = <&pio>;
        interrupts = <3 36 IRQ_TYPE_EDGE_RISING>; // Example GPIO Interrupt Pin
    };
};
```

---

## 2. Explanation of Key Device Tree Properties

| Device Tree Property | Value | Architectural Function |
|---|---|---|
| **`spi-tx-bus-width`** | `<2>` | Enables Linux kernel 2-bit Dual-SPI write transfers over `SDIO0` and `SDIO1` simultaneously (**2x Speed**). |
| **`spi-rx-bus-width`** | `<2>` | Enables Linux kernel 2-bit Dual-SPI read transfers over `SDIO0` and `SDIO1` simultaneously (**2x Speed**). |
| **`spi-tx-bus-width`** | `<1>` (or omitted) | Forces Linux kernel to operate in standard 1-bit Single-SPI Mode (`MOSI`). |
| **`spi-rx-bus-width`** | `<1>` (or omitted) | Forces Linux kernel to operate in standard 1-bit Single-SPI Mode (`MISO`). |
| **`spi-max-frequency`**| `<50000000>` | Sets maximum SPI clock rate (supports any rate from `100000` to `50000000`). |

---

## 3. Device Tree Overlay (`.dts` Overlay File `abstractx.dts`)

Compile and apply this Device Tree Overlay dynamically on Allwinner, Raspberry Pi, or i.MX Linux:

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "allwinner,sun8i-a83t", "brcm,bcm2835", "arm,vexpress";

    fragment@0 {
        target = <&spi0>;
        __overlay__ {
            status = "okay";

            spidev@0 {
                compatible = "linux,spidev";
                reg = <0>;
                spi-max-frequency = <25000000>;
                spi-tx-bus-width = <2>;
                spi-rx-bus-width = <2>;
            };
        };
    };
};
```

### Compiling the Overlay
```bash
dtc -I dts -O dtb -o abstractx.dtbo abstractx.dts
```
