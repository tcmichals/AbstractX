"""
Test Suite: 64-Byte TLP Bus Framing & Dynamic CTF 1.8 Schema Decoding
---------------------------------------------------------------------
Verifies that:
1. 64-byte Tlp64 packets conform to wire framing invariants (20B header, 40B payload, 4B CRC32).
2. Dynamic CTF schema loader parses barectf/CTF 1.8 schema without hardcoded byte offsets.
3. Decoded engineering values respect scaling factors, units, and widgets.
"""

import zlib
import struct
import pytest
from tools.visualizer.ctf_schema_loader import CtfSchema

def test_tlp64_wire_framing_dimensions(sample_tlp64_frame):
    """Enforces the strict 64-byte physical wire framing invariant."""
    assert len(sample_tlp64_frame) == 64, f"TLP frame must be exactly 64 bytes, got {len(sample_tlp64_frame)}"

    # Header is first 20 bytes
    header_bytes = sample_tlp64_frame[:20]
    tlp_type, flags, tag, channel, target_addr, len_dw, seq, ts_ns = struct.unpack("<BBBBIHHQ", header_bytes)

    assert len_dw == 16, f"len_dw must be 16 DWords (64 bytes), got {len_dw}"
    assert tlp_type == 0x10, "Expected MEM_WRITE_POSTED (0x10) type"

    # CRC32 is last 4 bytes
    data_payload_and_hdr = sample_tlp64_frame[:60]
    expected_crc = zlib.crc32(data_payload_and_hdr) & 0xFFFFFFFF
    actual_crc = struct.unpack("<I", sample_tlp64_frame[60:64])[0]

    assert actual_crc == expected_crc, f"CRC32 mismatch: expected {hex(expected_crc)}, got {hex(actual_crc)}"

def test_dynamic_ctf_schema_loader(trace_schema_path):
    """Verifies dynamic CTF 1.8 schema loading from JSON."""
    assert trace_schema_path.exists(), f"Schema file missing: {trace_schema_path}"

    schema = CtfSchema(str(trace_schema_path))
    assert 2 in schema.streams_by_channel, "Stream channel 2 (telemetry) not mapped"

    stream = schema.streams_by_channel[2]
    assert 1 in stream.events, "IMU event (1) missing in stream"
    assert 2 in stream.events, "GPS event (2) missing in stream"
    assert 3 in stream.events, "Mag event (3) missing in stream"
    assert 4 in stream.events, "AHRS event (4) missing in stream"

    ahrs_event = stream.events[4]
    assert ahrs_event.name == "ahrs_state"
    field_names = [f["name"] for f in ahrs_event.fields]
    assert "roll_cdeg" in field_names
    assert "pitch_cdeg" in field_names
    assert "yaw_cdeg" in field_names
    assert "m1_throttle" in field_names

def test_tlp_packet_decoding_without_hardcoded_offsets(trace_schema_path):
    """Proves end-to-end decoding of fused AHRS state via dynamic schema."""
    schema = CtfSchema(str(trace_schema_path))

    # Pack an AHRS state payload:
    # Event 4 format: <2hHiH4H (roll, pitch, yaw, alt, speed, m1, m2, m3, m4)
    # roll = 1500 (15.00 deg), pitch = -350 (-3.50 deg), yaw = 9000 (90.00 deg)
    # alt = 150000 (150.000 m), speed = 1250 (12.50 m/s), m1..m4 = 550 us
    event_payload = struct.pack("<2hHiH4H", 1500, -350, 9000, 150000, 1250, 550, 550, 550, 550)
    ctf_payload = event_payload + (b"\x00" * (40 - len(event_payload)))

    # Construct 64B TLP:
    # Tag = 4 maps to event 4
    header = struct.pack("<BBBBIHHQ", 0x10, 0x00, 4, 2, 0x40000100, 16, 888, 123456789000)
    data = header + ctf_payload
    crc32 = zlib.crc32(data) & 0xFFFFFFFF
    tlp_64 = data + struct.pack("<I", crc32)

    decoded = schema.decode_tlp_packet(tlp_64)
    assert decoded is not None
    assert decoded["valid"] is True
    assert decoded["channel"] == 2
    assert decoded["event"] == "ahrs_state"

    fields = decoded["fields"]
    assert "roll_cdeg" in fields
    assert pytest.approx(fields["roll_cdeg"]["value"], 0.01) == 15.0
    assert pytest.approx(fields["pitch_cdeg"]["value"], 0.01) == -3.5
    assert pytest.approx(fields["yaw_cdeg"]["value"], 0.01) == 90.0
    assert pytest.approx(fields["alt_mm"]["value"], 0.01) == 150.0
    assert fields["m1_throttle"]["value"] == 550
    assert fields["roll_cdeg"]["unit"] == "deg"
