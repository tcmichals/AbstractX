# Zynq ICM-42688-P Backend Validation POC

This program runs one shared sensor-validation suite through either Linux
userspace GPIO-event-to-SPI acquisition or AbstractX PL DRDY-to-SPI Auto-DMA.
It is for hardware bring-up and measurement, not a simulator or a claim that
either backend is faster.

## Requirements

* Python 3 with `spidev` and libgpiod's Python v2 bindings for the Linux
  backend.
* A configured ICM-42688-P wired to the selected SPI master and DRDY input.
* For the PL backend, a matching bitstream, generic UIO device with CSR and
  reserved DMA maps, and the reserved DDR range described by the parent
  specification.

The sensor must be routed to only one SPI master at a time. Confirm the board's
PS GPIO line offset and safe PS/PL SPI route before running either backend.
Those physical routing details are board bring-up items; the default GPIO
offset is intentionally not guessed.

## Run

From this directory, first run the host-only tests:

```sh
python3 -m unittest -v test_imu_validation
```

Linux userspace mode:

```sh
python3 run_imu_validation.py \
  --backend linux-userspace \
  --gpiochip /dev/gpiochip0 --gpio-line <verified-offset> \
  --duration-seconds 10 --warmup 100 --odr-hz 8000 \
  --speed-hz 10000000 --board qmtech-zynq7020 \
  --bitstream-id <revision> --software-id <revision> \
  --json linux-userspace.json
```

AbstractX Auto-DMA mode:

```sh
python3 run_imu_validation.py \
  --backend abstractx-auto-dma --uio /dev/uio0 \
  --duration-seconds 10 --warmup 100 --odr-hz 8000 \
  --speed-hz 10000000 --board qmtech-zynq7020 \
  --bitstream-id <revision> --software-id <revision> \
  --json abstractx-auto-dma.json
```

Compare two successful results with identical sensor setup and workload:

```sh
python3 run_imu_validation.py \
  --compare linux-userspace.json abstractx-auto-dma.json
```

The reports preserve per-sample payloads and sequence IDs, event/host timing,
PL service timing, effective SPI-rate information, and explicit failure cases.
Linux event timestamps and PL timestamps are different clock domains and are
never subtracted from one another. Unknown or mismatched effective bus clocks
are called out by the comparison tool; raw ratios do not establish a winner.

The application writes configuration registers on the connected sensor and
arms its selected acquisition path. Run only after verifying the wiring,
interrupt polarity, bus ownership, and power state for the target board.
