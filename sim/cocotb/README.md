# AbstractX Cocotb Testbenches

## AXIS seam testbench

- Test file: `test_abstractx_axis_cocotb.py`
- DUT target: `rtl/spi/asp_spi_reg_bank.sv`

This suite validates baseline seam behavior for:

- `READ_STATUS` byte layout,
- `WRITE_DATA` forwarding to ingress valid/ready seam,
- `READ_DATA` consumption from egress valid/ready seam.

## Running (example)

Use your normal cocotb/Verilator workflow and set DUT top to `asp_spi_reg_bank`.

The exact invocation depends on your local Makefile/CMake flow.

## IMU Auto-DMA block regression

Run the focused mode-0/direct-register/Auto-DMA contract test from this
directory with Cocotb and Icarus Verilog installed:

```sh
make SIM=icarus TOPLEVEL=asp_imu_auto_dma \
  MODULE=test_asp_imu_auto_dma_cocotb EXTRA_ARGS=
```

The test checks programmed SPI half-period timing, direct-transfer completion,
DRDY busy-edge overrun and sequence accounting, stable TLP data/valid behavior,
and clean Auto-DMA shutdown.
