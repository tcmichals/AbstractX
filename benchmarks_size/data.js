window.BENCHMARK_DATA = {
  "lastUpdate": 1793031120304,
  "repoUrl": "https://github.com/tcmichals/AbstractX",
  "entries": {
    "AbstractX Static Size Metrics": [
      {
        "commit": {
          "author": {
            "email": "tcmichals@gmail.com",
            "name": "Tim Michals",
            "username": "tcmichals"
          },
          "committer": {
            "email": "tcmichals@gmail.com",
            "name": "Tim Michals",
            "username": "tcmichals"
          },
          "distinct": true,
          "id": "2dfa62a272b0e184c105b6093760ce396c5b622b",
          "message": "fix(ci): SPEC-PIO-05 named an uncommitted skill, failing the matrix step\n\ntrac.py test --matrix fails on any BROKEN requirement. [SPEC-PIO-05] listed\nhw-sw-register-parity, a skill (and tools/check_register_parity.py) that exists\nonly on the other PC, so CI has been red since 527ae15. It now names\ncocotb-regression and records the parity check as an open item, to restore\nwhen that work is pushed.\n\nEvidence (this PC):\n  ctest build_host 67/67 passed; ctest build_asan 67/67 passed\n  trac.py test --ctest debug=build_host --ctest asan=build_asan --matrix: exit 0\n  audit_specs.py exit 0; pytest tests/test_adversarial_invariants.py 14 passed\n  track_memory_membrowse.py / track_quality_trends.py exit 0 in a clean worktree\nNot run: TSan locally (passed in CI on a0791a0), the advisory steps' real\nresults, E906 cross-build, board.",
          "timestamp": "2026-10-26T10:55:52-05:00",
          "tree_id": "c0b195b902f97293859e3d827368c1bc499f480b",
          "url": "https://github.com/tcmichals/AbstractX/commit/2dfa62a272b0e184c105b6093760ce396c5b622b"
        },
        "date": 1793031118192,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "elf:e906 bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 ram_bytes",
            "value": 93300,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 text",
            "value": 52968,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug ram_bytes",
            "value": 79898,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug text",
            "value": 39566,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto bss",
            "value": 41128,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto data",
            "value": 336,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto ram_bytes",
            "value": 101596,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto text",
            "value": 60132,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release ram_bytes",
            "value": 92884,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release text",
            "value": 52552,
            "unit": "bytes"
          },
          {
            "name": "elf:host bss",
            "value": 100796,
            "unit": "bytes"
          },
          {
            "name": "elf:host data",
            "value": 2676,
            "unit": "bytes"
          },
          {
            "name": "elf:host flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:host ram_bytes",
            "value": 250985,
            "unit": "bytes"
          },
          {
            "name": "elf:host text",
            "value": 147513,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w bss",
            "value": 145660,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w data",
            "value": 6640,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w flash_bytes",
            "value": 362076,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w ram_bytes",
            "value": 152300,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w text",
            "value": 355436,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_used",
            "value": 1230,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_pct",
            "value": 0,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_used",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_pct",
            "value": 4,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_used",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_pct",
            "value": 30,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_used",
            "value": 4759,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_used",
            "value": 27,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_pct",
            "value": 22,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_used",
            "value": 4747,
            "unit": "bytes"
          }
        ]
      }
    ]
  }
}