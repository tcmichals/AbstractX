import cocotb
from cocotb.triggers import RisingEdge, ReadOnly
import random

class ASP_AXIS_Driver:
    """
    AbstractX Standard AXI-Stream Driver
    Injects randomized stall delays (inter-byte gaps) to prove that 
    downstream hardware correctly honors tvalid and does not speculatively latch.
    """
    def __init__(self, clk, tdata, tvalid, tready, tlast=None):
        self.clk = clk
        self.tdata = tdata
        self.tvalid = tvalid
        self.tready = tready
        self.tlast = tlast
        
        self.tvalid.value = 0
        if self.tlast is not None:
            self.tlast.value = 0

    async def send_frame(self, data_bytes, random_delays=True):
        """Sends an array of bytes bounded by tlast"""
        if not data_bytes:
            return

        for i, b in enumerate(data_bytes):
            # Assert randomized pipeline stalls (simulate slow sources)
            if random_delays and random.random() < 0.3:
                self.tvalid.value = 0
                if self.tlast is not None: self.tlast.value = 0
                for _ in range(random.randint(1, 3)):
                    await RisingEdge(self.clk)

            # Present data
            self.tdata.value = b
            self.tvalid.value = 1
            if self.tlast is not None:
                self.tlast.value = 1 if i == len(data_bytes) - 1 else 0

            # Wait for handshake from downstream (tready == 1)
            while True:
                await ReadOnly()
                if self.tready.value == 1:
                    break
                await RisingEdge(self.clk)

            # Advance clock
            await RisingEdge(self.clk)

        # Terminate transaction
        self.tvalid.value = 0
        if self.tlast is not None:
            self.tlast.value = 0


class ASP_AXIS_Monitor:
    """
    AbstractX Standard AXI-Stream Monitor 
    Subject modules to highly randomized, hostile backpressure (tready toggling)
    to verify absolute data integrity and tlast framing lock.
    """
    def __init__(self, clk, tdata, tvalid, tready, tlast=None):
        self.clk = clk
        self.tdata = tdata
        self.tvalid = tvalid
        self.tready = tready
        self.tlast = tlast
        
        self.received_frames = []
        self._current_frame = []
        
        self.tready.value = 0
        self._running = True
        cocotb.start_soon(self._run())

    async def _run(self):
        while self._running:
            # Implement AbstractX Invariants: Random Backpressure Torture Test
            # 25% chance to brutally stall the pipeline
            if random.random() < 0.25:
                self.tready.value = 0
            else:
                self.tready.value = 1

            await ReadOnly()
            if self.tvalid.value == 1 and self.tready.value == 1:
                self._current_frame.append(int(self.tdata.value))
                
                # Check framing boundary
                if self.tlast is not None and self.tlast.value == 1:
                    self.received_frames.append(bytes(self._current_frame))
                    self._current_frame = []

            await RisingEdge(self.clk)

    def stop(self):
        self._running = False
        self.tready.value = 0
