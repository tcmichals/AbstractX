import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer

async def _reset_dut(dut, cycles=5):
    dut.rst.value = 1
    dut.i_cs_n.value = 1
    dut.i_sclk.value = 0
    dut.i_mosi.value = 0
    dut.i_rx_ready.value = 0
    dut.i_tx_byte.value = 0
    dut.i_tx_valid.value = 0

    for _ in range(cycles):
        await RisingEdge(dut.clk)
    dut.rst.value = 0
    await RisingEdge(dut.clk)

async def spi_exchange_byte(dut, mosi_byte):
    """Mocks SPI Master transmitting one byte (Mode 0: CPOL=0, CPHA=0)"""
    miso_byte = 0
    dut.i_cs_n.value = 0
    await Timer(20, units='ns') # CS to SCLK delay
    
    for i in range(8):
        bit_idx = 7 - i
        # Setup MOSI 
        dut.i_mosi.value = (mosi_byte >> bit_idx) & 1
        await Timer(20, units='ns') # Setup time
        
        # SCLK Rising Edge (Slave samples MOSI)
        dut.i_sclk.value = 1
        await Timer(20, units='ns')
        
        # Sample MISO (it was shifted out by slave on previous falling edge / CS fall)
        miso_byte = (miso_byte << 1) | int(dut.o_miso.value)
        
        # SCLK Falling Edge (Slave shifts next MISO)
        dut.i_sclk.value = 0
    
    await Timer(20, units='ns')
    dut.i_cs_n.value = 1
    await Timer(20, units='ns')
    
    return miso_byte

@cocotb.test()
async def test_spi_rx_to_core(dut):
    """Validate MOSI bytes successfully decode into o_rx_byte stream."""
    cocotb.start_soon(Clock(dut.clk, 10, units="ns").start()) # 100MHz core clock
    await _reset_dut(dut)
    
    dut.i_rx_ready.value = 0 # Block consumption so o_rx_valid holds high
    
    # Send 0xAB over SPI (master clock is much slower than 100MHz core)
    await spi_exchange_byte(dut, 0xAB)
    
    # Wait for the cross-domain sync into core clock to finish
    for _ in range(5):
        await RisingEdge(dut.clk)
        if int(dut.o_rx_valid.value) == 1:
            break
    
    assert int(dut.o_rx_valid.value) == 1, "o_rx_valid should trigger after full byte"
    assert int(dut.o_rx_byte.value) == 0xAB, f"Expected 0xAB, got {hex(int(dut.o_rx_byte.value))}"
    
    # Handshake completion
    dut.i_rx_ready.value = 1
    await RisingEdge(dut.clk)

@cocotb.test()
async def test_core_to_spi_tx(dut):
    """Validate core can enqueue a byte which is serialized out MISO."""
    cocotb.start_soon(Clock(dut.clk, 10, units="ns").start())
    await _reset_dut(dut)
    
    # Enqueue byte 0x55 from core
    dut.i_tx_byte.value = 0x55
    dut.i_tx_valid.value = 1
    await RisingEdge(dut.clk)
    dut.i_tx_valid.value = 0
    
    # Wait for SPI frontend to acknowledge it
    assert int(dut.o_tx_ready.value) == 1, "Frontend must be ready"
    
    # Now exchange a dummy byte from SPI master to clock out the MISO
    miso = await spi_exchange_byte(dut, 0x00)
    
    assert miso == 0x55, f"Expected TX byte 0x55 on MISO, got {hex(miso)}"
