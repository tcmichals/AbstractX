================================================================================
 Radxa Cubie A5E Sensor & Peripheral Test Cheat Sheet
================================================================================

1. 40-PIN HEADER WIRING REFERENCE
--------------------------------------------------------------------------------
  [Pin 2 ] 5V DC    -> GPS Red Wire (VCC)
  [Pin 6 ] GND      -> GPS Black Wire (GND)
  [Pin 11] Cubie TX -> GPS Green Wire (GPS RX in)
  [Pin 13] Cubie RX -> GPS Yellow Wire (GPS TX out at 9600 baud)
  [Pin 3 ] I2C SDA  -> Compass SDA
  [Pin 5 ] I2C SCL  -> Compass SCL

  * CRITICAL: GPS Yellow (TX) connects to Pin 13 (Cubie RX)!
              GPS Green (RX) connects to Pin 11 (Cubie TX)!


2. QUICK TEST SCRIPTS (IN /root/)
--------------------------------------------------------------------------------
  * Full Python Test (Menu with Pinmux check, Serial & I2C tests):
      python3 /root/test_sensors.py

  * Direct Serial Shell Script (Configures 9600 baud and streams NMEA):
      /root/test_serial.sh

  * Direct I2C Shell Script (Scans bus 1):
      /root/test_i2c.sh


3. SERIAL / GPS COMMANDS (/dev/ttyS1)
--------------------------------------------------------------------------------
  * Stream NMEA sentences directly to console:
      stty -F /dev/ttyS1 9600 raw -echo && cat /dev/ttyS1

  * Interactive serial console (microcom):
      microcom -s 9600 /dev/ttyS1
      (To exit: press Ctrl-X)

  * View raw hex / ASCII stream:
      stty -F /dev/ttyS1 9600 raw -echo && cat /dev/ttyS1 | hexdump -C


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
