# AbstractX Documentation Map

Use this page as the entry point for AbstractX ASP protocol, hardware architecture, and governance documentation.

## Canonical Protocol Stack (`asp-tlp-64b`)

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
7. **`PORTABLE_FLIGHT_STACK_ARCHITECTURE.md`** (**NEW**)  
   Architecture specification for porting Betaflight / iNav flight control code to a decoupled PCIe-like Register BAR API (`pcie_reg_api.h`), removing microcontroller HAL driver crud.
8. **`ASP_REQUIREMENTS.md`**  
   Mission, must-have requirements, quality gates, and definition-of-done.
9. **`ASP_VALIDATION_MATRIX.md`**  
   Gate-by-gate validation matrix and verification test plan.

## Suggested Reading Order

- **Flight Stack & Software Architects**: `PORTABLE_FLIGHT_STACK_ARCHITECTURE.md` $\rightarrow$ `IMU_AUTO_DMA_IP_SPEC.md`
- **Protocol & Hardware Designers**: `ASP_SPEC_DIRECTION.md` $\rightarrow$ `ASP_PROTOCOL.md` $\rightarrow$ `ASP_SPI_TRANSPORT.md` $\rightarrow$ `IMU_AUTO_DMA_IP_SPEC.md`
- **Fabric & RTL Engineers**: `ABSTRACTX_SWITCH_FABRIC_ARCHITECTURE.md` $\rightarrow$ `ASP_SPI_REGISTER_MAP.md`
- **Verification Owners**: `ASP_REQUIREMENTS.md` $\rightarrow$ `ASP_VALIDATION_MATRIX.md`
