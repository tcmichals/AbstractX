#!/usr/bin/env python3
"""
Copyright (C) 2026 Tim Michals
SPDX-License-Identifier: GPL-3.0-or-later

AbstractX Application Specification & Schema Generator (create_app_spec.py)
--------------------------------------------------------------------------
Automates the creation of canonical, compliant SPECIFICATION.md and
trace_schema.json files for AbstractX applications. Enforces the
Primary-Paced Channel pattern, zero-heap rules, and multi-target invariants.

Usage:
  python3 tools/create_app_spec.py --app quad_ahrs_app \
    --primary imu:8000:spi \
    --aux mag:50:i2c,gps:10:uart,baro:20:i2c
"""

import sys
import json
import argparse
from pathlib import Path

SPEC_TEMPLATE = """# {app_title} Application Specification (`apps/{app_name}`)

This document is the **authoritative architectural design specification** for the **{app_title} (`apps/{app_name}/`)**. It specifies the multi-rate sensor channel pipeline, parallel hardware initialization, flight control loop, and cross-target execution invariants across **Raspberry Pi Pico 2 W (RP2350)**, **Espressif ESP32-P4**, and **ARM Cortex-A55 Linux**.

---

## 1. Application Overview & Objectives

The `{app_name}` coordinates multi-rate sensor streams:
- **Primary Pacer**: {primary_name} ({primary_bus_upper}) @ {primary_rate} Hz
{aux_list_md}
- **100% Portability**: Identical single-source C++20 code executes across all target platforms with **zero application `#ifdef`s**.

```mermaid
flowchart TD
    subgraph HW_LAYER["1. Physical Hardware Sensors"]
        HW_PRIM["<b>{primary_name}</b><br/>{primary_bus_upper} @ {primary_rate} Hz"]
{hw_aux_mermaid}
    end

    subgraph PROD_LAYER["2. Dedicated Coroutine Producers"]
        P_PRIM["<b>{primary_name_lower}_producer_task</b><br/><code>co_await {primary_name_lower}.next_sample_async()</code>"]
{prod_aux_mermaid}
    end

    subgraph CHAN_LAYER["3. Typed Asynchronous Channels (0 B Heap)"]
        Q_PRIM["<b>g_{primary_name_lower}_channel</b><br/><code>AsyncQueue&lt;{primary_name}Sample, 32&gt;</code>"]
{chan_aux_mermaid}
    end

    subgraph FUSION_LAYER["4. Primary-Paced Execution Loop"]
        F_PACE["<b>1. Pacing Await ({primary_rate} Hz):</b><br/><code>co_await g_{primary_name_lower}_channel.pop()</code>"]
        F_DRAIN["<b>2. Non-Blocking Aux Drains:</b><br/>{drain_mermaid_text}"]
        F_EXEC["<b>3. Execution & State Update:</b><br/>Process sensor state and generate control outputs"]
        F_PACE --> F_DRAIN --> F_EXEC
    end

    subgraph TELEM_LAYER["5. Telemetry Egress"]
        RING["<b>g_telemetry_ring</b> (SpscTlpRing&lt;64&gt;)"]
        NET["<b>UDP :9870 / CTF Stream</b>"]
        RING --> NET
    end

    HW_PRIM --> P_PRIM -->|try_push| Q_PRIM
{connect_aux_mermaid}

    Q_PRIM -.->|Pacing Clock| F_PACE
{drain_conn_mermaid}

    F_EXEC -->|Decimated TLP| RING

    classDef hwStyle fill:#1e1b4b,stroke:#818cf8,stroke-width:2px,color:#ffffff;
    classDef prodStyle fill:#4c1d95,stroke:#c084fc,stroke-width:2px,color:#ffffff;
    classDef chanStyle fill:#14532d,stroke:#4ade80,stroke-width:2px,color:#ffffff;
    classDef fuseStyle fill:#0f172a,stroke:#38bdf8,stroke-width:2px,color:#ffffff;
    classDef telStyle fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#ffffff;

    class HW_PRIM{hw_class_targets} hwStyle;
    class P_PRIM{prod_class_targets} prodStyle;
    class Q_PRIM{chan_class_targets} chanStyle;
    class F_PACE,F_DRAIN,F_EXEC fuseStyle;
    class RING,NET telStyle;
```

---

## 2. Structured Concurrency Parallel Boot (`when_all`)

Peripherals MUST be initialized concurrently without sequential delays:

```cpp
// [SPEC-APP-01] Parallel Structured Hardware Boot
auto [{init_return_vars}] = co_await coro::when_all(
{init_call_list}
);
```

Total boot time is bounded by $\\max(T_i)$ rather than $\\sum T_i$.

---

## 3. Multi-Rate Channel Execution Contract

The application strictly implements the **Primary-Paced Coroutine Channel Pattern**:
1. High-rate `{primary_name}` paces the loop via `co_await g_{primary_name_lower}_channel.pop()`.
2. Auxiliary lower-rate channels are drained non-blockingly via `try_pop()` without stalling the primary loop.
3. Ad-hoc tick prescalers and switch-case state machines are strictly prohibited.

---

## 4. Multi-Target Silicon Portability Invariants

The application code must compile and execute identically across:
1. **Raspberry Pi Pico 2 W**: Dual Cortex-M33 @ 150 MHz with single-precision hardware FPU.
2. **Espressif ESP32-P4**: Dual RISC-V @ 400 MHz with hardware FPU.
3. **Allwinner Cubie A5E (ARM A55 Linux)**: Quad AArch64 @ 1.4 GHz with NEON FPU.

### Strict Invariants:
* **0 Bytes Dynamic Heap**: All queues, rings, and coroutine frames must be static.
* **0 Synchronous Bus Calls**: All bus operations must be awaitable.
* **0 Application `#ifdef`s**: Platform-specific initialization is handled autonomously by `abstractx::init()`.

---

## 5. Normative Specification Requirements Matrix

Every requirement below MUST be implemented in code with a matching `@impl` tag:

### `[SPEC-APP-01]` Parallel Hardware Initialization
Peripherals MUST initialize concurrently using `co_await coro::when_all(...)`.

### `[SPEC-APP-02]` Primary-Paced Execution Loop
The control loop MUST be paced exclusively by awaiting the primary `{primary_name}` channel.

### `[SPEC-APP-03]` Non-Blocking Auxiliary Ingestion
Auxiliary sensor channels MUST be drained non-blockingly using `try_pop()`.

### `[SPEC-APP-04]` Zero Dynamic Heap Allocation
All buffers, queues, and tasks MUST allocate from static memory during runtime execution.

### `[SPEC-APP-05]` Multi-Target Portability Guarantee
Application MUST compile and execute identically on Pico 2 W, ESP32-P4, and ARM A55 with zero `#ifdef`s.

### `[SPEC-APP-06]` Dynamic CTF 1.8 Telemetry
Telemetry packets MUST conform to 64-byte TLPs matching `trace_schema.json`.
"""

def parse_sensor(s_str: str):
    parts = s_str.split(":")
    name = parts[0]
    rate = int(parts[1]) if len(parts) > 1 else 100
    bus = parts[2] if len(parts) > 2 else "spi"
    return {"name": name, "rate": rate, "bus": bus}

def main():
    parser = argparse.ArgumentParser(description="AbstractX Application Specification Generator")
    parser.add_argument("--app", type=str, required=True, help="Application directory name (e.g. quad_ahrs_app)")
    parser.add_argument("--title", type=str, default="", help="Human readable title")
    parser.add_argument("--primary", type=str, default="imu:8000:spi", help="Primary sensor (name:rate:bus)")
    parser.add_argument("--aux", type=str, default="mag:50:i2c,gps:10:uart", help="Comma-separated aux sensors")
    parser.add_argument("--outdir", type=str, default="", help="Target output directory")
    args = parser.parse_args()

    app_name = args.app
    app_title = args.title if args.title else app_name.replace("_", " ").title()
    
    root_dir = Path(__file__).resolve().parent.parent
    out_dir = Path(args.outdir) if args.outdir else root_dir / "apps" / app_name
    out_dir.mkdir(parents=True, exist_ok=True)

    prim = parse_sensor(args.primary)
    aux_list = [parse_sensor(s) for s in args.aux.split(",") if s.strip()]

    # Format Markdown strings
    aux_list_md = "\n".join([f"- **Auxiliary**: {a['name'].upper()} ({a['bus'].upper()}) @ {a['rate']} Hz" for a in aux_list])
    
    hw_aux_mermaid = "\n".join([f"        HW_{a['name'].upper()}[\"<b>{a['name'].upper()}</b><br/>{a['bus'].upper()} @ {a['rate']} Hz\"]" for a in aux_list])
    prod_aux_mermaid = "\n".join([f"        P_{a['name'].upper()}[\"<b>{a['name'].lower()}_producer_task</b><br/><code>co_await {a['name'].lower()}.next_sample_async()</code>\"]" for a in aux_list])
    chan_aux_mermaid = "\n".join([f"        Q_{a['name'].upper()}[\"<b>g_{a['name'].lower()}_channel</b><br/><code>AsyncQueue&lt;{a['name'].title()}Sample, 16&gt;</code>\"]" for a in aux_list])
    
    drain_mermaid_text = "<br/>".join([f"<code>while (g_{a['name'].lower()}_channel.try_pop({a['name'].lower()}))</code>" for a in aux_list])
    
    connect_aux_mermaid = "\n".join([f"    HW_{a['name'].upper()} --> P_{a['name'].upper()} -->|try_push| Q_{a['name'].upper()}" for a in aux_list])
    drain_conn_mermaid = "\n".join([f"    Q_{a['name'].upper()} -.->|Aux Ingestion| F_DRAIN" for a in aux_list])
    
    hw_class_targets = "".join([f",HW_{a['name'].upper()}" for a in aux_list])
    prod_class_targets = "".join([f",P_{a['name'].upper()}" for a in aux_list])
    chan_class_targets = "".join([f",Q_{a['name'].upper()}" for a in aux_list])

    init_return_vars = ", ".join([f"{prim['name'].lower()}_ok"] + [f"{a['name'].lower()}_ok" for a in aux_list])
    init_call_list = f"    {prim['name'].lower()}.init_async(),\n" + ",\n".join([f"    {a['name'].lower()}.init_async()" for a in aux_list])

    spec_content = SPEC_TEMPLATE.format(
        app_name=app_name,
        app_title=app_title,
        primary_name=prim['name'].upper(),
        primary_name_lower=prim['name'].lower(),
        primary_rate=prim['rate'],
        primary_bus_upper=prim['bus'].upper(),
        aux_list_md=aux_list_md,
        hw_aux_mermaid=hw_aux_mermaid,
        prod_aux_mermaid=prod_aux_mermaid,
        chan_aux_mermaid=chan_aux_mermaid,
        drain_mermaid_text=drain_mermaid_text,
        connect_aux_mermaid=connect_aux_mermaid,
        drain_conn_mermaid=drain_conn_mermaid,
        hw_class_targets=hw_class_targets,
        prod_class_targets=prod_class_targets,
        chan_class_targets=chan_class_targets,
        init_return_vars=init_return_vars,
        init_call_list=init_call_list
    )

    spec_path = out_dir / "SPECIFICATION.md"
    with open(spec_path, "w", encoding="utf-8") as f:
        f.write(spec_content)
    print(f"[SUCCESS] Generated: {spec_path}")

    # Generate basic trace_schema.json
    schema_dict = {
        "application": app_name,
        "version": "1.0.0",
        "wire_protocol": {
            "magic": "0xC1FC1FC1",
            "frame_size_bytes": 64,
            "header_size_bytes": 20,
            "payload_size_bytes": 40
        },
        "streams": {
            "1": {
                "name": "telemetry",
                "channel": 2,
                "events": {
                    "1": {
                        "name": f"{prim['name'].lower()}_sample",
                        "rate_hz": prim['rate'],
                        "fields": [
                            {"name": "sample_seq", "type": "uint32", "display_name": "Sequence"}
                        ]
                    }
                }
            }
        }
    }
    
    schema_path = out_dir / "trace_schema.json"
    if not schema_path.exists():
        with open(schema_path, "w", encoding="utf-8") as f:
            json.dump(schema_dict, f, indent=2)
        print(f"[SUCCESS] Generated: {schema_path}")

if __name__ == "__main__":
    main()
