#!/usr/bin/env python3
"""
Copyright (C) 2026 Tim Michals
SPDX-License-Identifier: GPL-3.0-or-later

AbstractX Common Trace Format (CTF) & TLP Dynamic Schema Loader
----------------------------------------------------------------
Dynamically parses barectf YAML or JSON trace schemas, compiles binary
struct unpack format strings, and decodes 64-byte TLP telemetry frames
into typed, engineering-scaled Python dictionaries.

Usage:
  python3 tools/visualizer/ctf_schema_loader.py [path_to_schema]
"""

import os
import sys
import json
import struct
from pathlib import Path
from typing import Dict, Any, Optional, Tuple

try:
    import yaml
    HAS_YAML = True
except ImportError:
    HAS_YAML = False

TYPE_MAP = {
    "uint8": ("B", 1),
    "int8": ("b", 1),
    "uint16": ("H", 2),
    "int16": ("h", 2),
    "uint32": ("I", 4),
    "int32": ("i", 4),
    "uint64": ("Q", 8),
    "int64": ("q", 8),
    "float": ("f", 4),
    "float32": ("f", 4),
    "double": ("d", 8),
    "float64": ("d", 8),
}

class EventDescriptor:
    def __init__(self, event_id: int, name: str, description: str, fields: list):
        self.id = event_id
        self.name = name
        self.description = description
        self.fields = fields  # list of field dicts with name, type, scale, unit, display_name, widget
        
        # Build struct format
        fmt = "<"
        total_size = 0
        for f in fields:
            t = f.get("type", "uint32")
            if t not in TYPE_MAP:
                raise ValueError(f"Unsupported CTF type: {t}")
            code, size = TYPE_MAP[t]
            fmt += code
            total_size += size
            
        self.struct_fmt = fmt
        self.payload_size = total_size
        self._unpacker = struct.Struct(fmt)

    def unpack(self, payload_bytes: bytes) -> Dict[str, Any]:
        if len(payload_bytes) < self.payload_size:
            return {}
        raw_values = self._unpacker.unpack(payload_bytes[:self.payload_size])
        result = {}
        for f, raw in zip(self.fields, raw_values):
            scale = f.get("scale", 1.0)
            val = raw * scale if scale != 1.0 else raw
            result[f["name"]] = {
                "value": val,
                "raw": raw,
                "unit": f.get("unit", ""),
                "display_name": f.get("display_name", f["name"]),
                "widget": f.get("widget", "default")
            }
        return result

class StreamDescriptor:
    def __init__(self, stream_id: int, name: str, channel: int):
        self.id = stream_id
        self.name = name
        self.channel = channel
        self.events: Dict[int, EventDescriptor] = {}

class CtfSchema:
    def __init__(self, schema_path: str):
        self.path = Path(schema_path)
        self.streams: Dict[int, StreamDescriptor] = {}
        self.streams_by_channel: Dict[int, StreamDescriptor] = {}
        self._load()

    def _load(self):
        if not self.path.exists():
            raise FileNotFoundError(f"Schema file not found: {self.path}")

        if self.path.suffix in [".yaml", ".yml"]:
            if not HAS_YAML:
                raise RuntimeError("PyYAML required to parse .yaml schemas. Run: pip install pyyaml")
            with open(self.path, "r", encoding="utf-8") as f:
                data = yaml.safe_load(f)
            self._parse_barectf_yaml(data)
        elif self.path.suffix == ".json":
            with open(self.path, "r", encoding="utf-8") as f:
                data = json.load(f)
            self._parse_json_schema(data)
        else:
            raise ValueError(f"Unsupported schema format: {self.path.suffix}")

    def _parse_barectf_yaml(self, data: dict):
        streams_data = data.get("streams") or data.get("metadata", {}).get("streams", {})
        stream_id_seq = 0
        for stream_name, s_cfg in streams_data.items():
            sid = s_cfg.get("stream_id", stream_id_seq)
            # Default mapping: telemetry stream -> channel 2
            channel = 2 if stream_name == "telemetry" else (1 if stream_name == "hal_tlp" else 0)
            stream_desc = StreamDescriptor(sid, stream_name, channel)
            
            events_data = s_cfg.get("events", {})
            event_id_seq = 1
            for event_name, e_cfg in events_data.items():
                eid = e_cfg.get("id", event_id_seq) if isinstance(e_cfg, dict) else event_id_seq
                desc = e_cfg.get("description", "") if isinstance(e_cfg, dict) else ""
                
                raw_fields = e_cfg.get("fields", {}) if isinstance(e_cfg, dict) else {}
                field_list = []
                for fname, fmeta in raw_fields.items():
                    if isinstance(fmeta, dict):
                        f_entry = {
                            "name": fname,
                            "type": fmeta.get("type", "uint32"),
                            "scale": float(fmeta.get("scale", 1.0)),
                            "unit": fmeta.get("unit", ""),
                            "display_name": fmeta.get("display_name", fname),
                            "widget": fmeta.get("widget", "default")
                        }
                    else:
                        f_entry = {
                            "name": fname,
                            "type": str(fmeta),
                            "scale": 1.0,
                            "unit": "",
                            "display_name": fname,
                            "widget": "default"
                        }
                    field_list.append(f_entry)
                
                stream_desc.events[eid] = EventDescriptor(eid, event_name, desc, field_list)
                event_id_seq += 1
                
            self.streams[sid] = stream_desc
            self.streams_by_channel[channel] = stream_desc
            stream_id_seq += 1

    def _parse_json_schema(self, data: dict):
        streams_data = data.get("streams", {})
        for sid_str, s_cfg in streams_data.items():
            sid = int(sid_str)
            s_name = s_cfg.get("name", f"stream_{sid}")
            channel = s_cfg.get("channel", sid)
            stream_desc = StreamDescriptor(sid, s_name, channel)
            
            events_data = s_cfg.get("events", {})
            for eid_str, e_cfg in events_data.items():
                eid = int(eid_str)
                e_name = e_cfg.get("name", f"event_{eid}")
                desc = e_cfg.get("description", "")
                fields = e_cfg.get("fields", [])
                stream_desc.events[eid] = EventDescriptor(eid, e_name, desc, fields)
                
            self.streams[sid] = stream_desc
            self.streams_by_channel[channel] = stream_desc

    def decode_tlp(self, tlp_bytes: bytes) -> Optional[Dict[str, Any]]:
        """
        Decodes a 64-byte AbstractX TLP containing a CTF payload.
        Header:
          [0]: type (1B)
          [1]: flags (1B)
          [2]: tag (1B)
          [3]: channel (1B)
          [4..7]: target_addr (4B)
          [8..9]: len_dw (2B)
          [10..11]: seq (2B)
          [12..19]: timestamp_ns (8B)
          [20..59]: CTF payload (40B)
          [60..63]: crc32 (4B)
        """
        if len(tlp_bytes) < 64:
            return None
            
        tlp_type, flags, tag, channel, target_addr, len_dw, seq, timestamp_ns = struct.unpack(
            "<BBBBIHHQ", tlp_bytes[0:20]
        )
        ctf_payload = tlp_bytes[20:60]
        
        # Match stream by channel
        stream = self.streams_by_channel.get(channel)
        if not stream:
            return {
                "valid": True,
                "type": tlp_type,
                "channel": channel,
                "tag": tag,
                "seq": seq,
                "timestamp_ns": timestamp_ns,
                "stream": f"channel_{channel}",
                "event": "unknown",
                "fields": {}
            }
            
        # In AbstractX CTF payload, byte 0 is typically event_id or tag maps to event_id
        event_id = ctf_payload[0] if len(ctf_payload) > 0 else tag
        event = stream.events.get(event_id)
        
        # Fallback: check if tag matches event_id (e.g. tag 4 == ahrs_state)
        if not event and tag in stream.events:
            event_id = tag
            event = stream.events.get(event_id)
            
        decoded_fields = {}
        event_name = f"event_{event_id}"
        
        if event:
            event_name = event.name
            # If payload[0] was event_id, payload data starts at offset 1 or directly
            # For 64B TLPs with direct field maps:
            try:
                decoded_fields = event.unpack(ctf_payload)
            except Exception:
                decoded_fields = {}
                
        return {
            "valid": True,
            "type": tlp_type,
            "channel": channel,
            "tag": tag,
            "seq": seq,
            "timestamp_ns": timestamp_ns,
            "stream": stream.name,
            "event": event_name,
            "fields": decoded_fields
        }

    decode_tlp_packet = decode_tlp

def main():
    root = Path(__file__).resolve().parent.parent.parent
    yaml_path = root / "trace" / "barectf_config.yaml"
    json_path = root / "apps" / "gps_imu_app" / "trace_schema.json"
    
    target_path = json_path if json_path.exists() else yaml_path
    if len(sys.argv) > 1:
        target_path = Path(sys.argv[1])
        
    print(f"Loading CTF Schema: {target_path}")
    schema = CtfSchema(str(target_path))
    
    print("=" * 60)
    print(f"Loaded {len(schema.streams)} Streams:")
    for sid, stream in sorted(schema.streams.items()):
        print(f"  Stream [{sid}] '{stream.name}' (Channel {stream.channel}):")
        for eid, ev in sorted(stream.events.items()):
            print(f"    Event [{eid}] '{ev.name}' ({ev.payload_size} bytes):")
            for f in ev.fields:
                unit_str = f" [{f['unit']}]" if f.get('unit') else ""
                scale_str = f" (x{f['scale']})" if f.get('scale', 1.0) != 1.0 else ""
                print(f"      - {f['name']} ({f['type']}){scale_str}{unit_str}: {f['display_name']}")
    print("=" * 60)
    print("[SUCCESS] Schema parsed and compiled successfully!")

if __name__ == "__main__":
    main()
