# AbstractX Documentation Map

Use this page as the entry point for AbstractX ASP protocol, hardware architecture, and governance documentation.

## Canonical Protocol & Architecture Stack (`asp-tlp-64b`)

1. **`ASP_SPEC_DIRECTION.md`**  
   Protocol direction specifying the PCIe-like 64-byte TLP (`asp-tlp-64b`) architecture.
2. **`ASP_PROTOCOL.md`**  
   Normative 64-byte TLP frame format, PCIe-style operations (`MemRd`, `MemWr`, `CplD`, `DMA_Stream`), and payload specifications.
3. **`ASP_SPI_TRANSPORT.md`**  
   Dual-SPI and Single-SPI physical layer transport profile, 256-clock cycle burst mechanics, and hardware shift register seams.
4. **`ASP_SPI_REGISTER_MAP.md`**  
   Dual-SPI command byte set (`0xA1 TLP_WRITE_BURST`, `0xA2 TLP_READ_BURST`, `0xA0 TLP_READ_STATUS`) and Wishbone register address map.
5. **`ABSTRACTX_SWITCH_FABRIC_ARCHITECTURE.md`**  
   Switch fabric topology, 512-bit parallel vector routing, Wishbone master gateway, and channel routing.
6. **`IMU_AUTO_DMA_IP_SPEC.md`**  
   Dedicated hardware IMU SPI Master & Auto-DMA IP Core specification, `IMU_INT` trigger workflow, and timestamped telemetry stream generation.
7. **`PORTABLE_FLIGHT_STACK_ARCHITECTURE.md`**  
   Architecture specification for porting Betaflight / iNav / ArduPilot flight control code to a decoupled PCIe-like Register BAR API (`pcie_reg_api.h`), removing microcontroller HAL driver crud.

## Hardware Pinout Maps & Constraints

8. **`TANG9K_PINOUT.md`** (**NEW**)  
   Physical package pinout map for Tang Nano 9K FPGA (`GW1NR-9`), detailing Dual-SPI host pins, IMU SPI master pins, DShot 1..8 motor outputs, and NeoPixel LED output.
9. **`PRIMER20K_PINOUT.md`** (**NEW**)  
   Physical package pinout map for Tang Primer 20K FPGA (`GW2A-18`), detailing Dual-SPI host pins, IMU SPI master pins, DShot 1..8 motor outputs, and NeoPixel LED output.

## Governance & Verification

10. **`ASP_REQUIREMENTS.md`**  
    Mission, must-have requirements, quality gates, and definition-of-done.
11. **`ASP_VALIDATION_MATRIX.md`**  
    Gate-by-gate validation matrix and verification test plan.

## Suggested Reading Order

- **Flight Stack & Software Architects**: `PORTABLE_FLIGHT_STACK_ARCHITECTURE.md` $\rightarrow$ `IMU_AUTO_DMA_IP_SPEC.md`
- **FPGA Board & Hardware Designers**: `TANG9K_PINOUT.md` $\rightarrow$ `PRIMER20K_PINOUT.md`
- **Protocol & Hardware Designers**: `ASP_SPEC_DIRECTION.md` $\rightarrow$ `ASP_PROTOCOL.md` $\rightarrow$ `ASP_SPI_TRANSPORT.md`
- **Fabric & RTL Engineers**: `ABSTRACTX_SWITCH_FABRIC_ARCHITECTURE.md` $\rightarrow$ `ASP_SPI_REGISTER_MAP.md`
