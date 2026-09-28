"""
AbstractX Pytest Configuration and Shared Test Fixtures
-------------------------------------------------------
Standard fixtures verifying compliance with AbstractX Architectural Invariants:
- 64-Byte TLP framing
- Dynamic CTF 1.8 schemas
- SSOT Spec-to-Code traceability
- Zero dynamic heap and non-blocking HAL constraints
"""

import sys
import zlib
import struct
import pytest
from pathlib import Path

# Setup Python paths for tools and visualizer
REPO_ROOT = Path(__file__).resolve().parent.parent
TOOLS_DIR = REPO_ROOT / "tools"
VISUALIZER_DIR = TOOLS_DIR / "visualizer"

for p in [str(REPO_ROOT), str(TOOLS_DIR), str(VISUALIZER_DIR)]:
    if p not in sys.path:
        sys.path.insert(0, p)

@pytest.fixture(scope="session")
def repo_root() -> Path:
    return REPO_ROOT

@pytest.fixture(scope="session")
def design_spec_path(repo_root) -> Path:
    return repo_root / "docs" / "DESIGN_SPECIFICATION.md"

@pytest.fixture(scope="session")
def app_spec_path(repo_root) -> Path:
    return repo_root / "apps" / "gps_imu_app" / "SPECIFICATION.md"

@pytest.fixture(scope="session")
def trace_schema_path(repo_root) -> Path:
    return repo_root / "apps" / "gps_imu_app" / "trace_schema.json"

@pytest.fixture(scope="session")
def studio_spec_path(repo_root) -> Path:
    return repo_root / "tools" / "visualizer" / "SPECIFICATION.md"

@pytest.fixture
def sample_tlp64_frame() -> bytes:
    """
    Generates a valid 64-byte AbstractX TLP frame according to include/asp_tlp64.hpp:
    - 20-byte Header: type, flags, tag, channel, target_addr, len_dw, seq, timestamp_ns
    - 40-byte CTF Payload: roll, pitch, yaw, motor outputs (m1..m4)
    - 4-byte CRC32 (IEEE 802.3 standard)
    """
    tlp_type = 0x10       # MEM_WRITE_POSTED
    flags = 0x00
    tag = 0x04            # AHRS State tag
    channel = 0x02        # High-Rate Telemetry Channel
    target_addr = 0x40000100
    len_dw = 16           # 16 DWords == 64 Bytes
    seq = 42
    timestamp_ns = 1720000000123456789

    header_bytes = struct.pack("<BBBBIHHQ", tlp_type, flags, tag, channel, target_addr, len_dw, seq, timestamp_ns)

    # 40-byte payload: roll, pitch, yaw, m1..m4 demands (scaled floats or int32)
    # Event ID (1B), subchannel (1B), roll (2B), pitch (2B), yaw (2B), m1..m4 (4x2B = 8B), padding (24B)
    event_id = 0x04
    payload_content = struct.pack("<Bffff", event_id, 12.5, -4.2, 178.0, 0.65)
    padding = b"\x00" * (40 - len(payload_content))
    payload_bytes = payload_content + padding

    data_before_crc = header_bytes + payload_bytes
    crc32 = zlib.crc32(data_before_crc) & 0xFFFFFFFF
    crc_bytes = struct.pack("<I", crc32)

    frame = data_before_crc + crc_bytes
    assert len(frame) == 64
    return frame
