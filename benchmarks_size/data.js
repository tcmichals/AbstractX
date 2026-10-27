window.BENCHMARK_DATA = {
  "lastUpdate": 1793108450442,
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
      },
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
          "id": "c5a1eaa4ec6f9110982b3fe8d33d04e404188ccf",
          "message": "feat(zynq,e906,drivers): add Zynq target HAL tests, register parity audit, and E906 INAV/compass drivers\n\n- Add Zynq-7000 Target HAL & IoProcessor simulation test (sim/test_zynq_target.cpp)\n- Add Hardware-Software Register Parity auditor (tools/check_register_parity.py) and skill\n- Add Control TLP Wishbone access to zynq_diagnostics.py via UioRegion\n- Add HMC5883L / Compass driver abstractions and SPEC-MAG-01 in DESIGN_SPECIFICATION.md\n- Add E906 UART3 and TWI1 HAL drivers for GPS and compass\n- Add INAV E906 sensor coprocessor application (apps/inav_e906_app/)\n- Fix signal connection in asp_top.sv and add rtl/pio include to rtl-lint skill\n\nVerification:\n- build_host: 69/69 tests passed (100%)\n- build_asan: 69/69 tests passed (100%)\n- build_tsan: 69/69 tests passed (100%)\n- tools/spectrace.py --review: PASS FOR COMMIT (0 issues)\n- tools/audit_specs.py: 47/47 tagged (100.0% coverage)\n- tools/audit_boundaries.py: PASS (100% core purity)\n- tools/audit_interfaces.py: PASS (100% compliance)\n- tools/check_queue_concurrency.py: PASS (232 files checked, 0 defects)\n- tools/check_register_parity.py: PASS (100% register parity verified)\n- verilator lint: PASS (all RTL modules clean)\n\nNOT VERIFIED: Physical FPGA bitstream flashing on hardware, Cubie A5E remote hardware execution.",
          "timestamp": "2026-10-26T20:45:52-05:00",
          "tree_id": "5c7bb586977076a555d5e4e9a45fca7ce1314169",
          "url": "https://github.com/tcmichals/AbstractX/commit/c5a1eaa4ec6f9110982b3fe8d33d04e404188ccf"
        },
        "date": 1793065939056,
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
      },
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
          "id": "2b3970ae04073bae814cd9e0ebddd0aa682c3746",
          "message": "fix(e906): remove inav_e906_app and polled UART3/TWI1 classes; E906 build links again\n\nc5a1eaa broke the E906 build: two polled driver classes in platform.cpp\nadded ~14 KB to every E906 app and e906_repl_app overflowed SRAM_FAST by\n6192 B. TWI1 init also remapped UART2's PB0/PB1, and both classes waited\non hardware inside start_hardware_transfer_from_isr.\n\nINAV belongs in its own repo (AGENTS.md section 14); the E906 runs\ngeneric AbstractX firmware. UART3/TWI1 become ports by moving the\ninterrupt-driven code from e906_repl_app/src/e906_periph.cpp into the\ntarget ([SPEC-E906-HAL-04], added spec-first).\n\nEvidence (this PC):\n- cmake --preset e906 && cmake --build build_e906: exit 0, all segments [ok]\n- analyze_elf_size.sh e906_repl_app e906_ddr.ld:\n  SRAM_FAST 185.1/192.0 KB (96.4%), SRAM_A3_2 45.1/128 KB, DRAM 0.00 MB\n- ctest --test-dir build_host: 69/69 passed\n- audit_specs.py: 47/47; audit_hal_invariants.py: PASS\n- spectrace_matrix.py --changes: 0 untraced hunks\n\nNot verified: board run; ASan/TSan not re-run (E906-only change).\nOther c5a1eaa defects (asp_uart_core.sv, Zynq TLP word order) are\nlisted in engineering_log.md and not fixed here.",
          "timestamp": "2026-10-27T08:34:14-05:00",
          "tree_id": "58e0c7e45e56cd7abb1d9f4c60a3ad3c01427723",
          "url": "https://github.com/tcmichals/AbstractX/commit/2b3970ae04073bae814cd9e0ebddd0aa682c3746"
        },
        "date": 1793108449987,
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