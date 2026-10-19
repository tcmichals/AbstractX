================================================================================
 Radxa Cubie A5E Sensor & Peripheral Test Cheat Sheet
 Detailed Live Test Results: see TEST_RESULTS.md
================================================================================

1. 40-PIN HEADER WIRING GROUND TRUTH (Radxa Cubie A5E Schematic v1.2)
--------------------------------------------------------------------------------
  [Pin 2 ] 5V DC    -> GPS/Compass Red Wire (VCC)
  [Pin 6 ] GND      -> GPS/Compass Black Wire (GND)
  [Pin 16] UART3-TX -> GPS Yellow Wire (GPS RX in) [PI11]
  [Pin 31] UART3-RX -> GPS Green Wire (GPS TX out at 115200 baud) [PI12]
  [Pin 3 ] I2C1-SDA -> Compass SDA (PB5)
  [Pin 5 ] I2C1-SCL -> Compass SCL (PB4)

  * CRITICAL CROSSOVER: GPS Green (TX) connects to Pin 31 (Cubie RX)!
                        GPS Yellow (RX) connects to Pin 16 (Cubie TX)!
  * NOTE: Pins 11 & 13 connect to PI9/PI10 (No UART hardware block).
          UART3 is routed to Pins 16 and 31!

2. QUICK TEST SCRIPTS (IN /root/)
--------------------------------------------------------------------------------
  * Automated Host Sensor Test Runner (Records to SpecTrace Evidence Ledger):
      python3 tools/test_e906_sensors_board.py

  * MicroPython Flight Sensors Ingestion (on E906 Coprocessor):
      python3 /root/run_e906_script.py /root/flight_sensors.py

  * Linux Host Diagnostic Script:
      python3 /root/test_sensors.py

3. SERIAL / GPS COMMANDS (/dev/ttyS3)
--------------------------------------------------------------------------------
  * Stream NMEA sentences directly to console at 115200 baud:
      stty -F /dev/ttyS3 115200 raw -echo && cat /dev/ttyS3

  * Interactive serial console (microcom):
      microcom -s 115200 /dev/ttyS3
      (To exit: press Ctrl-X)

4. I2C COMMANDS (/dev/i2c-1, Pins 3 & 5)
--------------------------------------------------------------------------------
  * Scan all 7-bit addresses on bus 1:

      i2cdetect -y 1

  * Read-mode scan (gentler on compasses):
      i2cdetect -y -r 1

  * Dump registers of a detected device (e.g. 0x0C):
      i2cdump -y 1 0x0C

  * Read single register (e.g. WHO_AM_I at 0x00 from 0x0C):
      i2cget -y 1 0x0C 0x00

  Common Compass / Sensor Addresses:
    - 0x0C: AK09916 / AK09918 (u-blox NEO-M8N internal compass)
    - 0x0D: QMC5883L
    - 0x1E: HMC5883L / LIS3MDL
    - 0x76 / 0x77: BMP280 / MS5611 Barometer


5. HARDWARE REGISTER LEVEL CHECK (devmem)
--------------------------------------------------------------------------------
  * Check Pinmux register PB_CFG0 (Should be 0x77FFFF22):
      devmem 0x02000030

  * Check Pull-up register PB_PULL0 (Should be 0x00105005):
      devmem 0x02000054

  * Check Pin data register PB_DAT:
      devmem 0x02000040
================================================================================
