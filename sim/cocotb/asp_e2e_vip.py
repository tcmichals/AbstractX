import cocotb
from cocotb.triggers import RisingEdge, Timer

CMD_WRITE_DATA  = 0x80
CMD_READ_STATUS = 0x01
CMD_READ_DATA   = 0x02

class AbstractX_VIP:
    """
    AbstractX Verification IP (VIP)
    
    Emulates the physical Linux SPI driver directly interacting with the 
    FPGA hardware pins in Cocotb. This acts as a unified injection point 
    for End-to-End (E2E) Verification, similar to Vivado's AXI VIP.
    
    Developers can use this class to inject modular AXIS payloads directly 
    from the emulated Linux host, and pull responses asynchronously.
    """
    def __init__(self, dut, clk_domain_freq_mhz=100, spi_freq_mhz=5):
        self.dut = dut
        
        # Tie-off initial SPI states
        self.dut.spi_cs_n.value = 1
        self.dut.spi_sclk.value = 0
        self.dut.spi_mosi.value = 0
        
        # Calculate half-period for parameterized SPI clock in nanoseconds
        self.spi_half_period_ns = int((1000 / spi_freq_mhz) / 2)

    async def reset_hardware(self, cycles=5):
        """Asynchronously resets the targeted core logic bounding the MAC."""
        self.dut.rst_n.value = 0
        for _ in range(cycles):
            await RisingEdge(self.dut.clk)
        self.dut.rst_n.value = 1
        await RisingEdge(self.dut.clk)

    async def _xfer(self, tx_bytes):
        """Low-level bit-banged SPI transaction directly toggling DUT pins."""
        rx_bytes = []
        self.dut.spi_cs_n.value = 0
        await Timer(self.spi_half_period_ns * 2, units='ns') # CS to SCLK setup
        
        for b in tx_bytes:
            miso_byte = 0
            for i in range(8):
                bit_idx = 7 - i
                self.dut.spi_mosi.value = (b >> bit_idx) & 1
                await Timer(self.spi_half_period_ns, units='ns') # Setup time
                
                self.dut.spi_sclk.value = 1
                await Timer(self.spi_half_period_ns, units='ns') # SCLK High
                
                miso_byte = (miso_byte << 1) | int(self.dut.spi_miso.value)
                self.dut.spi_sclk.value = 0
                
            rx_bytes.append(miso_byte)
            
        await Timer(self.spi_half_period_ns * 2, units='ns') # Hold CS
        self.dut.spi_cs_n.value = 1
        await Timer(self.spi_half_period_ns * 2, units='ns') # Inter-transaction gap
        return rx_bytes

    async def write_stream(self, payload):
        """
        [E2E INGRESS] 
        Streams a generic byte payload into the AbstractX Routing Fabric, 
        wrapping it in the standard SPI Hardware WRITE_DATA Command.
        """
        frame = [CMD_WRITE_DATA] + list(payload)
        await self._xfer(frame)

    async def read_status(self):
        """
        [E2E STATUS] 
        Polls the AbstractX Register Bank SPI endpoint.
        Returns: (version, status, rx_len)
        """
        resp = await self._xfer([CMD_READ_STATUS, 0x00, 0x00, 0x00, 0x00])
        version = resp[1]
        status = resp[2]
        rx_len = (resp[3] << 8) | resp[4]
        return version, status, rx_len

    async def read_stream(self, expected_len):
        """
        [E2E EGRESS]
        Consumes exactly `expected_len` bytes from the AbstractX Egress buffer
        using the SPI Hardware READ_DATA command.
        """
        req = [CMD_READ_DATA] + [0x00] * expected_len
        resp = await self._xfer(req)
        return resp[1:] # Strip sequential dummy byte caused by command parsing
